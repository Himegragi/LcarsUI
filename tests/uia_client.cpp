#include <windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <string>
#include <atomic>

class PropertyEvents final : public IUIAutomationPropertyChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~PropertyEvents() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationPropertyChangedEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override {
        if (id == UIA_ToggleToggleStatePropertyId && value.vt == VT_I4 && value.lVal == ToggleState_Off) SetEvent(received);
        return S_OK;
    }
};

struct Search { DWORD pid; HWND window = nullptr; };
BOOL CALLBACK find_window(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<Search*>(data);
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid == search.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        search.window = window; return FALSE;
    }
    return TRUE;
}
IUIAutomationElement* find_id(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    IUIAutomationCondition* condition = nullptr; IUIAutomationElement* element = nullptr;
    if (SUCCEEDED(automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition))) {
        root->FindFirst(TreeScope_Descendants, condition, &element); condition->Release();
    }
    VariantClear(&value); return element;
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
    IUIAutomationElement* retained = nullptr;
    IUIAutomationCondition* condition = nullptr;
    HRESULT hr = search.window ? CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)) : E_FAIL;
    if (SUCCEEDED(hr)) hr = automation->ElementFromHandle(search.window, &root);
    VARIANT value; VariantInit(&value); value.vt = VT_BSTR; value.bstrVal = SysAllocString(L"form.name");
    if (SUCCEEDED(hr)) hr = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    if (SUCCEEDED(hr)) hr = root->FindFirst(TreeScope_Descendants, condition, &element);
    bool pass = SUCCEEDED(hr) && element;
    if (pass) {
        CONTROLTYPEID type = 0; BSTR name = nullptr, label_id = nullptr; RECT bounds{}, client{};
        IUIAutomationElement* label = nullptr;
        auto hidden = find_id(automation, root, L"form.title.pages.v03.progress");
        bool field = SUCCEEDED(element->get_CurrentControlType(&type)) && type == UIA_EditControlTypeId &&
            SUCCEEDED(element->get_CurrentName(&name)) && name && std::wstring(name) == L"顯示名稱" &&
            SUCCEEDED(element->get_CurrentLabeledBy(&label)) && label && SUCCEEDED(label->get_CurrentAutomationId(&label_id)) &&
            label_id && std::wstring(label_id) == L"form.name.label" && SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds));
        GetClientRect(search.window, &client); POINT origin{0, 0}; ClientToScreen(search.window, &origin);
        field = field && bounds.left >= origin.x && bounds.top >= origin.y && bounds.right <= origin.x + client.right && bounds.bottom <= origin.y + client.bottom && bounds.right > bounds.left && bounds.bottom > bounds.top && !hidden;
        if (hidden) hidden->Release(); if (label) label->Release(); SysFreeString(name); SysFreeString(label_id);
        SendMessageW(search.window, WM_APP + 80, 6, 0);
        auto first = find_id(automation, root, L"T005"); auto second = find_id(automation, root, L"T006");
        BSTR first_name = nullptr, second_name = nullptr, first_id = nullptr, second_id = nullptr; CONTROLTYPEID first_type = 0, second_type = 0;
        bool items = first && second && SUCCEEDED(first->get_CurrentControlType(&first_type)) && SUCCEEDED(second->get_CurrentControlType(&second_type)) &&
            first_type == UIA_ListItemControlTypeId && second_type == UIA_ListItemControlTypeId &&
            SUCCEEDED(first->get_CurrentName(&first_name)) && SUCCEEDED(second->get_CurrentName(&second_name)) && first_name && second_name &&
            std::wstring(first_name) == L"共用範本" && std::wstring(first_name) == std::wstring(second_name) &&
            SUCCEEDED(first->get_CurrentAutomationId(&first_id)) && SUCCEEDED(second->get_CurrentAutomationId(&second_id)) && first_id && second_id && std::wstring(first_id) != std::wstring(second_id);
        SysFreeString(first_name); SysFreeString(second_name); SysFreeString(first_id); SysFreeString(second_id); if (first) first->Release(); if (second) second->Release();
        SendMessageW(search.window, WM_APP + 80, 5, 0);
        pass = field && items;
        std::cout << "UIA01 details field=" << field << " items=" << items << '\n';
    }
    std::cout << "UIA01 tree and identity: " << (pass ? "PASS" : "FAIL") << " hr=0x" << std::hex << hr << '\n';
    if (pass) {
        auto apply = find_id(automation, root, L"form.apply");
        auto count = find_id(automation, root, L"result.count");
        IUIAutomationInvokePattern* invoke = nullptr; BSTR before = nullptr; BSTR after = nullptr;
        HRESULT get_name = count ? count->get_CurrentName(&before) : E_FAIL;
        HRESULT get_invoke = apply ? apply->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)) : E_FAIL;
        HRESULT invoke_hr = invoke ? invoke->Invoke() : E_FAIL;
        HRESULT get_after = count ? count->get_CurrentName(&after) : E_FAIL;
        bool invoked = apply && count && SUCCEEDED(get_name) && SUCCEEDED(get_invoke) && invoke &&
            SUCCEEDED(invoke_hr) && SUCCEEDED(get_after) && before && after &&
            std::wstring(before, SysStringLen(before)) != std::wstring(after, SysStringLen(after));
        std::cout << "UIA02 Invoke: " << (invoked ? "PASS" : "FAIL") << '\n'; pass &= invoked;
        if (!invoked) std::cout << "  hr " << get_name << ' ' << get_invoke << ' ' << invoke_hr << ' ' << get_after << '\n';
        SysFreeString(before); SysFreeString(after); before = after = nullptr;
        IUIAutomationCacheRequest* cache = nullptr; IUIAutomationElement* cached_apply = nullptr;
        if (invoke) { invoke->Release(); invoke = nullptr; }
        if (SUCCEEDED(automation->CreateCacheRequest(&cache)) && SUCCEEDED(cache->AddPattern(UIA_InvokePatternId)) &&
            SUCCEEDED(apply->BuildUpdatedCache(cache, &cached_apply)))
            cached_apply->GetCachedPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
        HRESULT warmed = invoke ? invoke->Invoke() : E_FAIL;
        if (count) count->get_CurrentName(&before);
        auto signal_name = L"Local\\LcarsUiaBusy-" + std::to_wstring(process.dwProcessId);
        HANDLE busy_signal = CreateEventW(nullptr, TRUE, FALSE, signal_name.c_str());
        PostMessageW(search.window, WM_APP + 80, 1, 0);
        bool busy_started = busy_signal && WaitForSingleObject(busy_signal, 5000) == WAIT_OBJECT_0;
        if (busy_signal) CloseHandle(busy_signal);
        auto start = GetTickCount64(); HRESULT timeout = invoke ? invoke->Invoke() : E_FAIL; auto elapsed = GetTickCount64() - start;
        Sleep(2200); if (count) count->get_CurrentName(&after);
        bool timed_out = busy_started && SUCCEEDED(warmed) && timeout == static_cast<HRESULT>(UIA_E_TIMEOUT) && elapsed >= 900 && elapsed < 2000 && before && after &&
            std::wstring(before, SysStringLen(before)) == std::wstring(after, SysStringLen(after));
        std::cout << "UIA16 timeout: " << (timed_out ? "PASS" : "FAIL") << " hr=0x" << timeout << " ms=" << std::dec << elapsed << '\n'; pass &= timed_out;
        SysFreeString(before); SysFreeString(after); if (invoke) invoke->Release(); if (cached_apply) cached_apply->Release(); if (cache) cache->Release(); if (apply) apply->Release(); if (count) count->Release();
        auto notices = find_id(automation, root, L"form.notices"); IUIAutomationTogglePattern* toggle = nullptr;
        auto events = new PropertyEvents(); PROPERTYID property = UIA_ToggleToggleStatePropertyId;
        HRESULT subscribed = notices ? automation->AddPropertyChangedEventHandlerNativeArray(notices, TreeScope_Element, nullptr, events, &property, 1) : E_FAIL;
        ToggleState old_state{}, new_state{};
        bool toggled = notices && SUCCEEDED(notices->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle &&
            SUCCEEDED(toggle->get_CurrentToggleState(&old_state)) && old_state == ToggleState_On && SUCCEEDED(toggle->Toggle()) &&
            SUCCEEDED(toggle->get_CurrentToggleState(&new_state)) && new_state == ToggleState_Off && SUCCEEDED(subscribed) &&
            WaitForSingleObject(events->received, 2000) == WAIT_OBJECT_0;
        std::cout << "UIA03 Toggle: " << (toggled ? "PASS" : "FAIL") << '\n'; pass &= toggled;
        if (SUCCEEDED(subscribed)) automation->RemovePropertyChangedEventHandler(notices, events);
        events->Release(); if (toggle) toggle->Release(); if (notices) notices->Release();
        const HRESULT expected[] = {S_OK, static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED),
            static_cast<HRESULT>(UIA_E_INVALIDOPERATION), static_cast<HRESULT>(UIA_E_INVALIDOPERATION), static_cast<HRESULT>(UIA_E_INVALIDOPERATION), E_INVALIDARG,
            E_FAIL, E_FAIL, static_cast<HRESULT>(UIA_E_NOTSUPPORTED), static_cast<HRESULT>(UIA_E_INVALIDOPERATION),
            static_cast<HRESULT>(UIA_E_INVALIDOPERATION), static_cast<HRESULT>(UIA_E_INVALIDOPERATION)};
        bool mapping = true;
        for (unsigned i = 0; i < sizeof(expected) / sizeof(*expected); ++i) {
            auto id = L"fixture.status." + std::to_wstring(i); auto target = find_id(automation, root, id.c_str()); IUIAutomationInvokePattern* action = nullptr;
            HRESULT actual = target ? target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&action)) : E_FAIL;
            if (SUCCEEDED(actual) && action) actual = action->Invoke();
            if (actual != expected[i]) { std::cout << "UIA15 status=" << i << " expected=0x" << std::hex << expected[i] << " actual=0x" << actual << '\n'; mapping = false; }
            if (action) action->Release(); if (target) target->Release();
        }
        auto disabled = find_id(automation, root, L"fixture.real_disabled"); IUIAutomationInvokePattern* disabled_action = nullptr;
        HRESULT disabled_hr = disabled ? disabled->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&disabled_action)) : E_FAIL;
        if (SUCCEEDED(disabled_hr) && disabled_action) disabled_hr = disabled_action->Invoke();
        mapping = mapping && disabled_hr == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        if (disabled_action) disabled_action->Release(); if (disabled) disabled->Release();
        apply = find_id(automation, root, L"form.apply"); invoke = nullptr;
        if (apply) apply->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
        SendMessageW(search.window, WM_APP + 80, 2, 0);
        HRESULT hidden_hr = invoke ? invoke->Invoke() : E_FAIL;
        mapping = mapping && hidden_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        if (invoke) invoke->Release(); if (apply) apply->Release();
        auto modal_target = find_id(automation, root, L"form.title.pages.v03.temporary"); IUIAutomationInvokePattern* modal_action = nullptr;
        if (modal_target) modal_target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&modal_action));
        SendMessageW(search.window, WM_APP + 80, 7, 0);
        HRESULT modal_hr = modal_action ? modal_action->Invoke() : E_FAIL;
        mapping = mapping && modal_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        SendMessageW(search.window, WM_APP + 80, 8, 0);
        if (modal_action) modal_action->Release(); if (modal_target) modal_target->Release();
        SendMessageW(search.window, WM_APP + 80, 5, 0);
        if (!mapping) std::cout << "UIA15 actual disabled/hidden/modal: 0x" << std::hex << disabled_hr << ' ' << hidden_hr << ' ' << modal_hr << '\n';
        std::cout << "UIA15 status mapping: " << (mapping ? "PASS" : "FAIL") << '\n'; pass &= mapping;
        SendMessageW(search.window, WM_APP + 80, 2, 0);
        auto temporary = find_id(automation, root, L"form.title.pages.v03.temporary"); SAFEARRAY* old_id = nullptr; SAFEARRAY* new_id = nullptr;
        if (temporary) temporary->GetRuntimeId(&old_id);
        SendMessageW(search.window, WM_APP + 80, 3, 0);
        VARIANT removed; VariantInit(&removed);
        HRESULT removed_hr = temporary ? temporary->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &removed) : E_FAIL; VariantClear(&removed);
        SendMessageW(search.window, WM_APP + 80, 4, 0);
        auto replacement = find_id(automation, root, L"form.title.pages.v03.temporary");
        if (replacement) replacement->GetRuntimeId(&new_id);
        BOOL equal = TRUE; if (old_id && new_id) automation->CompareRuntimeIds(old_id, new_id, &equal);
        HRESULT still_removed = temporary ? temporary->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &removed) : E_FAIL; VariantClear(&removed);
        bool lifecycle = removed_hr == UIA_E_ELEMENTNOTAVAILABLE && still_removed == UIA_E_ELEMENTNOTAVAILABLE && replacement && old_id && new_id && !equal;
        if (replacement) { retained = replacement; retained->AddRef(); }
        std::cout << "UIA19 removal: " << (lifecycle ? "PASS" : "FAIL") << " hr=0x" << std::hex << removed_hr << ' ' << still_removed << '\n'; pass &= lifecycle;
        if (old_id) SafeArrayDestroy(old_id); if (new_id) SafeArrayDestroy(new_id); if (temporary) temporary->Release(); if (replacement) replacement->Release();
    }
    if (element) element->Release(); if (condition) condition->Release(); VariantClear(&value);
    if (root) root->Release(); if (automation) automation->Release();
    if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); pass = false; }
    VARIANT closed; VariantInit(&closed);
    HRESULT closed_hr = retained ? retained->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &closed) : E_FAIL;
    bool closed_pass = closed_hr == UIA_E_ELEMENTNOTAVAILABLE;
    std::cout << "UIA19 close: " << (closed_pass ? "PASS" : "FAIL") << " hr=0x" << std::hex << closed_hr << '\n';
    VariantClear(&closed); if (retained) retained->Release(); pass &= closed_pass;
    CloseHandle(process.hProcess); CoUninitialize(); return pass ? 0 : 1;
}
