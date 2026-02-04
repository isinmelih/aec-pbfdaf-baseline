#include "EchoApo.h"
static const GUID CLSID_EchoApo_Value = { 0x5f6c2a6e, 0x6b09, 0x4b1a,{0x9a,0x57,0x91,0x2c,0x7f,0x0a,0x0b,0x11} };
extern "C" const CLSID CLSID_EchoApo = CLSID_EchoApo_Value;
EchoApo::EchoApo() : refCount(1), channels(1) {}
EchoApo::~EchoApo() {}
ULONG EchoApo::AddRef() { return InterlockedIncrement(&refCount); }
ULONG EchoApo::Release() { ULONG r = InterlockedDecrement(&refCount); if (r==0) delete this; return r; }
HRESULT EchoApo::QueryInterface(REFIID riid, void** ppv) {
    if (ppv==nullptr) return E_POINTER;
    if (riid == __uuidof(IUnknown)) { *ppv = static_cast<IAudioProcessingObject*>(this); AddRef(); return S_OK; }
    if (riid == __uuidof(IAudioProcessingObject)) { *ppv = static_cast<IAudioProcessingObject*>(this); AddRef(); return S_OK; }
    if (riid == __uuidof(IAudioSystemEffects2)) { *ppv = static_cast<IAudioSystemEffects2*>(this); AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
HRESULT EchoApo::GetLatency(HRTIME* pLatency) { if (!pLatency) return E_POINTER; *pLatency = 0; return S_OK; }
HRESULT EchoApo::IsInputFormatSupported(IAudioMediaType* pOutputType, IAudioMediaType* pRequestedInputType, IAudioMediaType** ppSupportedInputType) { if (ppSupportedInputType) *ppSupportedInputType = pRequestedInputType; return S_OK; }
HRESULT EchoApo::IsOutputFormatSupported(IAudioMediaType* pInputType, IAudioMediaType* pRequestedOutputType, IAudioMediaType** ppSupportedOutputType) { if (ppSupportedOutputType) *ppSupportedOutputType = pRequestedOutputType; return S_OK; }
HRESULT EchoApo::GetInputChannelCount(UINT32* pCount) { if (!pCount) return E_POINTER; *pCount = channels; return S_OK; }
HRESULT EchoApo::GetOutputChannelCount(UINT32* pCount) { if (!pCount) return E_POINTER; *pCount = channels; return S_OK; }
HRESULT EchoApo::Initialize(APOInit* pInit) {
    channels = 1;
    sampleRate = 48000;
    current.filterLen = 1024;
    current.mu = 0.1f;
    current.epsilon = 1e-6f;
    AECParams ap; ap.sampleRate = (int)sampleRate; ap.channels = (int)channels; ap.filterLen = current.filterLen; ap.mu = current.mu; ap.epsilon = current.epsilon; ap.leak=0.0001f; ap.maxDelayMs=80; ap.corrBlock=1024; ap.dtdAlpha=2.0f; ap.dtdBeta=1.5f;
    aec.initialize(ap);
    aecInit = true;
    AIParams ip; ip.sampleRate = (int)sampleRate; ip.channels = (int)channels;
    ai.initialize(ip);
    aiInit = true;
    dcPrevIn = 0.0f; dcPrevOut = 0.0f; dcAlpha = 0.995f;
    return S_OK;
}
HRESULT EchoApo::Reset() { return S_OK; }
HRESULT EchoApo::LockForProcess(UINT32 u32NumInputConnections, APO_CONNECTION** ppInputConnections, UINT32 u32NumOutputConnections, APO_CONNECTION** ppOutputConnections) {
    refBuf.clear(); micBuf.clear(); outBuf.clear();
    return S_OK;
}
HRESULT EchoApo::Process(UINT32 u32NumInputConnections, APO_CONNECTION** ppInputConnections, UINT32 u32NumOutputConnections, APO_CONNECTION** ppOutputConnections) {
    if (u32NumInputConnections<1 || u32NumOutputConnections<1) return E_FAIL;
    APO_CONNECTION* in = ppInputConnections[0];
    APO_CONNECTION* out = ppOutputConnections[0];
    if (!in || !out) return E_FAIL;
    float* pi = (float*)in->pBuffer;
    float* po = (float*)out->pBuffer;
    UINT32 frames = in->u32Frames;
    micBuf.resize(frames);
    refBuf.resize(frames);
    outBuf.resize(frames);
    for (UINT32 i=0;i<frames;i++) {
        float x = pi[i];
        float y = x - dcPrevIn + dcAlpha * dcPrevOut;
        dcPrevIn = x;
        dcPrevOut = y;
        micBuf[i] = y;
        refBuf[i] = 0.0f;
    }
    if (aecInit) aec.process(micBuf.data(), refBuf.data(), outBuf.data(), frames);
    else outBuf = micBuf;
    if (aiInit) ai.process(outBuf.data(), frames);
    for (UINT32 i=0;i<frames;i++) {
        float v = outBuf[i];
        if (v > 0.99f) v = 0.99f;
        if (v < -0.99f) v = -0.99f;
        po[i] = v;
    }
    return S_OK;
}
HRESULT EchoApo::UnlockForProcess() { return S_OK; }
HRESULT EchoApo::SetEffectsParameters(GUID* pEffectId, void* pParameters, UINT32 cbParameters) {
    if (!pParameters || cbParameters < sizeof(ApoParams)) return E_INVALIDARG;
    ApoParams* pr = reinterpret_cast<ApoParams*>(pParameters);
    current = *pr;
    AECParams ap; ap.sampleRate = (int)sampleRate; ap.channels = (int)channels; ap.filterLen = current.filterLen; ap.mu = current.mu; ap.epsilon = current.epsilon; ap.leak=0.0001f; ap.maxDelayMs=80; ap.corrBlock=1024; ap.dtdAlpha=2.0f; ap.dtdBeta=1.5f;
    aec.initialize(ap);
    AIParams ip; ip.sampleRate = (int)sampleRate; ip.channels = (int)channels;
    ai.initialize(ip);
    return S_OK;
}
HRESULT EchoApo::GetEffectsParameters(GUID* pEffectId, void* pParameters, UINT32 cbParameters) {
    if (!pParameters || cbParameters < sizeof(ApoParams)) return E_INVALIDARG;
    ApoParams* pr = reinterpret_cast<ApoParams*>(pParameters);
    *pr = current;
    return S_OK;
}
HRESULT EchoApo::GetEffectsList(GUID** ppEffectsIds, UINT32* pcEffects) { if (ppEffectsIds) *ppEffectsIds = nullptr; if (pcEffects) *pcEffects = 0; return S_OK; }
EchoApoClassFactory::EchoApoClassFactory() : refCount(1) {}
ULONG EchoApoClassFactory::AddRef() { return InterlockedIncrement(&refCount); }
ULONG EchoApoClassFactory::Release() { ULONG r = InterlockedDecrement(&refCount); if (r==0) delete this; return r; }
HRESULT EchoApoClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (ppv==nullptr) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
HRESULT EchoApoClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (ppv==nullptr) return E_POINTER;
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}
HRESULT EchoApoClassFactory::LockServer(BOOL fLock) { return S_OK; }
STDAPI DllCanUnloadNow() { return S_FALSE; }
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid == CLSID_EchoApo) {
        EchoApoClassFactory* f = new EchoApoClassFactory();
        HRESULT hr = f->QueryInterface(riid, ppv);
        f->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
