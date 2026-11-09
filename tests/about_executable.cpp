#include "uia_client_helpers.hpp"
#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    auto directory = std::filesystem::temp_directory_path() / (L"lcars-about-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --settings-dir \"" + directory.wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    bool ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    Search search{}; IUIAutomation* uia = nullptr; IUIAutomationElement* root = nullptr;
    if (ok) {
        CloseHandle(pi.hThread); search.pid = pi.dwProcessId;
        for (int i = 0; i < 100 && !search.window; ++i) { EnumWindows(find_window, reinterpret_cast<LPARAM>(&search)); Sleep(100); }
        ok = search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && SUCCEEDED(uia->ElementFromHandle(search.window, &root)) && root;
    }
    if (ok) {
        auto nav = find_id(uia, root, L"app.nav");
        ok = nav && SUCCEEDED(select_id(uia, nav, L"about"));
        if (nav) nav->Release();
        auto label = ok ? find_id(uia, root, L"about.diagnostics") : nullptr;
        BSTR name = nullptr;
        ok = label && SUCCEEDED(label->get_CurrentName(&name)) && name && std::wstring(name) == std::wstring(L"診斷：") + argv[2];
        if (label) label->Release(); SysFreeString(name);
    }
    if (root) root->Release(); if (uia) uia->Release();
    if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
    if (pi.hProcess) { if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); }
    std::filesystem::remove(directory); CoUninitialize();
    if (!ok) std::cerr << "About diagnostics executable check failed\n";
    return ok ? 0 : 1;
}
