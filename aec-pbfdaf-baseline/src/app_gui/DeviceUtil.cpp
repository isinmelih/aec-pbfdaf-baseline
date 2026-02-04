#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <string>
#include <vector>
#include "./DeviceUtil.h"
bool ListDevices(EDataFlow flow, std::vector<DeviceInfo>& list) {
    list.clear();
    IMMDeviceEnumerator* en=nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) return false;
    IMMDeviceCollection* col=nullptr;
    hr = en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col);
    if (FAILED(hr)) { en->Release(); return false; }
    UINT count=0; col->GetCount(&count);
    for (UINT i=0;i<count;i++) {
        IMMDevice* dev=nullptr;
        hr = col->Item(i, &dev);
        if (FAILED(hr)) continue;
        LPWSTR sid=nullptr;
        dev->GetId(&sid);
        IPropertyStore* ps=nullptr;
        hr = dev->OpenPropertyStore(STGM_READ, &ps);
        std::wstring name=L"";
        if (SUCCEEDED(hr)) {
            PROPVARIANT pv; PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv))) {
                if (pv.vt == VT_LPWSTR && pv.pwszVal) name = pv.pwszVal;
            }
            PropVariantClear(&pv);
            ps->Release();
        }
        DeviceInfo di;
        di.id = sid ? sid : L"";
        di.name = name;
        list.push_back(di);
        if (sid) CoTaskMemFree(sid);
        dev->Release();
    }
    col->Release();
    en->Release();
    return true;
}
bool GetDeviceByIndex(EDataFlow flow, int index, IMMDevice** dev) {
    *dev=nullptr;
    IMMDeviceEnumerator* en=nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) return false;
    IMMDeviceCollection* col=nullptr;
    hr = en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col);
    if (FAILED(hr)) { en->Release(); return false; }
    UINT count=0; col->GetCount(&count);
    if (index<0 || (UINT)index>=count) { col->Release(); en->Release(); return false; }
    hr = col->Item(index, dev);
    col->Release();
    en->Release();
    return SUCCEEDED(hr);
}
