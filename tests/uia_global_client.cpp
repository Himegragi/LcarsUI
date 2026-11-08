#include <windows.h>
#include <UIAutomation.h>
#include <atomic>
#include <iostream>
#include <string>

class GlobalToggleHandler final : public IUIAutomationPropertyChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    std::atomic<DWORD> expected_pid{0};
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~GlobalToggleHandler() { if (received) CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationPropertyChangedEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender, PROPERTYID property, VARIANT value) override {
        if (property != UIA_ToggleToggleStatePropertyId || value.vt != VT_I4 || value.lVal != ToggleState_Off) return S_OK;
        const DWORD expected = expected_pid.load();
        int pid = 0; if (expected && SUCCEEDED(sender->get_CurrentProcessId(&pid)) && static_cast<DWORD>(pid) == expected) SetEvent(received);
        return S_OK;
    }
};
struct Search { DWORD pid; HWND window = nullptr; };
BOOL CALLBACK find_window(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<Search*>(data); DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid == search.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) { search.window = window; return FALSE; }
    return TRUE;
}
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
