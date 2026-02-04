#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "DeviceUtil.h"
static HWND hMic, hSpk, hDur, hSr, hOut, hFilter, hMu, hEps, hAlpha, hBeta, hFreeze, hRun;
static std::wstring GetText(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring s; s.resize(len);
    GetWindowTextW(h, &s[0], len+1);
    return s;
}
static int ToInt(const std::wstring& s, int defv) {
    if (s.empty()) return defv;
    return (int)wcstol(s.c_str(), nullptr, 10);
}
static double ToDouble(const std::wstring& s, double defv) {
    if (s.empty()) return defv;
    return wcstod(s.c_str(), nullptr);
}
static void PopulateDevices(HWND hCombo, EDataFlow flow) {
    std::vector<DeviceInfo> list;
    ListDevices(flow, list);
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    for (size_t i=0;i<list.size();i++) {
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)list[i].name.c_str());
    }
    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}
static void RunProcess(HWND hwnd) {
    int mic = (int)SendMessageW(hMic, CB_GETCURSEL, 0, 0);
    int spk = (int)SendMessageW(hSpk, CB_GETCURSEL, 0, 0);
    int dur = ToInt(GetText(hDur), 10000);
    int sr = ToInt(GetText(hSr), 0);
    std::wstring out = GetText(hOut);
    int filter = ToInt(GetText(hFilter), 0);
    double mu = ToDouble(GetText(hMu), -1.0);
    double eps = ToDouble(GetText(hEps), -1.0);
    double alpha = ToDouble(GetText(hAlpha), -1.0);
    double beta = ToDouble(GetText(hBeta), -1.0);
    int freeze = ToInt(GetText(hFreeze), -1);

    std::wstring cmd = L"wasapi_aec.exe";
    if (mic >= 0) { cmd += L" --mic "; cmd += std::to_wstring(mic); }
    if (spk >= 0) { cmd += L" --spk "; cmd += std::to_wstring(spk); }
    cmd += L" --dur "; cmd += std::to_wstring(dur);
    if (sr > 0) { cmd += L" --sr "; cmd += std::to_wstring(sr); }
    if (!out.empty()) { cmd += L" --out \""; cmd += out; cmd += L"\""; }
    if (filter > 0) { cmd += L" --filter "; cmd += std::to_wstring(filter); }
    if (mu >= 0.0) { cmd += L" --mu "; cmd += std::to_wstring(mu); }
    if (eps >= 0.0) { cmd += L" --eps "; cmd += std::to_wstring(eps); }
    if (alpha > 0.0) { cmd += L" --alpha "; cmd += std::to_wstring(alpha); }
    if (beta > 0.0) { cmd += L" --beta "; cmd += std::to_wstring(beta); }
    if (freeze >= 0) { cmd += L" --freeze "; cmd += std::to_wstring(freeze); }

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cmdline = cmd;
    if (CreateProcessW(nullptr, &cmdline[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
static HWND AddLabel(HWND parent, int x, int y, const wchar_t* t) {
    return CreateWindowW(L"STATIC", t, WS_CHILD|WS_VISIBLE, x, y, 160, 20, parent, nullptr, nullptr, nullptr);
}
static HWND AddEdit(HWND parent, int x, int y, const wchar_t* def) {
    HWND h = CreateWindowW(L"EDIT", def, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, x, y, 160, 20, parent, nullptr, nullptr, nullptr);
    return h;
}
static HWND AddCombo(HWND parent, int x, int y) {
    HWND h = CreateWindowW(L"COMBOBOX", L"", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST, x, y, 220, 200, parent, nullptr, nullptr, nullptr);
    return h;
}
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        hMic = AddCombo(hwnd, 20, 20);
        hSpk = AddCombo(hwnd, 260, 20);
        PopulateDevices(hMic, eCapture);
        PopulateDevices(hSpk, eRender);
        AddLabel(hwnd, 20, 60, L"Duration(ms)");
        hDur = AddEdit(hwnd, 140, 60, L"10000");
        AddLabel(hwnd, 20, 90, L"SampleRate(Hz)");
        hSr = AddEdit(hwnd, 140, 90, L"0");
        AddLabel(hwnd, 20, 120, L"Output WAV");
        hOut = AddEdit(hwnd, 140, 120, L"out_realtime.wav");
        AddLabel(hwnd, 20, 160, L"filterLen");
        hFilter = AddEdit(hwnd, 140, 160, L"0");
        AddLabel(hwnd, 20, 190, L"mu");
        hMu = AddEdit(hwnd, 140, 190, L"-1");
        AddLabel(hwnd, 20, 220, L"epsilon");
        hEps = AddEdit(hwnd, 140, 220, L"-1");
        
        // DTD Params
        AddLabel(hwnd, 320, 160, L"DTD Alpha");
        hAlpha = AddEdit(hwnd, 420, 160, L"-1");
        AddLabel(hwnd, 320, 190, L"DTD Beta");
        hBeta = AddEdit(hwnd, 420, 190, L"-1");
        AddLabel(hwnd, 320, 220, L"FreezeBlocks");
        hFreeze = AddEdit(hwnd, 420, 220, L"-1");

        hRun = CreateWindowW(L"BUTTON", L"Run", WS_CHILD|WS_VISIBLE, 20, 260, 100, 30, hwnd, (HMENU)1, nullptr, nullptr);
        return 0;
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {
            RunProcess(hwnd);
            return 0;
        }
    } else if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    INITCOMMONCONTROLSEX ic; ic.dwSize = sizeof(ic); ic.dwICC = ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AECControlPanel";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"AECControlPanel", L"AEC Control Panel", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 340, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
