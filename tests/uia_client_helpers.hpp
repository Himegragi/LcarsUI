#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <atomic>
struct Search { DWORD pid; HWND window = nullptr; };
inline BOOL CALLBACK find_window(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<Search*>(data);
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid == search.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        search.window = window; return FALSE;
    }
    return TRUE;
}
inline IUIAutomationElement* find_id(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* id) {
    if (!root) return nullptr;
    VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    IUIAutomationCondition* condition = nullptr; IUIAutomationElement* element = nullptr;
    if (SUCCEEDED(automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition))) {
        root->FindFirst(TreeScope_Descendants, condition, &element); condition->Release();
    }
    VariantClear(&value); return element;
}
inline HRESULT invoke_id(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = find_id(uia, root, id); IUIAutomationInvokePattern* p = nullptr;
    HRESULT hr = e ? e->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&p)) : E_FAIL;
    if (SUCCEEDED(hr) && p) hr = p->Invoke(); if (p) p->Release(); if (e) e->Release(); return hr;
}
inline HRESULT select_id(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = find_id(uia, root, id); IUIAutomationSelectionItemPattern* p = nullptr;
    HRESULT hr = e ? e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&p)) : E_FAIL;
    if (SUCCEEDED(hr) && p) hr = p->Select(); if (p) p->Release(); if (e) e->Release(); return hr;
}

template<class Derived, class Interface>
class UiaEventHandler : public Interface {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~UiaEventHandler() { if (received) CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(Interface)) return E_NOINTERFACE;
        *out = static_cast<Interface*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete static_cast<Derived*>(this); return count; }
};
