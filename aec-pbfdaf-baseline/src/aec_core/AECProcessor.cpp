#include "AECProcessor.h"
#include "FftUtil.h"
#include <cmath>
#include <algorithm>
#include <cstring>

static inline float sq(float x) { return x * x; }

AECProcessor::AECProcessor() : 
    xIndex(0), xPowerSum(0.0f), 
    delayIdx(0), currentLag(0), maxLag(0), lastLag(0),
    instantErle(0.0f), maxErle(0.0f), avgErle(0.0f), convergedTimeMs(0.0f), lastDelayChangeTime(0.0f),
    blockSize(0), blockCount(0), micPowerSum(0.0f), refPowerSum(0.0f),
    errPowerSum(0.0f), yPowerSum(0.0f), freeze(false), 
    muDynamic(0.0f), muMin(0.0f), muMax(0.0f), iirAlpha(0.0f), 
    micE(0.0f), refE(0.0f), errE(0.0f), 
    freezeBlocks(0), dtdFreezeSamples(0), delayFreezeSamples(0), delayUpdateCounter(0), totalBlocks(0),
    fdafM(0), fdafN(0), numPartitions(0), constraintIdx(0), fdafBufIdx(0), 
    outFifoRead(0), outFifoWrite(0), outFifoCount(0),
    coherence(0.0f), farEndEnergy(0.0f)
{
    statsSeq.store(0);
    // Initialize atomic parameters with defaults
    atomicMu.store(0.05f);
    atomicMuMin.store(0.01f);
    atomicMuMax.store(0.1f);
    atomicDtdAlpha.store(2.0f);
    atomicDtdBeta.store(1.5f);
    atomicFreezeBlocks.store(5);
}

void AECProcessor::initialize(const AECParams& p) {
    params = p;
    
    // --- Common Setup ---
    maxLag = (int)(params.maxDelayMs * params.sampleRate / 1000);
    refDelay.assign(maxLag + params.filterLen + 4096, 0.0f); // Extra buffer
    refFeed.assign(params.corrBlock, 0.0f);
    micDelay.assign(params.corrBlock, 0.0f);
    delayIdx = 0;
    currentLag = 0;
    lastLag = 0;
    
    // --- Time Domain Setup (Legacy) ---
    w.assign(params.filterLen, 0.0f);
    x.assign(params.filterLen, 0.0f);
    xIndex = 0;
    xPowerSum = 0.0f;
    
    // --- Frequency Domain Setup (PBFDAF) ---
    fdafM = 256; // Block size
    fdafN = fdafM * 2; // FFT size (Overlap-Save)
    // Ensure filterLen is multiple of M
    size_t alignedLen = (params.filterLen + fdafM - 1) / fdafM * fdafM;
    numPartitions = alignedLen / fdafM;
    if (numPartitions < 1) numPartitions = 1;
    constraintIdx = 0;
    
    // Resize Buffers
    fdafMicBuf.assign(fdafM, 0.0f);
    fdafRefBuf.assign(fdafM, 0.0f);
    fdafBufIdx = 0;
    
    // Resize State
    X_freq.assign(numPartitions, std::vector<std::complex<float>>(fdafN, {0.0f, 0.0f}));
    W_freq.assign(numPartitions, std::vector<std::complex<float>>(fdafN, {0.0f, 0.0f}));
    E_freq.assign(fdafN, {0.0f, 0.0f});
    Y_freq.assign(fdafN, {0.0f, 0.0f});
    fftScratch.resize(fdafN);
    olaBuffer.assign(fdafM, 0.0f); // Only need M output samples
    powerSpectralDensity.assign(fdafN, 100.0f); // Initial small regularization
    
    // Coherence State
    psd_ref.assign(fdafN, 1e-9f);
    psd_mic.assign(fdafN, 1e-9f);
    psd_cross.assign(fdafN, {0.0f, 0.0f});

    // Output FIFO
    outputFifo.assign(4096, 0.0f);
    outFifoRead = 0;
    outFifoWrite = 0;
    outFifoCount = 0;
    
    // --- Stats / Runtime ---
    blockSize = params.corrBlock;
    blockCount = 0;
    micPowerSum = 0.0f; refPowerSum = 0.0f; errPowerSum = 0.0f; yPowerSum = 0.0f;
    freeze = false;
    
    muDynamic = params.mu;
    muMin = params.mu * 0.1f;
    muMax = params.mu * 1.0f;
    
    iirAlpha = 0.95f; // For DTD energy smoothing
    micE = 0.0f; refE = 0.0f; errE = 0.0f;
    
    freezeBlocks = 5;
    dtdFreezeSamples = 0;
    delayFreezeSamples = 0;
    
    delayUpdateCounter = 0;
    instantErle = 0.0f;
    maxErle = 0.0f;
    convergedTimeMs = 0.0f;
    
    // Initialize atomic copies
    atomicMu.store(params.mu);
    atomicDtdAlpha.store(params.dtdAlpha);
    atomicDtdBeta.store(params.dtdBeta);
    
    micPrev.assign(fdafM, 0.0f);
    avgErle = 0.0f;
    totalBlocks = 0;
}

void AECProcessor::setMu(float val) { atomicMu.store(val); }
void AECProcessor::setMuRange(float min, float max) { atomicMuMin.store(min); atomicMuMax.store(max); }
void AECProcessor::setDtdParams(float alpha, float beta) { atomicDtdAlpha.store(alpha); atomicDtdBeta.store(beta); }
void AECProcessor::setFreezeBlocks(int blocks) { atomicFreezeBlocks.store(blocks); }

// Cyclic dot product helper for time-domain
static inline float dot_cyclic(const std::vector<float>& w, const std::vector<float>& x, size_t head) {
    size_t n = w.size();
    float sum = 0.0f;
    // Split loop: head -> 0, n-1 -> head+1
    // x is filled backwards? No, typically x is circular buffer.
    // If x[head] is newest. x[head-1] is older.
    // w[0] corresponds to newest x.
    
    // Part 1: i from 0 to head
    // w[i] * x[head - i]
    for (size_t i = 0; i <= head; ++i) {
        sum += w[i] * x[head - i];
    }
    // Part 2: i from head+1 to n-1
    // w[i] * x[n + head - i]
    for (size_t i = head + 1; i < n; ++i) {
        sum += w[i] * x[n + head - i];
    }
    return sum;
}

void AECProcessor::process(const float* mic, const float* ref, float* out, size_t frames) {
    // Dispatch to PBFDAF implementation as the primary baseline
    processFrequencyDomain(mic, ref, out, frames);
}

void AECProcessor::processFrequencyDomain(const float* mic, const float* ref, float* out, size_t frames) {
    // Load atomic parameters once per block
    float curMu = atomicMu.load(std::memory_order_relaxed);
    float curDtdAlpha = atomicDtdAlpha.load(std::memory_order_relaxed);
    
    // Delay Estimation Logic (Shared with Time Domain)
    // We update delay estimation continuously using raw streams
    
    // Update Ring Buffers for Delay Estimation
    size_t cap = refDelay.size();
    for (size_t i = 0; i < frames; ++i) {
        // Feed delay line
        refDelay[delayIdx] = ref[i];
        
        // Feed correlation buffers
        if (blockCount < blockSize) {
            refFeed[blockCount] = ref[i];
            micDelay[blockCount] = mic[i];
            
            // Energy Stats for Lag Update logic
            micPowerSum += sq(mic[i]);
            refPowerSum += sq(ref[i]);
        }
        
        delayIdx++;
        if (delayIdx >= cap) delayIdx = 0;
        
        blockCount++;
        if (blockCount >= blockSize) {
             // Delay Update Logic
            bool dtdActive = (dtdFreezeSamples > 0);
            if (!dtdActive && refPowerSum > 1e-6f) {
                updateDelay();
            }
            
            if (currentLag != lastLag) {
                // Delay changed
                delayFreezeSamples = atomicFreezeBlocks.load() * fdafM; // Roughly convert blocks to samples
                lastLag = currentLag;
                delayUpdateCounter++;
                // In Frequency Domain, changing delay means resetting X_freq history or realigning
                // For simplicity, we just clear history to avoid glitches
                for(auto& v : X_freq) std::fill(v.begin(), v.end(), std::complex<float>(0,0));
                std::fill(fdafRefBuf.begin(), fdafRefBuf.end(), 0.0f);
                std::fill(fdafMicBuf.begin(), fdafMicBuf.end(), 0.0f);
                fdafBufIdx = 0;
                // Reset Output FIFO to avoid stale data? No, keep it flowing to avoid clicks.
            }
            
            blockCount = 0;
            micPowerSum = 0.0f; refPowerSum = 0.0f;
        }
    }

    // Main Processing Loop
    size_t fifoCap = outputFifo.size();
    
    for (size_t i = 0; i < frames; ++i) {
        // 1. Get Delayed Reference
        // Calculate read index based on current estimated lag
        int writePos = (int)delayIdx - (int)frames + (int)i;
        while(writePos < 0) writePos += cap;
        while(writePos >= (int)cap) writePos -= cap;
        
        int readPos = writePos - currentLag;
        while(readPos < 0) readPos += cap;
        while(readPos >= (int)cap) readPos -= cap;
        
        float rdel = refDelay[readPos];
        float mval = mic[i];
        
        // 2. Accumulate into Block Buffer
        fdafMicBuf[fdafBufIdx] = mval;
        fdafRefBuf[fdafBufIdx] = rdel;
        fdafBufIdx++;
        
        // 3. Process Block if Full
        if (fdafBufIdx >= fdafM) {
            performBlockFdaf();
            // performBlockFdaf writes result to olaBuffer
            // We push olaBuffer to outputFifo
            for (size_t k = 0; k < fdafM; ++k) {
                outputFifo[outFifoWrite] = olaBuffer[k];
                outFifoWrite = (outFifoWrite + 1) % fifoCap;
            }
            outFifoCount += fdafM;
            if (outFifoCount > fifoCap) outFifoCount = fifoCap; // Overflow safety
            
            fdafBufIdx = 0;
        }
        
        // 4. Output (from FIFO)
        if (outFifoCount > 0) {
            out[i] = outputFifo[outFifoRead];
            outFifoRead = (outFifoRead + 1) % fifoCap;
            outFifoCount--;
        } else {
            // Initial latency or underrun
            out[i] = 0.0f; 
        }
    }
}

void AECProcessor::performBlockFdaf() {
    // 1. Shift History X_freq
    if (numPartitions > 0) {
        std::rotate(X_freq.rbegin(), X_freq.rbegin() + 1, X_freq.rend());
    }

    // 2. FFT of Reference Input
    std::fill(fftScratch.begin(), fftScratch.end(), std::complex<float>(0,0));
    for(size_t i=0; i<fdafM; ++i) {
        fftScratch[i] = { x[i], 0.0f }; // Old Ref
        // This 'x' usage is confusing because 'x' is time-domain ring buffer for legacy.
        // In PBFDAF, we should use 'fdafRefBuf'.
        // But 'x' is not updated in processFrequencyDomain properly for this purpose unless we share logic.
        // Actually, 'fdafRefBuf' holds the CURRENT block.
        // Correct implementation would take fdafRefBuf.
        fftScratch[i] = { fdafRefBuf[i], 0.0f }; 
    }
    FftUtil::fft(fftScratch);
    
    // Store in X_freq[0]
    for(size_t k=0; k<fdafN; ++k) X_freq[0][k] = fftScratch[k];
    
    // 3. Filter Calculation (Convolution in Freq Domain)
    // Y = sum(X[p] * W[p])
    // But first, update Power Spectral Density for normalization (MDF/FDAF)
    
    // Coherence Calc (Simplified)
    // Calculate P_mic, P_ref, P_cross
    float cohSum = 0.0f;
    float alpha = 0.95f; // Smoothing
    
    // Mic FFT for coherence
    std::fill(fftScratch.begin(), fftScratch.end(), std::complex<float>(0,0));
    for(size_t i=0; i<fdafM; ++i) fftScratch[i] = { fdafMicBuf[i], 0.0f };
    FftUtil::fft(fftScratch);
    
    for(size_t k=0; k<fdafN; ++k) {
        float magMic2 = std::norm(fftScratch[k]);
        float magRef2 = std::norm(X_freq[0][k]);
        
        psd_mic[k] = alpha * psd_mic[k] + (1.0f - alpha) * magMic2;
        psd_ref[k] = alpha * psd_ref[k] + (1.0f - alpha) * magRef2;
        
        std::complex<float> cross = fftScratch[k] * std::conj(X_freq[0][k]);
        psd_cross[k] = alpha * psd_cross[k] + (1.0f - alpha) * cross;
        
        // Normalization power (P_est)
        float magX2 = 0.0f;
        for(size_t p=0; p<numPartitions; ++p) magX2 += std::norm(X_freq[p][k]);
        
        powerSpectralDensity[k] = alpha * powerSpectralDensity[k] + (1.0f - alpha) * magX2;

        // Coherence for this bin
        float num = std::norm(psd_cross[k]); // |P_xd|^2
        float den = psd_ref[k] * psd_mic[k] + 1e-9f;
        cohSum += num / den;
    }
    coherence = cohSum / (float)fdafN;

    // 5. Convolution (Filter)
    std::fill(Y_freq.begin(), Y_freq.end(), std::complex<float>(0,0));
    for(size_t p=0; p<numPartitions; ++p) {
        for(size_t k=0; k<fdafN; ++k) {
            Y_freq[k] += X_freq[p][k] * W_freq[p][k];
        }
    }
    
    // 6. IFFT to get linear output
    for(size_t k=0; k<fdafN; ++k) fftScratch[k] = Y_freq[k];
    FftUtil::ifft(fftScratch);
    
    // 7. Overlap-Save & Error Calc
    float sumE2 = 0.0f;
    float sumY2 = 0.0f;
    float sumRef2 = 0.0f;
    
    for(size_t i=0; i<fdafM; ++i) {
        float y_val = fftScratch[fdafM + i].real();
        float m_val = fdafMicBuf[i];
        float e_val = m_val - y_val; // AEC Output
        
        olaBuffer[i] = e_val; 
        
        sumE2 += sq(e_val);
        sumY2 += sq(y_val); 
        sumRef2 += sq(fdafRefBuf[i]); 
    }
    
    // Stats Update
    micE = 0.95f * micE + 0.05f * (sumY2 + sumE2); 
    errE = 0.95f * errE + 0.05f * sumE2;
    refE = 0.95f * refE + 0.05f * sumRef2;
    farEndEnergy = refE;
    
    float curErle = (sumY2 + sumE2) / (sumE2 + 1e-9f);
    float curErleDb = 10.0f * std::log10(curErle + 1e-9f);
    if (curErleDb < 0.0f) curErleDb = 0.0f;
    
    instantErle = curErleDb;
    
    // Increment total processed blocks for convergence tracking
    totalBlocks++;

    // 8. Adaptation Control & DTD (Moved before ERLE to prevent leakage)
    // Check DTD for current block
    if (micE > atomicDtdAlpha.load() * refE && refE > 1e-5f) {
        dtdFreezeSamples = 10;
    } else if (dtdFreezeSamples > 0) {
        dtdFreezeSamples--;
    }

    bool dtdActive = (dtdFreezeSamples > 0);
    
    // Calculate Average ERLE (Gated for Far-End Single-Talk)
    // ERLE is only updated during far-end single-talk (when !dtd) to avoid corruption by near-end speech.
    if (!dtdActive && refE > 1e-6f && totalBlocks > 50) {
         // Slow smoothing (approx 0.5s - 1s time constant)
         avgErle = 0.99f * avgErle + 0.01f * instantErle;
         if (avgErle > maxErle) maxErle = avgErle;
         
         // Track convergence time
         if (avgErle > 10.0f && convergedTimeMs == 0.0f) {
             convergedTimeMs = (float)totalBlocks * (float)fdafM / (float)params.sampleRate * 1000.0f;
         }
    }
    
    if (instantErle > maxErle) maxErle = instantErle;
    
    freeze = dtdActive || (delayFreezeSamples > 0);
    if (delayFreezeSamples > 0) delayFreezeSamples--;
    
    if (!freeze) {
        float mu = atomicMu.load();
        
        // Transform error to frequency domain
        std::fill(fftScratch.begin(), fftScratch.end(), std::complex<float>(0,0));
        for(size_t i=0; i<fdafM; ++i) {
            fftScratch[fdafM + i] = { olaBuffer[i], 0.0f }; // Pad with zeros at front? 
            // Overlap-save update: [0, e] -> FFT -> ...
        }
        FftUtil::fft(fftScratch);
        for(size_t k=0; k<fdafN; ++k) E_freq[k] = fftScratch[k];
        
        // Update weights
        for(size_t p=0; p<numPartitions; ++p) {
            for(size_t k=0; k<fdafN; ++k) {
                // PBFDAF Update Rule
                std::complex<float> num = fftScratch[k] * std::conj(X_freq[p][k]);
                float den = powerSpectralDensity[k] + 1e-9f;
                W_freq[p][k] += mu * num / den;
            }
        }
        
        // Gradient Constraint (Linear Convolution Constraint)
        // Enforce that the time-domain impulse response of the adaptive filter has zero padding.
        // This is critical for PBFDAF correctness (avoids circular convolution artifacts).
        // Optimization: Apply to one partition per block (Round-Robin) to save CPU.
        if (numPartitions > 0) {
            size_t p = constraintIdx;
            constraintIdx = (constraintIdx + 1) % numPartitions;

            // 1. Transform W_freq[p] to time domain
            for(size_t k=0; k<fdafN; ++k) fftScratch[k] = W_freq[p][k];
            FftUtil::ifft(fftScratch);
            
            // 2. Zero out the second half (enforce causality/linear convolution)
            // Ideally we should also zero out the first few samples if there is a system delay,
            // but just zeroing the tail (circular wrap-around part) is the main requirement.
            for(size_t i=fdafM; i<fdafN; ++i) {
                fftScratch[i] = {0.0f, 0.0f};
            }
            
            // 3. Transform back to frequency domain
            FftUtil::fft(fftScratch);
            for(size_t k=0; k<fdafN; ++k) W_freq[p][k] = fftScratch[k];
        }
    }
    
    // Update thread-safe stats
    uint32_t seq = statsSeq.load(std::memory_order_relaxed);
    statsSeq.store(seq + 1, std::memory_order_release);
    
    statsBuf.erle = instantErle;
    statsBuf.maxErle = maxErle;
    statsBuf.avgErle = avgErle;
    statsBuf.convergedTimeMs = convergedTimeMs;
    statsBuf.micE = micE;
    statsBuf.refE = refE;
    statsBuf.errE = errE;
    statsBuf.dtd = freeze; // Reuse freeze flag for DTD indication in GUI
    statsBuf.coherence = coherence; // Add this to stats struct if possible?
    statsBuf.currentLag = currentLag;
    statsBuf.currentLagMs = (float)currentLag * 1000.0f / (float)params.sampleRate;
    statsBuf.mu = atomicMu.load();
    
    statsSeq.store(seq + 2, std::memory_order_release);
}

void AECProcessor::updateDelay() {
    if (maxLag <= 0) return;
    size_t cap = refDelay.size();
    float maxCorr = 0.0f;
    int bestLag = currentLag;
    
    // Coarse search
    for (int lag = 0; lag < maxLag; lag += 4) {
        float corr = 0.0f;
        for (int i = 0; i < blockSize; i += 4) {
             int rIdx = (int)delayIdx - (int)blockSize + i - lag;
             while (rIdx < 0) rIdx += cap;
             while (rIdx >= (int)cap) rIdx -= cap;
             corr += micDelay[i] * refDelay[rIdx];
        }
        if (std::abs(corr) > maxCorr) {
            maxCorr = std::abs(corr);
            bestLag = lag;
        }
    }
    
    // Refine
    int start = bestLag - 4; if (start < 0) start = 0;
    int end = bestLag + 4; if (end > maxLag) end = maxLag;
    maxCorr = 0.0f;
    
    for (int lag = start; lag <= end; ++lag) {
        float corr = 0.0f;
        for (int i = 0; i < blockSize; ++i) {
             int rIdx = (int)delayIdx - (int)blockSize + i - lag;
             while (rIdx < 0) rIdx += cap;
             while (rIdx >= (int)cap) rIdx -= cap;
             corr += micDelay[i] * refDelay[rIdx];
        }
        if (std::abs(corr) > maxCorr) {
            maxCorr = std::abs(corr);
            bestLag = lag;
        }
    }
    
    currentLag = bestLag;
}

void AECProcessor::processTimeDomain(const float* mic, const float* ref, float* out, size_t frames) {
    float curMu = atomicMu.load(std::memory_order_relaxed);
    float curEpsilon = params.epsilon; 
    
    size_t cap = refDelay.size();
    size_t n = w.size();
    if (n == 0) return; // Safety

    for (size_t i = 0; i < frames; ++i) {
        // --- Delay Estimation Logic ---
        refDelay[delayIdx] = ref[i];
        
        if (blockCount < blockSize) {
            refFeed[blockCount] = ref[i];
            micDelay[blockCount] = mic[i];
            micPowerSum += sq(mic[i]);
            refPowerSum += sq(ref[i]);
        }
        
        delayIdx++;
        if (delayIdx >= cap) delayIdx = 0;
        
        blockCount++;
        if (blockCount >= blockSize) {
            bool dtdActive = (dtdFreezeSamples > 0);
            if (!dtdActive && refPowerSum > 1e-6f) {
                updateDelay();
            }
            if (currentLag != lastLag) {
                delayFreezeSamples = atomicFreezeBlocks.load() * blockSize;
                lastLag = currentLag;
                delayUpdateCounter++;
                // Realign x buffer logic could go here (clear x to avoid glitch)
                // std::fill(x.begin(), x.end(), 0.0f);
                // xPowerSum = 0.0f;
            }
            blockCount = 0;
            micPowerSum = 0.0f; refPowerSum = 0.0f;
        }
        
        // --- NLMS Processing ---
        
        // 1. Get Delayed Reference
        int readPos = (int)delayIdx - 1 - currentLag;
        while(readPos < 0) readPos += cap;
        while(readPos >= (int)cap) readPos -= cap;
        
        float r_val = refDelay[readPos];
        float m_val = mic[i];
        
        // 2. Update x ring buffer
        float x_oldest = x[xIndex];
        xPowerSum -= (x_oldest * x_oldest);
        
        x[xIndex] = r_val;
        xPowerSum += (r_val * r_val);
        if (xPowerSum < 0.0f) xPowerSum = 0.0f;
        
        // 3. Filter (Dot Product)
        float y_val = dot_cyclic(w, x, xIndex);
        
        // 4. Error
        float e_val = m_val - y_val;
        out[i] = e_val;
        
        // 5. Update Weights (NLMS)
        if (!freeze && dtdFreezeSamples == 0) {
            float norm = xPowerSum + curEpsilon;
            float step = (curMu * e_val) / norm;
            
            for(size_t j=0; j<n; ++j) {
                int x_idx = (int)xIndex - (int)j;
                if (x_idx < 0) x_idx += n;
                w[j] += step * x[x_idx];
            }
        }
        
        xIndex++;
        if (xIndex >= n) xIndex = 0;
        
        // 6. Stats (Fast approximate)
        micE = 0.999f * micE + 0.001f * sq(m_val);
        errE = 0.999f * errE + 0.001f * sq(e_val);
        refE = 0.999f * refE + 0.001f * sq(r_val);
        
        // Simple DTD Logic
        float dtdThresh = atomicDtdAlpha.load(std::memory_order_relaxed);
        bool isDtd = (micE > dtdThresh * refE) && (refE > 1e-6f);
        if (isDtd) {
            dtdFreezeSamples = 100; // Hold DTD state briefly
        } else if (dtdFreezeSamples > 0) {
            dtdFreezeSamples--;
        }
    }
    
    // Update Stats Buffer
    if (errE > 1e-12f) {
        instantErle = 10.0f * std::log10((micE + 1e-12f) / errE);
    } else {
        instantErle = 0.0f;
    }

    // Calculate Average ERLE (Gated for Far-End Single-Talk)
    // We only update average if:
    // 1. Reference is active (refE > -60dB approx)
    // 2. No Double-Talk detected (isDtd is false)
    // 3. System has converged slightly (ignore first few blocks)
    // dtdFreezeSamples > 0 is our current DTD flag.
    bool dtdActive = (dtdFreezeSamples > 0);
    if (!dtdActive && refE > 1e-6f && totalBlocks > 50) {
        // Slow smoothing (approx 0.5s - 1s time constant)
        // instantErle is computed every block (e.g. 4ms)
        // alpha = 0.01 -> 100 blocks ~ 400ms
        avgErle = 0.99f * avgErle + 0.01f * instantErle;
        if (avgErle > maxErle) maxErle = avgErle;
    }
    totalBlocks++;
    
    uint32_t seq = statsSeq.load(std::memory_order_relaxed);
    statsSeq.store(seq + 1, std::memory_order_release);
    
    statsBuf.erle = instantErle;
    statsBuf.maxErle = maxErle;
    statsBuf.avgErle = avgErle; // Now holding the Gated Average ERLE
    statsBuf.micE = micE;
    statsBuf.refE = refE;
    statsBuf.errE = errE;
    statsBuf.currentLag = currentLag;
    statsBuf.currentLagMs = (float)currentLag * 1000.0f / (float)params.sampleRate;
    statsBuf.mu = curMu;
    statsBuf.dtd = (dtdFreezeSamples > 0);
    statsBuf.freeze = freeze || statsBuf.dtd;
    
    statsSeq.store(seq + 2, std::memory_order_release);
}

AECStats AECProcessor::getStats() const {
    AECStats s;
    uint32_t seq;
    do {
        seq = statsSeq.load(std::memory_order_acquire);
        s = statsBuf;
        std::atomic_thread_fence(std::memory_order_acquire);
    } while (seq % 2 != 0 || seq != statsSeq.load(std::memory_order_relaxed));
    return s;
}