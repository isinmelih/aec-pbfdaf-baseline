#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "DeviceUtil.h"
#include "WasapiRunner.h"
static HWND gMic, gSpk, gDur, gSr, gOut, gFilter, gMu, gEps, gStart, gStop, gStatus;
static WasapiRunner runner;
static std::wstring GetText(HWND h) { int len = GetWindowTextLengthW(h); std::wstring s; s.resize(len); GetWindowTextW(h, &s[0], len+1); return s; }
static int ToInt(const std::wstring& s, int defv) { if (s.empty()) return defv; return (int)wcstol(s.c_str(), nullptr, 10); }
static double ToDouble(const std::wstring& s, double defv) { if (s.empty()) return defv; return wcstod(s.c_str(), nullptr); }
static void PopulateDevices(HWND hCombo, EDataFlow flow) {
    std::vector<DeviceInfo> list; ListDevices(flow, list);
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    for (size_t i=0;i<list.size();i++) SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)list[i].name.c_str());
    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}
static void UpdateStatus(const wchar_t* s) { SetWindowTextW(gStatus, s); }
static void StartRun(HWND hwnd) {
    RunnerParams p;
    p.micIndex = (int)SendMessageW(gMic, CB_GETCURSEL, 0, 0);
    p.spkIndex = (int)SendMessageW(gSpk, CB_GETCURSEL, 0, 0);
    p.durationMs = ToInt(GetText(gDur), 10000);
    p.sampleRate = ToInt(GetText(gSr), 0);
    p.outPath = GetText(gOut);
    p.filterLen = ToInt(GetText(gFilter), 0);
    p.mu = (float)ToDouble(GetText(gMu), -1.0);
    p.epsilon = (float)ToDouble(GetText(gEps), -1.0);
    if (runner.start(p)) UpdateStatus(L"Running"); else UpdateStatus(L"Already running");
}
static void StopRun() { runner.stop(); UpdateStatus(L"Stopped"); }
static HWND AddLabel(HWND parent, int x, int y, const wchar_t* t) { return CreateWindowW(L"STATIC", t, WS_CHILD|WS_VISIBLE, x, y, 160, 20, parent, nullptr, nullptr, nullptr); }
static HWND AddEdit(HWND parent, int x, int y, const wchar_t* def) { return CreateWindowW(L"EDIT", def, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, x, y, 160, 20, parent, nullptr, nullptr, nullptr); }
static HWND AddCombo(HWND parent, int x, int y) { return CreateWindowW(L"COMBOBOX", L"", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST, x, y, 220, 200, parent, nullptr, nullptr, nullptr); }
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        gMic = AddCombo(hwnd, 20, 20); gSpk = AddCombo(hwnd, 260, 20);
        PopulateDevices(gMic, eCapture); PopulateDevices(gSpk, eRender);
        AddLabel(hwnd, 20, 60, L"Duration(ms)"); gDur = AddEdit(hwnd, 140, 60, L"10000");
        AddLabel(hwnd, 20, 90, L"SampleRate(Hz)"); gSr = AddEdit(hwnd, 140, 90, L"0");
        AddLabel(hwnd, 20, 120, L"Output WAV"); gOut = AddEdit(hwnd, 140, 120, L"out_realtime.wav");
        AddLabel(hwnd, 20, 160, L"filterLen"); gFilter = AddEdit(hwnd, 140, 160, L"0");
        AddLabel(hwnd, 20, 190, L"mu"); gMu = AddEdit(hwnd, 140, 190, L"-1");
        AddLabel(hwnd, 20, 220, L"epsilon"); gEps = AddEdit(hwnd, 140, 220, L"-1");
        
        gStart = CreateWindowW(L"BUTTON", L"Start", WS_CHILD|WS_VISIBLE, 20, 260, 100, 30, hwnd, (HMENU)1, nullptr, nullptr);
        gStop = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD|WS_VISIBLE, 140, 260, 100, 30, hwnd, (HMENU)2, nullptr, nullptr);
        gStatus = AddLabel(hwnd, 260, 280, L"Stopped");
        return 0;
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) { StartRun(hwnd); return 0; }
        if (LOWORD(wParam) == 2) { StopRun(); return 0; }
    } else if (msg == WM_DESTROY) {
        runner.stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    INITCOMMONCONTROLSEX ic; ic.dwSize = sizeof(ic); ic.dwICC = ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"EchoAppWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"EchoAppWnd", L"Echo App", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 340, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow); UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CoUninitialize();
    return 0;
}
