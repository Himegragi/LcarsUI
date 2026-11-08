#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include <atomic>
#include <iostream>
#include "uia_pattern_drivers.hpp"
class EnabledEvent final : public IUIAutomationPropertyChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~EnabledEvent() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { if (!out) return E_POINTER; *out = nullptr; if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationPropertyChangedEventHandler)) return E_NOINTERFACE; *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this); AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override { if (id == UIA_IsEnabledPropertyId && value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE) SetEvent(received); return S_OK; }
};
class ValueEvent final : public IUIAutomationPropertyChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~ValueEvent() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { if (!out) return E_POINTER; *out = nullptr; if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationPropertyChangedEventHandler)) return E_NOINTERFACE; *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this); AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override { if (id == UIA_ValueValuePropertyId && value.vt == VT_BSTR && value.bstrVal && std::wstring(value.bstrVal) == L"sample") SetEvent(received); return S_OK; }
};
class TextEvent final : public IUIAutomationEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    EVENTID expected = UIA_Text_TextChangedEventId;
    ~TextEvent() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { if (!out) return E_POINTER; *out = nullptr; if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationEventHandler)) return E_NOINTERFACE; *out = static_cast<IUIAutomationEventHandler*>(this); AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override { if (id == expected) SetEvent(received); return S_OK; }
};
struct Search { DWORD pid; HWND hwnd = nullptr; };
BOOL CALLBACK find_window(HWND hwnd, LPARAM l) { auto& s = *reinterpret_cast<Search*>(l); DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid); if (pid == s.pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) { s.hwnd = hwnd; return FALSE; } return TRUE; }
IUIAutomationElement* find_id(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(id); IUIAutomationCondition* c = nullptr; IUIAutomationElement* e = nullptr;
    if (root && SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, v, &c))) { root->FindFirst(TreeScope_Descendants, c, &e); c->Release(); }
    VariantClear(&v); return e;
}
bool select(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = find_id(uia, root, id); IUIAutomationSelectionItemPattern* p = nullptr;
    bool ok = e && SUCCEEDED(e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->Select());
    if (p) p->Release(); if (e) e->Release(); return ok;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2]; STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 1; CloseHandle(pi.hThread);
    Search s{pi.dwProcessId}; for (int i = 0; i < 100 && !s.hwnd; ++i) { EnumWindows(find_window, reinterpret_cast<LPARAM>(&s)); Sleep(100); }
    IUIAutomation* uia = nullptr; IUIAutomationElement* root = nullptr; bool ok = s.hwnd && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && SUCCEEDED(uia->ElementFromHandle(s.hwnd, &root)) && root;
    if (ok) {
        auto layout_events = new TextEvent(); layout_events->expected = UIA_LayoutInvalidatedEventId;
        auto layout_subscribed = uia->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, TreeScope_Element, nullptr, layout_events);
        RECT outer{}; GetWindowRect(s.hwnd, &outer); SetWindowPos(s.hwnd, nullptr, 0, 0, outer.right - outer.left + 40, outer.bottom - outer.top + 40, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        bool layout_event = SUCCEEDED(layout_subscribed) && WaitForSingleObject(layout_events->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(layout_subscribed)) uia->RemoveAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, layout_events); layout_events->Release();
        std::cout << "layout event=" << layout_event << '\n'; ok &= layout_event;
        auto nav = find_id(uia, root, L"app.nav"); IUIAutomationSelectionPattern* selection = nullptr; BOOL required = FALSE, multiple = TRUE; CONTROLTYPEID type = 0;
        bool navigation = nav && SUCCEEDED(nav->get_CurrentControlType(&type)) && type == UIA_ListControlTypeId && SUCCEEDED(nav->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection))) && selection && SUCCEEDED(selection->get_CurrentIsSelectionRequired(&required)) && required && SUCCEEDED(selection->get_CurrentCanSelectMultiple(&multiple)) && !multiple;
        navigation = navigation && patterns(uia, root) && select(uia, nav, L"items") && !find_id(uia, root, L"general.name") && patterns(uia, root);
        auto items_list = find_id(uia, root, L"items.list"); IUIAutomationSelectionPattern* item_selection = nullptr; BOOL list_required = TRUE;
        bool optional_list = items_list && SUCCEEDED(items_list->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&item_selection))) && item_selection && SUCCEEDED(item_selection->get_CurrentIsSelectionRequired(&list_required)) && !list_required;
        if (item_selection) item_selection->Release(); if (items_list) items_list->Release();
        std::cout << "optional list=" << optional_list << '\n'; navigation &= optional_list;
        auto edit = find_id(uia, root, L"items.edit"); auto events = new EnabledEvent(); PROPERTYID enabled = UIA_IsEnabledPropertyId;
        auto subscribed = edit ? uia->AddPropertyChangedEventHandlerNativeArray(edit, TreeScope_Element, nullptr, events, &enabled, 1) : E_FAIL;
        bool enabled_event = SUCCEEDED(subscribed) && select(uia, root, L"T001") && WaitForSingleObject(events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(subscribed)) uia->RemovePropertyChangedEventHandler(edit, events); events->Release(); if (edit) edit->Release();
        std::cout << "enabled event=" << enabled_event << '\n'; navigation &= enabled_event;
        auto search = find_id(uia, root, L"items.search"); auto text_events = new TextEvent();
        auto value_events = new ValueEvent(); PROPERTYID value_property = UIA_ValueValuePropertyId;
        auto value_subscribed = search ? uia->AddPropertyChangedEventHandlerNativeArray(search, TreeScope_Element, nullptr, value_events, &value_property, 1) : E_FAIL;
        auto text_subscribed = search ? uia->AddAutomationEventHandler(UIA_Text_TextChangedEventId, search, TreeScope_Element, nullptr, text_events) : E_FAIL;
        auto changed = SendMessageW(s.hwnd, WM_APP + 81, 1, 0);
        bool text_event = changed == 0 && SUCCEEDED(text_subscribed) && WaitForSingleObject(text_events->received, 2000) == WAIT_OBJECT_0;
        bool value_event = SUCCEEDED(value_subscribed) && WaitForSingleObject(value_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(value_subscribed)) uia->RemovePropertyChangedEventHandler(search, value_events); value_events->Release();
        std::cout << "value event=" << value_event << '\n'; navigation &= value_event;
        if (SUCCEEDED(text_subscribed)) uia->RemoveAutomationEventHandler(UIA_Text_TextChangedEventId, search, text_events); text_events->Release();
        auto selection_events = new TextEvent(); selection_events->expected = UIA_Text_TextSelectionChangedEventId;
        auto selection_subscribed = search ? uia->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, search, TreeScope_Element, nullptr, selection_events) : E_FAIL;
        auto selection_changed = SendMessageW(s.hwnd, WM_APP + 82, 0, 0);
        bool selection_event = selection_changed == 0 && SUCCEEDED(selection_subscribed) && WaitForSingleObject(selection_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selection_subscribed)) uia->RemoveAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, search, selection_events); selection_events->Release(); if (search) search->Release();
        std::cout << "text selection event=" << selection_event << '\n'; navigation &= selection_event;
        SendMessageW(s.hwnd, WM_APP + 81, 0, 0); std::cout << "text event=" << text_event << '\n'; navigation &= text_event;
        navigation = navigation && select(uia, nav, L"appearance") && patterns(uia, root);
        auto font = find_id(uia, root, L"appearance.font"); IUIAutomationExpandCollapsePattern* expand = nullptr;
        auto default_option = font ? find_id(uia, font, L"default") : nullptr; BOOL closed_offscreen = FALSE;
        bool collapsed = default_option && SUCCEEDED(default_option->get_CurrentIsOffscreen(&closed_offscreen)) && closed_offscreen;
        bool dropdown = font && SUCCEEDED(font->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand && SUCCEEDED(expand->Expand());
        auto last = font ? find_id(uia, font, L"family:Font149") : nullptr; BOOL offscreen = FALSE;
        dropdown = dropdown && last && SUCCEEDED(last->get_CurrentIsOffscreen(&offscreen)) && offscreen && patterns(uia, root);
        if (expand) { collapsed = collapsed && SUCCEEDED(expand->Collapse()); closed_offscreen = FALSE; collapsed = collapsed && SUCCEEDED(default_option->get_CurrentIsOffscreen(&closed_offscreen)) && closed_offscreen; }
        dropdown = dropdown && collapsed; if (default_option) default_option->Release();
        if (last) last->Release(); if (expand) expand->Release(); if (font) font->Release();
        bool about = select(uia, nav, L"about") && patterns(uia, root); auto text = find_id(uia, root, L"about.license_text"); BSTR name = nullptr; type = 0; BOOL focusable = FALSE;
        about = about && text && SUCCEEDED(text->get_CurrentControlType(&type)) && type == UIA_TextControlTypeId && SUCCEEDED(text->get_CurrentName(&name)) && name && SysStringLen(name) == 3009 && SUCCEEDED(text->get_CurrentIsKeyboardFocusable(&focusable)) && focusable;
        SysFreeString(name); if (text) text->Release();
        std::cout << "navigation=" << navigation << " long-dropdown=" << dropdown << " about-text=" << about << '\n'; ok = ok && navigation && dropdown && about;
        if (selection) selection->Release(); if (nav) nav->Release();
    }
    if (root) root->Release(); if (uia) uia->Release(); if (s.hwnd) PostMessageW(s.hwnd, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); ok = false; } CloseHandle(pi.hProcess); CoUninitialize(); return ok ? 0 : 1;
}
