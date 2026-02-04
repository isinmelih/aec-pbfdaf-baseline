#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Unknwn.h>
#include <audioapotypes.h>
#include <audioenginebaseapo.h>
#include <vector>
#include "../src/aec_core/AECProcessor.h"
#include "../src/aec_core/AIEnhancer.h"
#include "ApoParams.h"
typedef long long HRTIME;
struct APOInit { int reserved; };
struct APO_CONNECTION { void* pBuffer; UINT32 u32Frames; };
class EchoApo :
    public IAudioProcessingObject,
    public IAudioSystemEffects2
{
public:
    EchoApo();
    virtual ~EchoApo();
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD(GetLatency)(HRTIME* pLatency);
    STDMETHOD(IsInputFormatSupported)(IAudioMediaType* pOutputType, IAudioMediaType* pRequestedInputType, IAudioMediaType** ppSupportedInputType);
    STDMETHOD(IsOutputFormatSupported)(IAudioMediaType* pInputType, IAudioMediaType* pRequestedOutputType, IAudioMediaType** ppSupportedOutputType);
    STDMETHOD(GetInputChannelCount)(UINT32* pCount);
    STDMETHOD(GetOutputChannelCount)(UINT32* pCount);
    STDMETHOD(Initialize)(APOInit* pInit);
    STDMETHOD(Reset)();
    STDMETHOD(LockForProcess)(UINT32 u32NumInputConnections, APO_CONNECTION** ppInputConnections, UINT32 u32NumOutputConnections, APO_CONNECTION** ppOutputConnections);
    STDMETHOD(Process)(UINT32 u32NumInputConnections, APO_CONNECTION** ppInputConnections, UINT32 u32NumOutputConnections, APO_CONNECTION** ppOutputConnections);
    STDMETHOD(UnlockForProcess)();
    STDMETHOD(GetEffectsList)(GUID** ppEffectsIds, UINT32* pcEffects);
    STDMETHOD(SetEffectsParameters)(GUID* pEffectId, void* pParameters, UINT32 cbParameters);
    STDMETHOD(GetEffectsParameters)(GUID* pEffectId, void* pParameters, UINT32 cbParameters);
private:
    LONG refCount;
    UINT32 channels;
    UINT32 sampleRate;
    AECProcessor aec;
    AIEnhancer ai;
    bool aecInit;
    bool aiInit;
    std::vector<float> refBuf;
    std::vector<float> micBuf;
    std::vector<float> outBuf;
    ApoParams current;
    float dcPrevIn;
    float dcPrevOut;
    float dcAlpha;
};
class EchoApoClassFactory : public IClassFactory {
public:
    EchoApoClassFactory();
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD(CreateInstance)(IUnknown* pUnkOuter, REFIID riid, void** ppv);
    STDMETHOD(LockServer)(BOOL fLock);
private:
    LONG refCount;
};
extern "C" const CLSID CLSID_EchoApo;
