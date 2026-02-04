#pragma once
#include <vector>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
struct DeviceInfo {
    std::wstring id;
    std::wstring name;
};
bool ListDevices(EDataFlow flow, std::vector<DeviceInfo>& list);
bool GetDeviceByIndex(EDataFlow flow, int index, IMMDevice** dev);
