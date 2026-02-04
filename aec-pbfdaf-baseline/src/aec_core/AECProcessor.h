#pragma once
// This implementation is intended as a real-time Partitioned-Block Frequency-Domain Adaptive Filter (PBFDAF) AEC baseline. 
// Focused on correctness, low-delay and measurability. No non-linear post-processing.
// Real-time capable, lock-free statistics, and thread-safe parameter tuning.

#include <vector>
#include <cstdint>
#include <cstddef>
#include <complex>
#include <atomic>
// #include <mutex> // Removed mutex for lock-free design

struct AECParams {
    int sampleRate;
    int channels;
    int filterLen;
    float mu;
    float epsilon;
    float leak;
    int maxDelayMs;
    int corrBlock;
    float dtdAlpha;
    float dtdBeta;
};

struct AECStats {
    float erle;
    float micE;
    float refE;
    float errE;
    bool dtd;
    float coherence;
    int currentLag;
    float currentLagMs;
    bool freeze;
    float mu;
    bool dtdFreezeActive;
    bool delayFreezeActive;
    int delayUpdateCount;
    float lastDelayChangeTime; // Placeholder for now
    
    // Measurements
    float instantErle;
    float maxErle;
    float avgErle;
    float convergedTimeMs;
};

class AECProcessor {
public:
    AECProcessor();
    void initialize(const AECParams& p);
    void process(const float* mic, const float* ref, float* out, size_t frames);
    
    // Thread-safe stats getter
    AECStats getStats() const;

    // Runtime parameter setters (thread-safe)
    void setMu(float val);
    void setMuRange(float min, float max);
    void setDtdParams(float alpha, float beta);
    void setFreezeBlocks(int blocks);

private:
    void processTimeDomain(const float* mic, const float* ref, float* out, size_t frames);
    void processFrequencyDomain(const float* mic, const float* ref, float* out, size_t frames);
    void performBlockFdaf();
    void updateDelay();

    AECParams params;
    
    // --- Time Domain Legacy ---
    std::vector<float> w;
    std::vector<float> x;
    size_t xIndex;
    float xPowerSum;
    // --------------------------

    // --- Common Delay / Buffer ---
    std::vector<float> refDelay;
    std::vector<float> refFeed;
    std::vector<float> micDelay;
    size_t delayIdx;
    int currentLag;
    int maxLag;
    int lastLag;
    // -----------------------------

    // --- Stats / Runtime ---
    float instantErle;
    float maxErle;
    float avgErle;
    float convergedTimeMs;
    float lastDelayChangeTime;

    int blockSize;
    int blockCount;
    float micPowerSum;
    float refPowerSum;
    float errPowerSum;
    float yPowerSum;
    bool freeze;
    
    // Dynamic params
    float muDynamic;
    float muMin;
    float muMax;
    float iirAlpha;
    float micE;
    float refE;
    float errE;
    
    int freezeBlocks;
    int dtdFreezeSamples;
    int delayFreezeSamples;
    int delayUpdateCounter;
    int totalBlocks;
    
    // FDAF members (kept for struct size consistency if needed, but unused in time-domain mode mostly)
    int fdafM;
    int fdafN;
    int numPartitions;
    int constraintIdx;
    std::vector<float> fdafMicBuf;
    std::vector<float> fdafRefBuf;
    size_t fdafBufIdx;
    std::vector<std::vector<std::complex<float>>> X_freq;
    std::vector<std::vector<std::complex<float>>> W_freq;
    std::vector<std::complex<float>> E_freq;
    std::vector<std::complex<float>> Y_freq;
    std::vector<std::complex<float>> fftScratch;
    std::vector<float> olaBuffer;
    std::vector<float> powerSpectralDensity;
    std::vector<float> psd_ref;
    std::vector<float> psd_mic;
    std::vector<std::complex<float>> psd_cross;
    float coherence;
    float farEndEnergy;
    std::vector<float> outputFifo;
    size_t outFifoRead;
    size_t outFifoWrite;
    size_t outFifoCount;
    std::vector<float> micPrev;

    // Atomics
    std::atomic<uint32_t> statsSeq;
    mutable AECStats statsBuf;
    std::atomic<float> atomicMu;
    std::atomic<float> atomicMuMin;
    std::atomic<float> atomicMuMax;
    std::atomic<float> atomicDtdAlpha;
    std::atomic<float> atomicDtdBeta;
    std::atomic<int> atomicFreezeBlocks;
};
