#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include "../aec_core/AECProcessor.h"
#include "../aec_core/AIEnhancer.h"
#include "DeviceUtil.h"
struct WavWriter {
    HANDLE h;
    DWORD dataBytes;
    int sr;
    int ch;
    WavWriter() : h(INVALID_HANDLE_VALUE), dataBytes(0), sr(0), ch(0) {}
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
};
static bool get_default_device(EDataFlow flow, IMMDevice** dev) {
    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) return false;
    hr = en->GetDefaultAudioEndpoint(flow, eConsole, dev);
    en->Release();
    return SUCCEEDED(hr);
}
static int watoi(const wchar_t* s) {
    return (int)wcstol(s, nullptr, 10);
}
int wmain(int argc, wchar_t** argv) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    int micIndex = -1;
    int spkIndex = -1;
    int durationMs = 10000;
    int sampleRateOverride = 0;
    std::wstring outPath = L"out_realtime.wav";
    int filterLenOverride = 0;
    float muOverride = -1.0f;
    float epsOverride = -1.0f;
    float alphaOverride = -1.0f;
    float betaOverride = -1.0f;
    int freezeOverride = -1;
    for (int i=1;i<argc;i++) {
        std::wstring a = argv[i];
        if (a == L"--list") {
            std::vector<DeviceInfo> caps, rends;
            ListDevices(eCapture, caps);
            ListDevices(eRender, rends);
            for (size_t k=0;k<caps.size();k++) wprintf(L"mic[%zu]: %s\n", k, caps[k].name.c_str());
            for (size_t k=0;k<rends.size();k++) wprintf(L"spk[%zu]: %s\n", k, rends[k].name.c_str());
            CoUninitialize();
            return 0;
        } else if (a == L"--mic" && i+1 < argc) {
            micIndex = watoi(argv[++i]);
        } else if (a == L"--spk" && i+1 < argc) {
            spkIndex = watoi(argv[++i]);
        } else if (a == L"--dur" && i+1 < argc) {
            durationMs = watoi(argv[++i]);
        } else if (a == L"--sr" && i+1 < argc) {
            sampleRateOverride = watoi(argv[++i]);
        } else if (a == L"--out" && i+1 < argc) {
            outPath = argv[++i];
        } else if (a == L"--filter" && i+1 < argc) {
            filterLenOverride = watoi(argv[++i]);
        } else if (a == L"--mu" && i+1 < argc) {
            muOverride = (float)wcstod(argv[++i], nullptr);
        } else if (a == L"--eps" && i+1 < argc) {
            epsOverride = (float)wcstod(argv[++i], nullptr);
        } else if (a == L"--alpha" && i+1 < argc) {
            alphaOverride = (float)wcstod(argv[++i], nullptr);
        } else if (a == L"--beta" && i+1 < argc) {
            betaOverride = (float)wcstod(argv[++i], nullptr);
        } else if (a == L"--freeze" && i+1 < argc) {
            freezeOverride = watoi(argv[++i]);
        }
    }
    IMMDevice* micDev=nullptr; IMMDevice* spkDev=nullptr;
    if (micIndex >= 0) {
        if (!GetDeviceByIndex(eCapture, micIndex, &micDev)) return 2;
    } else {
        if (!get_default_device(eCapture, &micDev)) return 2;
    }
    if (spkIndex >= 0) {
        if (!GetDeviceByIndex(eRender, spkIndex, &spkDev)) return 3;
    } else {
        if (!get_default_device(eRender, &spkDev)) return 3;
    }
    IAudioClient* micClient=nullptr; IAudioClient* loopClient=nullptr;
    hr = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&micClient);
    if (FAILED(hr)) return 4;
    hr = spkDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&loopClient);
    if (FAILED(hr)) return 5;
    WAVEFORMATEX* micFmt=nullptr; WAVEFORMATEX* spkFmt=nullptr;
    hr = micClient->GetMixFormat(&micFmt); if (FAILED(hr)) return 6;
    hr = loopClient->GetMixFormat(&spkFmt); if (FAILED(hr)) return 7;
    if (micFmt->wFormatTag != WAVE_FORMAT_IEEE_FLOAT || spkFmt->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) return 8;
    if (sampleRateOverride > 0) {
        micFmt->nSamplesPerSec = sampleRateOverride;
        spkFmt->nSamplesPerSec = sampleRateOverride;
        micFmt->nAvgBytesPerSec = sampleRateOverride * micFmt->nBlockAlign;
        spkFmt->nAvgBytesPerSec = sampleRateOverride * spkFmt->nBlockAlign;
    }
    REFERENCE_TIME dur = 10000000;
    hr = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, dur, 0, micFmt, nullptr);
    if (FAILED(hr)) return 11;
    hr = loopClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, dur, 0, spkFmt, nullptr);
    if (FAILED(hr)) return 12;
    IAudioCaptureClient* micCap=nullptr; IAudioCaptureClient* loopCap=nullptr;
    hr = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
    if (FAILED(hr)) return 13;
    hr = loopClient->GetService(__uuidof(IAudioCaptureClient), (void**)&loopCap);
    if (FAILED(hr)) return 14;
    hr = micClient->Start(); if (FAILED(hr)) return 15;
    hr = loopClient->Start(); if (FAILED(hr)) return 16;
    int sr = (int)micFmt->nSamplesPerSec;
    int chSrc = (int)micFmt->nChannels;
    int ch = 1;
    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    double ticksPerFrame = (double)qpcFreq.QuadPart / (double)sr;
    AECProcessor aec;
    AECParams p;
    p.sampleRate=sr; p.channels=ch; p.filterLen=2048; p.mu=0.1f; p.epsilon=1e-6f;
    p.leak = 0.0001f; p.maxDelayMs = 80; p.corrBlock = 1024; p.dtdAlpha = 2.0f; p.dtdBeta = 1.5f;
    if (filterLenOverride > 0) p.filterLen = filterLenOverride;
    if (muOverride >= 0.0f) p.mu = muOverride;
    if (epsOverride >= 0.0f) p.epsilon = epsOverride;
    aec.initialize(p);
    AIParams aip;
    aip.sampleRate = sr;
    aip.channels = ch;
    AIEnhancer ai; ai.initialize(aip);
    WavWriter ww; if (!ww.open(outPath.c_str(), sr, ch)) return 17;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", nullptr);
    UINT32 micFrameSize = micFmt->nBlockAlign;
    UINT32 spkFrameSize = spkFmt->nBlockAlign;
    std::vector<float> micBuf;
    std::vector<float> spkBuf;
    std::vector<float> outBuf;
    DWORD totalMs = (DWORD)durationMs;
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < totalMs) {
        UINT32 pkt=0;
        hr = micCap->GetNextPacketSize(&pkt);
        if (FAILED(hr)) break;
        if (pkt==0) { Sleep(1); continue; }
        BYTE* micData=nullptr; UINT32 micFrames=0; DWORD micFlags=0;
        UINT64 micPos=0, micQPC=0;
        hr = micCap->GetBuffer(&micData, &micFrames, &micFlags, &micPos, &micQPC);
        if (FAILED(hr)) break;
        UINT32 loopPkt=0; hr = loopCap->GetNextPacketSize(&loopPkt);
        BYTE* loopData=nullptr; UINT32 loopFrames=0; DWORD loopFlags=0;
        UINT64 loopPos=0, loopQPC=0;
        if (loopPkt>0) {
            hr = loopCap->GetBuffer(&loopData, &loopFrames, &loopFlags, &loopPos, &loopQPC);
            if (FAILED(hr)) { micCap->ReleaseBuffer(micFrames); break; }
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
        aec.process(micBuf.data(), spkBuf.data(), outBuf.data(), frames);
        ai.process(outBuf.data(), frames);
        ww.write(outBuf.data(), frames);
        micCap->ReleaseBuffer(micFrames);
        if (loopPkt>0) loopCap->ReleaseBuffer(loopFrames);
    }
    
    // Print Metrics
    AECStats s = aec.getStats();
    wprintf(L"\n--- AEC Performance Metrics ---\n");
    wprintf(L"Avg ERLE: %.2f dB\n", s.avgErle);
    wprintf(L"Max ERLE: %.2f dB\n", s.maxErle);
    wprintf(L"Convergence Time: %.2f ms\n", s.convergedTimeMs);
    wprintf(L"-----------------------------\n");

    ww.close();
    if (hTask) AvRevertMmThreadCharacteristics(hTask);
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
