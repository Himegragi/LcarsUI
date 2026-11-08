#include <windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <string>

struct Search { DWORD pid; HWND window = nullptr; };
BOOL CALLBACK find_window(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<Search*>(data);
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid == search.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        search.window = window; return FALSE;
    }
    return TRUE;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2];
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 1;
    CloseHandle(process.hThread);
    Search search{process.dwProcessId};
    for (int i = 0; i < 100 && !search.window; ++i) {
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&search)); Sleep(100);
    }
    IUIAutomation* automation = nullptr; IUIAutomationElement* root = nullptr; IUIAutomationElement* element = nullptr;
    IUIAutomationCondition* condition = nullptr;
    HRESULT hr = search.window ? CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)) : E_FAIL;
    if (SUCCEEDED(hr)) hr = automation->ElementFromHandle(search.window, &root);
    VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(L"form.name");
    if (SUCCEEDED(hr)) hr = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    if (SUCCEEDED(hr)) hr = root->FindFirst(TreeScope_Descendants, condition, &element);
    bool pass = SUCCEEDED(hr) && element;
    std::cout << "UIA01 find form.name: " << (pass ? "PASS" : "FAIL") << " hr=0x" << std::hex << hr << '\n';
    if (element) element->Release(); if (condition) condition->Release(); VariantClear(&value);
    if (root) root->Release(); if (automation) automation->Release();
    if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hProcess); CoUninitialize(); return pass ? 0 : 1;
}
