#define NOMINMAX 1
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "../aec_core/AECProcessor.h"
static bool read_wav(const std::wstring& path, std::vector<float>& data, int& sr, int& ch) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[4]; f.read(riff, 4);
    if (std::string(riff,4)!="RIFF") return false;
    uint32_t riffsz; f.read(reinterpret_cast<char*>(&riffsz),4);
    char wave[4]; f.read(wave,4);
    if (std::string(wave,4)!="WAVE") return false;
    bool fmtok=false, dataok=false;
    uint16_t audiofmt=0, bps=0, blockalign=0;
    uint32_t samplerate=0, byteRate=0;
    uint16_t channels=0;
    std::vector<char> pcm;
    while (f) {
        char id[4]; uint32_t sz=0;
        f.read(id,4); if (!f) break;
        f.read(reinterpret_cast<char*>(&sz),4); if (!f) break;
        std::string sid(id,4);
        if (sid=="fmt ") {
            fmtok=true;
            f.read(reinterpret_cast<char*>(&audiofmt),2);
            f.read(reinterpret_cast<char*>(&channels),2);
            f.read(reinterpret_cast<char*>(&samplerate),4);
            f.read(reinterpret_cast<char*>(&byteRate),4);
            f.read(reinterpret_cast<char*>(&blockalign),2);
            f.read(reinterpret_cast<char*>(&bps),2);
            if (sz>16) f.seekg(sz-16, std::ios::cur);
        } else if (sid=="data") {
            dataok=true;
            pcm.resize(sz);
            f.read(pcm.data(), sz);
        } else {
            f.seekg(sz, std::ios::cur);
        }
    }
    if (!fmtok || !dataok) return false;
    if (audiofmt!=1 || bps!=16) return false;
    sr = static_cast<int>(samplerate);
    ch = static_cast<int>(channels);
    size_t samples = pcm.size()/2;
    data.resize(samples);
    const int16_t* p = reinterpret_cast<const int16_t*>(pcm.data());
    for (size_t i=0;i<samples;i++) data[i] = p[i] / 32768.0f;
    return true;
}
static bool write_wav(const std::wstring& path, const std::vector<float>& data, int sr, int ch) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t datasz = static_cast<uint32_t>(data.size()*2);
    uint32_t riffsz = 36 + datasz;
    f.write("RIFF",4);
    f.write(reinterpret_cast<const char*>(&riffsz),4);
    f.write("WAVE",4);
    f.write("fmt ",4);
    uint32_t fmtsz=16; f.write(reinterpret_cast<const char*>(&fmtsz),4);
    uint16_t audiofmt=1; f.write(reinterpret_cast<const char*>(&audiofmt),2);
    uint16_t channels=static_cast<uint16_t>(ch); f.write(reinterpret_cast<const char*>(&channels),2);
    uint32_t samplerate=sr; f.write(reinterpret_cast<const char*>(&samplerate),4);
    uint16_t bps=16;
    uint16_t blockalign=channels*(bps/8);
    uint32_t byterate=samplerate*blockalign;
    f.write(reinterpret_cast<const char*>(&byterate),4);
    f.write(reinterpret_cast<const char*>(&blockalign),2);
    f.write(reinterpret_cast<const char*>(&bps),2);
    f.write("data",4);
    f.write(reinterpret_cast<const char*>(&datasz),4);
    for (size_t i=0;i<data.size();i++) {
        float v = std::max(-1.0f, std::min(1.0f, data[i]));
        int16_t s = static_cast<int16_t>(std::lround(v*32767.0f));
        f.write(reinterpret_cast<const char*>(&s),2);
    }
    return true;
}
int wmain(int argc, wchar_t** argv) {
    if (argc<4) {
        fwprintf(stderr, L"Usage: echocancel mic.wav ref.wav out.wav\n");
        return 1;
    }
    std::vector<float> mic;
    std::vector<float> ref;
    int sr1=0,ch1=0,sr2=0,ch2=0;
    if (!read_wav(argv[1], mic, sr1, ch1)) {
        fwprintf(stderr, L"Mic read error\n");
        return 2;
    }
    if (!read_wav(argv[2], ref, sr2, ch2)) {
        fwprintf(stderr, L"Ref read error\n");
        return 3;
    }
    if (sr1!=sr2 || ch1!=ch2) {
        fwprintf(stderr, L"Format mismatch\n");
        return 4;
    }
    size_t frames = std::min(mic.size(), ref.size());
    mic.resize(frames);
    ref.resize(frames);
    std::vector<float> out(frames);
    AECProcessor aec;
    AECParams p;
    p.sampleRate=sr1;
    p.channels=ch1;
    p.filterLen=1024;
    p.mu=0.2f;
    p.epsilon=1e-6f;
    p.leak=0.0001f; p.maxDelayMs=80; p.corrBlock=1024; p.dtdAlpha=2.0f; p.dtdBeta=1.5f;
    aec.initialize(p);
    aec.process(mic.data(), ref.data(), out.data(), frames);
    if (!write_wav(argv[3], out, sr1, ch1)) {
        fwprintf(stderr, L"Write error\n");
        return 5;
    }
    return 0;
}
