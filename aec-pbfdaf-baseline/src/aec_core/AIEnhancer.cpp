#include "AIEnhancer.h"
#include "FftUtil.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

static inline float hann(int n, int i) {
    return 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * (float)i / (float)(n - 1));
}

static const int kFftSize = 512;
static const int kHopSize = 256;
static const float kNoiseAdapt = 0.1f;
static const float kFloorGain = 0.1f;

AIEnhancer::AIEnhancer() : accIndex(0), initialized(false) {}

void AIEnhancer::initialize(const AIParams& p) {
    params = p;
    window.resize(kFftSize);
    noise.assign(kFftSize, 1e-3f);
    for (int i=0;i<kFftSize;i++) window[i] = hann(kFftSize, i);
    ola.assign(kFftSize, 0.0f);
    acc.assign(kFftSize, 0.0f);
    accIndex = 0;
    initialized = true;
}

void AIEnhancer::process(float* inout, size_t frames) {
    if (!initialized) return;
    
    size_t N = (size_t)kFftSize;
    size_t H = (size_t)kHopSize;
    
    // Buffer for FFT operations
    std::vector<std::complex<float>> fftBuf(N);
    
    size_t processed = 0;
    while (processed < frames) {
        // Determine how many samples to process in this micro-step
        size_t samplesUntilFFT = N - accIndex;
        size_t samplesRemaining = frames - processed;
        size_t chunk = std::min(samplesUntilFFT, samplesRemaining);
        
        // IO Loop: Read input, Write output
        for (size_t i = 0; i < chunk; ++i) {
            // Read input into acc
            acc[accIndex + i] = inout[processed + i];
            
            // Write output from OLA
            float val = ola[i];
            // Safety clamp
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;
            inout[processed + i] = val;
        }
        
        // Shift OLA
        // We consumed 'chunk' samples from OLA.
        if (chunk < N) {
            std::memmove(ola.data(), ola.data() + chunk, (N - chunk) * sizeof(float));
            std::fill(ola.begin() + (N - chunk), ola.end(), 0.0f);
        } else {
            std::fill(ola.begin(), ola.end(), 0.0f);
        }
        
        accIndex += chunk;
        processed += chunk;
        
        // Check FFT trigger
        if (accIndex == N) {
            // Apply window and copy to FFT buffer
            for (size_t i=0;i<N;i++) {
                fftBuf[i] = {acc[i] * window[i], 0.0f};
            }
            
            // Perform FFT
            FftUtil::fft(fftBuf);
            
            // Spectral Subtraction / Gain Calculation
            for (size_t k=0; k<N; k++) {
                float mag = std::abs(fftBuf[k]);
                float nEst = noise[k];
                
                // Noise estimation (simple min stats)
                if (mag < nEst) noise[k] = kNoiseAdapt * mag + (1.0f - kNoiseAdapt) * nEst;
                
                float s2 = mag*mag;
                float n2 = nEst*nEst;
                float h = s2 / (s2 + n2 + 1e-12f);
                float g = kFloorGain + (1.0f - kFloorGain) * h;
                
                // Apply gain
                fftBuf[k] *= g;
            }
            
            // Perform IFFT
            FftUtil::ifft(fftBuf);
            
            // Overlap-Add to OLA
            for (size_t i=0;i<N;i++) {
                float val = fftBuf[i].real() * window[i];
                // Add to OLA buffer
                ola[i] += val;
            }
            
            // Shift Acc
            // We keep the last (N - H) samples for the next overlap
            // e.g. if N=512, H=256. We keep last 256.
            size_t overlap = N - H;
            std::memmove(acc.data(), acc.data() + H, overlap * sizeof(float));
            accIndex = overlap;
        }
    }
}
