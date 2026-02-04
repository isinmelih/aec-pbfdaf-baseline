#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "./WasapiRunner.h"
#include "./DeviceUtil.h"
#include "../aec_core/AIEnhancer.h"
#include <vector>
#include <cmath>
class WavWriter2 {
public:
    WavWriter2() : h(INVALID_HANDLE_VALUE), dataBytes(0), sr(0), ch(0) {}
    bool open(const wchar_t* path, int sampleRate, int channels) {
        h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        sr = sampleRate; ch = channels; dataBytes = 0;
        DWORD dw;
        DWORD riffsz = 0;
        WriteFile(h, "RIFF", 4, &dw, nullptr);
        WriteFile(h, &riffsz, 4, &dw, nullptr);
        WriteFile(h, "WAVE", 4, &dw, nullptr);
        WriteFile(h, "fmt ", 4, &dw, nullptr);
        DWORD fmtsz = 16; WriteFile(h, &fmtsz, 4, &dw, nullptr);
        WORD audiofmt = 1; WriteFile(h, &audiofmt, 2, &dw, nullptr);
        WORD outChannels = (WORD)ch; WriteFile(h, &outChannels, 2, &dw, nullptr);
        DWORD samplerate = (DWORD)sr; WriteFile(h, &samplerate, 4, &dw, nullptr);
        WORD bps = 16;
        WORD blockalign = outChannels*(bps/8);
        DWORD byterate = samplerate*blockalign;
        WriteFile(h, &byterate, 4, &dw, nullptr);
        WriteFile(h, &blockalign, 2, &dw, nullptr);
        WriteFile(h, &bps, 2, &dw, nullptr);
        WriteFile(h, "data", 4, &dw, nullptr);
        DWORD datasz = 0; WriteFile(h, &datasz, 4, &dw, nullptr);
        return true;
    }
    void write(const float* data, size_t frames) {
        std::vector<short> pcm(frames*ch);
        for (size_t i=0;i<frames*ch;i++) {
            float v = data[i];
            if (v>1.0f) v=1.0f; if (v<-1.0f) v=-1.0f;
            pcm[i] = (short)std::lround(v*32767.0f);
        }
        DWORD dw;
        WriteFile(h, pcm.data(), (DWORD)(pcm.size()*2), &dw, nullptr);
        dataBytes += dw;
    }
    void close() {
        if (h==INVALID_HANDLE_VALUE) return;
        DWORD dw;
        LARGE_INTEGER pos; pos.QuadPart = 4;
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        DWORD riffsz = 36 + dataBytes;
        WriteFile(h, &riffsz, 4, &dw, nullptr);
        pos.QuadPart = 40;
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        WriteFile(h, &dataBytes, 4, &dw, nullptr);
        CloseHandle(h); h = INVALID_HANDLE_VALUE;
    }
private:
    HANDLE h;
    DWORD dataBytes;
    int sr;
    int ch;
};
static DWORD WINAPI RunnerThread(LPVOID lp) {
    WasapiRunner* self = (WasapiRunner*)lp;
    RunnerParams p = self->getParams();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDevice* micDev=nullptr; IMMDevice* spkDev=nullptr;
    if (p.micIndex >= 0) {
        if (!GetDeviceByIndex(eCapture, p.micIndex, &micDev)) { CoUninitialize(); return 1; }
    } else {
        if (!GetDeviceByIndex(eCapture, 0, &micDev)) { CoUninitialize(); return 2; }
    }
    if (p.spkIndex >= 0) {
        if (!GetDeviceByIndex(eRender, p.spkIndex, &spkDev)) { micDev->Release(); CoUninitialize(); return 3; }
    } else {
        if (!GetDeviceByIndex(eRender, 0, &spkDev)) { micDev->Release(); CoUninitialize(); return 4; }
    }
    IAudioClient* micClient=nullptr; IAudioClient* loopClient=nullptr;
    micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&micClient);
    spkDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&loopClient);
    WAVEFORMATEX* micFmt=nullptr; WAVEFORMATEX* spkFmt=nullptr;
    micClient->GetMixFormat(&micFmt);
    loopClient->GetMixFormat(&spkFmt);
    if (p.sampleRate > 0) {
        micFmt->nSamplesPerSec = p.sampleRate;
        spkFmt->nSamplesPerSec = p.sampleRate;
        micFmt->nAvgBytesPerSec = micFmt->nSamplesPerSec * micFmt->nBlockAlign;
        spkFmt->nAvgBytesPerSec = spkFmt->nSamplesPerSec * spkFmt->nBlockAlign;
    }
    REFERENCE_TIME dur = 10000000;
    micClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, dur, 0, micFmt, nullptr);
    loopClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, dur, 0, spkFmt, nullptr);
    IAudioCaptureClient* micCap=nullptr; IAudioCaptureClient* loopCap=nullptr;
    micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
    loopClient->GetService(__uuidof(IAudioCaptureClient), (void**)&loopCap);
    micClient->Start();
    loopClient->Start();
    int sr = (int)micFmt->nSamplesPerSec;
    int chSrc = (int)micFmt->nChannels;
    int ch = 1;
    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    double ticksPerFrame = (double)qpcFreq.QuadPart / (double)sr;
    // AECProcessor aec; // Use member
    AECParams ap;
    ap.sampleRate=sr; ap.channels=ch; ap.filterLen=p.filterLen>0?p.filterLen:2048; ap.mu=p.mu>=0?p.mu:0.1f; ap.epsilon=p.epsilon>=0?p.epsilon:1e-6f;
    ap.leak = 0.0001f; ap.maxDelayMs = 80; ap.corrBlock = 1024; 
    ap.dtdAlpha = p.dtdAlpha > 0.0f ? p.dtdAlpha : 2.0f;
    ap.dtdBeta = p.dtdBeta > 0.0f ? p.dtdBeta : 1.5f;
    
    self->getProcessor()->initialize(ap);
    if (p.freezeBlocks > 0) {
        self->getProcessor()->setFreezeBlocks(p.freezeBlocks);
    }

    AIParams aip;
    aip.sampleRate = sr; aip.channels = ch;
    AIEnhancer ai; ai.initialize(aip);
    WavWriter2 ww; ww.open(p.outPath.c_str(), sr, ch);
    std::vector<float> micBuf, spkBuf, outBuf;
    DWORD totalMs = (DWORD)(p.durationMs>0?p.durationMs:10000);
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < totalMs && self->isRunning()) {
        UINT32 pkt=0;
        micCap->GetNextPacketSize(&pkt);
        if (pkt==0) { Sleep(1); continue; }
        BYTE* micData=nullptr; UINT32 micFrames=0; DWORD micFlags=0;
        UINT64 micPos=0, micQPC=0;
        micCap->GetBuffer(&micData, &micFrames, &micFlags, &micPos, &micQPC);
        UINT32 loopPkt=0; loopCap->GetNextPacketSize(&loopPkt);
        BYTE* loopData=nullptr; UINT32 loopFrames=0; DWORD loopFlags=0;
        UINT64 loopPos=0, loopQPC=0;
        if (loopPkt>0) {
            loopCap->GetBuffer(&loopData, &loopFrames, &loopFlags, &loopPos, &loopQPC);
        } else {
            loopFrames = micFrames;
        }
        UINT32 micOff = 0, loopOff = 0;
        if (loopPkt>0) {
            LONGLONG delta = (LONGLONG)loopQPC - (LONGLONG)micQPC;
            int shift = (int)llround((double)delta / ticksPerFrame);
            if (shift > 0) {
                if ((UINT32)shift > loopFrames) shift = (int)loopFrames;
                loopOff = (UINT32)shift;
            } else if (shift < 0) {
                shift = -shift;
                if ((UINT32)shift > micFrames) shift = (int)micFrames;
                micOff = (UINT32)shift;
            }
        }
        UINT32 framesMic = micFrames - micOff;
        UINT32 framesLoop = loopFrames - loopOff;
        UINT32 frames = framesMic < framesLoop ? framesMic : framesLoop;
        micBuf.resize(frames);
        spkBuf.resize(frames);
        outBuf.resize(frames);
        const float* micF = (const float*)micData;
        const float* loopF = loopPkt>0 ? (const float*)loopData : micF;
        for (UINT32 i=0;i<frames;i++) {
            float sm=0.0f, sl=0.0f;
            for (int c=0;c<chSrc;c++) {
                sm += micF[(i+micOff)*chSrc + c];
                sl += loopF[(i+loopOff)*chSrc + c];
            }
            micBuf[i] = sm / (float)chSrc;
            spkBuf[i] = sl / (float)chSrc;
        }
        self->getProcessor()->process(micBuf.data(), spkBuf.data(), outBuf.data(), frames);
        ai.process(outBuf.data(), frames);
        ww.write(outBuf.data(), frames);
        micCap->ReleaseBuffer(micFrames);
        if (loopPkt>0) loopCap->ReleaseBuffer(loopFrames);
    }
    ww.close();
    micClient->Stop();
    loopClient->Stop();
    micCap->Release();
    loopCap->Release();
    CoTaskMemFree(micFmt);
    CoTaskMemFree(spkFmt);
    micClient->Release();
    loopClient->Release();
    micDev->Release();
    spkDev->Release();
    CoUninitialize();
    return 0;
}
WasapiRunner::WasapiRunner() : hThread(nullptr), running(false) {}
bool WasapiRunner::start(const RunnerParams& p) {
    if (running) return false;
    params = p;
    running = true;
    hThread = CreateThread(nullptr, 0, RunnerThread, this, 0, nullptr);
    return hThread != nullptr;
}
void WasapiRunner::stop() {
    running = false;
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        hThread = nullptr;
    }
}
bool WasapiRunner::isRunning() const { return running; }
RunnerParams WasapiRunner::getParams() const { return params; }
