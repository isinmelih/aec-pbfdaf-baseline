#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <string>
#include "../aec_core/AECProcessor.h"

struct RunnerParams {
    int micIndex;
    int spkIndex;
    int durationMs;
    int sampleRate;
    std::wstring outPath;
    int filterLen;
    float mu;
    float epsilon;
    float dtdAlpha;
    float dtdBeta;
    int freezeBlocks;
};
class WasapiRunner {
public:
    WasapiRunner();
    bool start(const RunnerParams& p);
    void stop();
    bool isRunning() const;
    RunnerParams getParams() const;
    AECProcessor* getProcessor() { return &aec; }
private:
    HANDLE hThread;
    RunnerParams params;
    volatile bool running;
    AECProcessor aec;
};
