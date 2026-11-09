#include "uia_client_helpers.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <atomic>
#include <iostream>
#include <string>

class GlobalToggleHandler final : public UiaEventHandler<GlobalToggleHandler, IUIAutomationPropertyChangedEventHandler> {
public:
    std::atomic<DWORD> expected_pid{0};
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender, PROPERTYID property, VARIANT value) override {
        if (property != UIA_ToggleToggleStatePropertyId || value.vt != VT_I4 || value.lVal != ToggleState_Off) return S_OK;
        const DWORD expected = expected_pid.load();
        int pid = 0; if (expected && SUCCEEDED(sender->get_CurrentProcessId(&pid)) && static_cast<DWORD>(pid) == expected) SetEvent(received);
        return S_OK;
    }
};
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    IUIAutomation* automation = nullptr; IUIAutomationElement* desktop = nullptr; auto handler = new GlobalToggleHandler();
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    if (SUCCEEDED(hr)) hr = automation->GetRootElement(&desktop);
    PROPERTYID property = UIA_ToggleToggleStatePropertyId;
    if (SUCCEEDED(hr)) hr = automation->AddPropertyChangedEventHandlerNativeArray(desktop, TreeScope_Subtree, nullptr, handler, &property, 1);
    bool subscribed = SUCCEEDED(hr); PROCESS_INFORMATION process{}; STARTUPINFOW startup{sizeof(startup)}; Search search{};
    if (subscribed) {
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2];
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            CloseHandle(process.hThread); search.pid = process.dwProcessId; handler->expected_pid = search.pid;
            for (int i = 0; i < 100 && !search.window; ++i) { EnumWindows(find_window, reinterpret_cast<LPARAM>(&search)); Sleep(100); }
        }
    }
    DWORD_PTR requests = 0;
    // Windows discovers newly created windows asynchronously. Wait for its
    // WM_GETOBJECT request without requesting the root ourselves: the listener
    // was registered before the host existed and the provider remains lazy.
    if (search.window) for (int i = 0; i < 50 && requests == 0; ++i) {
        SendMessageTimeoutW(search.window, WM_APP + 80, 16, 0, SMTO_ABORTIFHUNG, 1000, &requests);
        if (!requests) Sleep(100);
    }
    DWORD_PTR result = 0;
    bool sent = search.window && SendMessageTimeoutW(search.window, WM_APP + 80, 15, 0, SMTO_ABORTIFHUNG, 5000, &result) && result == 0;
    bool received = sent && WaitForSingleObject(handler->received, 5000) == WAIT_OBJECT_0;
    std::cout << "Desktop-wide listener before host creation: " << (received ? "PASS" : "FAIL") << " subscribe=0x" << std::hex << hr << " sent=" << sent << " root_requests_before_toggle=" << std::dec << requests << '\n';
    if (subscribed) automation->RemovePropertyChangedEventHandler(desktop, handler);
    if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
    if (process.hProcess) { if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1); CloseHandle(process.hProcess); }
    if (desktop) desktop->Release(); if (automation) automation->Release(); handler->Release(); CoUninitialize(); return received ? 0 : 1;
}
