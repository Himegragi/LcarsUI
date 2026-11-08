#include <windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include "uia_pattern_drivers.hpp"

class PropertyEvents final : public IUIAutomationPropertyChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    PROPERTYID expected = UIA_ToggleToggleStatePropertyId;
    LONG expected_value = ToggleState_Off;
    bool double_value = false; double expected_double = 0;
    bool string_value = false; std::wstring expected_string;
    ~PropertyEvents() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationPropertyChangedEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override {
        if (id == expected && ((string_value && value.vt == VT_BSTR && value.bstrVal && expected_string == value.bstrVal) || (!string_value && !double_value && value.vt == VT_I4 && value.lVal == expected_value) || (double_value && value.vt == VT_R8 && value.dblVal == expected_double))) SetEvent(received);
        return S_OK;
    }
};

class SelectionEvents final : public IUIAutomationEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    EVENTID expected = UIA_SelectionItem_ElementSelectedEventId;
    ~SelectionEvents() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override {
        if (id == expected) SetEvent(received); return S_OK;
    }
};

class StructureEvents final : public IUIAutomationStructureChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~StructureEvents() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationStructureChangedEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationStructureChangedEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType type, SAFEARRAY*) override {
        if (type == StructureChangeType_ChildrenInvalidated) SetEvent(received); return S_OK;
    }
};

class FocusEvents final : public IUIAutomationFocusChangedEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::wstring expected = L"T004";
    std::mutex mutex; std::vector<std::wstring> observed;
    HANDLE apply_received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~FocusEvents() { CloseHandle(received); CloseHandle(apply_received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationFocusChangedEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        BSTR id = nullptr; if (sender && SUCCEEDED(sender->get_CurrentAutomationId(&id)) && id) {
            std::wstring value(id); if (value == L"T003" || value == L"T004") { std::lock_guard lock(mutex); observed.push_back(value); }
            if (value == expected) SetEvent(received); if (value == L"form.apply") SetEvent(apply_received);
        }
        SysFreeString(id); return S_OK;
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
HRESULT invoke_id(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = find_id(uia, root, id); IUIAutomationInvokePattern* p = nullptr;
    HRESULT hr = e ? e->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&p)) : E_FAIL;
    if (SUCCEEDED(hr) && p) hr = p->Invoke(); if (p) p->Release(); if (e) e->Release(); return hr;
}
HRESULT select_id(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = find_id(uia, root, id); IUIAutomationSelectionItemPattern* p = nullptr;
    HRESULT hr = e ? e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&p)) : E_FAIL;
    if (SUCCEEDED(hr) && p) hr = p->Select(); if (p) p->Release(); if (e) e->Release(); return hr;
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
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
    auto focus_events = new FocusEvents(); auto focus_subscribed = pass ? automation->AddFocusChangedEventHandler(nullptr, focus_events) : E_FAIL;
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
        auto structures = new StructureEvents();
        auto structure_subscribed = automation->AddStructureChangedEventHandler(root, TreeScope_Element, nullptr, structures);
        SendMessageW(search.window, WM_APP + 80, 6, 0);
        bool structure = SUCCEEDED(structure_subscribed) && WaitForSingleObject(structures->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(structure_subscribed)) automation->RemoveStructureChangedEventHandler(root, structures);
        structures->Release();
        std::cout << "UIA10 structure event: " << (structure ? "PASS" : "FAIL") << '\n'; pass &= structure;
        auto first = find_id(automation, root, L"T005"); auto second = find_id(automation, root, L"T006");
        BSTR first_name = nullptr, second_name = nullptr, first_id = nullptr, second_id = nullptr; CONTROLTYPEID first_type = 0, second_type = 0;
        bool items = first && second && SUCCEEDED(first->get_CurrentControlType(&first_type)) && SUCCEEDED(second->get_CurrentControlType(&second_type)) &&
            first_type == UIA_ListItemControlTypeId && second_type == UIA_ListItemControlTypeId &&
            SUCCEEDED(first->get_CurrentName(&first_name)) && SUCCEEDED(second->get_CurrentName(&second_name)) && first_name && second_name &&
            std::wstring(first_name) == L"共用範本" && std::wstring(first_name) == std::wstring(second_name) &&
            SUCCEEDED(first->get_CurrentAutomationId(&first_id)) && SUCCEEDED(second->get_CurrentAutomationId(&second_id)) && first_id && second_id && std::wstring(first_id) != std::wstring(second_id);
        bool list_case = SUCCEEDED(select_id(automation, root, L"T006")) && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v02.sort"));
        IUIAutomationSelectionItemPattern* list_item = nullptr; BOOL selected_item = FALSE;
        list_case = list_case && second && SUCCEEDED(second->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&list_item))) && list_item && SUCCEEDED(list_item->get_CurrentIsSelected(&selected_item)) && selected_item;
        if (list_item) list_item->Release();
        list_case = list_case && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v02.disable")) && select_id(automation, root, L"T008") == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        list_case = list_case && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v02.sort"));
        bool list_focus = SUCCEEDED(focus_subscribed) && SendMessageW(search.window, WM_APP + 80, 19, 0) == 0 && WaitForSingleObject(focus_events->received, 10000) == WAIT_OBJECT_0;
        { std::lock_guard lock(focus_events->mutex); list_focus = list_focus && focus_events->observed.size() >= 2 && focus_events->observed[0] == L"T003" && focus_events->observed[1] == L"T004"; }
        std::cout << "UIA14 list focus: " << (list_focus ? "PASS" : "FAIL") << '\n'; pass &= list_focus;
        pass &= patterns(automation, root);
        list_case = list_case && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v02.hundred"));
        auto last_item = find_id(automation, root, L"T100"); BOOL last_offscreen = FALSE;
        list_case = list_case && last_item && SUCCEEDED(last_item->get_CurrentIsOffscreen(&last_offscreen)) && last_offscreen;
        if (last_item) last_item->Release();
        list_case = list_case && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v02.twenty"));
        std::cout << "UIA09 list: " << (list_case ? "PASS" : "FAIL") << '\n'; pass &= list_case;
        SysFreeString(first_name); SysFreeString(second_name); SysFreeString(first_id); SysFreeString(second_id); if (first) first->Release(); if (second) second->Release();
        SendMessageW(search.window, WM_APP + 80, 5, 0);
        pass = pass && field && items && patterns(automation, root);
        std::cout << "UIA01 details field=" << field << " items=" << items << '\n';
    }
    std::cout << "UIA01 tree and identity: " << (pass ? "PASS" : "FAIL") << " hr=0x" << std::hex << hr << '\n';
    if (pass) {
        auto apply = find_id(automation, root, L"form.apply");
        bool focus = apply && SUCCEEDED(focus_subscribed) && SendMessageW(search.window, WM_APP + 80, 17, 0) == 0 && WaitForSingleObject(focus_events->apply_received, 2000) == WAIT_OBJECT_0;
        std::cout << "UIA14 focus event: " << (focus ? "PASS" : "FAIL") << '\n'; pass &= focus;
        auto count = find_id(automation, root, L"result.count");
        auto name_events = new PropertyEvents(); name_events->expected = UIA_NamePropertyId; name_events->string_value = true; name_events->expected_string = L"\u5957\u7528\u6b21\u6578\uff1a1";
        PROPERTYID name_property = UIA_NamePropertyId;
        auto name_subscribed = count ? automation->AddPropertyChangedEventHandlerNativeArray(count, TreeScope_Element, nullptr, name_events, &name_property, 1) : E_FAIL;
        IUIAutomationInvokePattern* invoke = nullptr; BSTR before = nullptr; BSTR after = nullptr;
        HRESULT get_name = count ? count->get_CurrentName(&before) : E_FAIL;
        HRESULT get_invoke = apply ? apply->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)) : E_FAIL;
        HRESULT invoke_hr = invoke ? invoke->Invoke() : E_FAIL;
        HRESULT get_after = count ? count->get_CurrentName(&after) : E_FAIL;
        bool invoked = apply && count && SUCCEEDED(get_name) && SUCCEEDED(get_invoke) && invoke &&
            SUCCEEDED(invoke_hr) && SUCCEEDED(get_after) && before && after &&
            std::wstring(before, SysStringLen(before)) == L"套用次數：0" &&
            std::wstring(after, SysStringLen(after)) == L"套用次數：1";
        bool name_event = SUCCEEDED(name_subscribed) && WaitForSingleObject(name_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(name_subscribed)) automation->RemovePropertyChangedEventHandler(count, name_events); name_events->Release();
        std::cout << "Name event: " << (name_event ? "PASS" : "FAIL") << '\n'; pass &= name_event;
        std::cout << "UIA02 Invoke: " << (invoked ? "PASS" : "FAIL") << '\n'; pass &= invoked;
        if (!invoked) std::cout << "  hr " << get_name << ' ' << get_invoke << ' ' << invoke_hr << ' ' << get_after << '\n';
        SysFreeString(before); SysFreeString(after);
        if (invoke) invoke->Release(); if (apply) apply->Release(); if (count) count->Release();
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
        pass &= patterns(automation, root);
        auto mode = find_id(automation, root, L"form.mode");
        IUIAutomationSelectionPattern* selection = nullptr;
        IUIAutomationElementArray* selected = nullptr;
        BOOL multiple = TRUE;
        bool radio = mode && SUCCEEDED(mode->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection))) && selection &&
            SUCCEEDED(selection->get_CurrentCanSelectMultiple(&multiple)) && !multiple &&
            SUCCEEDED(selection->GetCurrentSelection(&selected)) && selected;
        int selected_count = 0;
        radio = radio && SUCCEEDED(selected->get_Length(&selected_count)) && selected_count == 1;
        auto focus_option = find_id(automation, mode, L"focus");
        IUIAutomationSelectionItemPattern* select_item = nullptr;
        auto selection_events = new SelectionEvents();
        HRESULT selection_subscribed = focus_option ? automation->AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, focus_option, TreeScope_Element, nullptr, selection_events) : E_FAIL;
        radio = radio && focus_option && SUCCEEDED(focus_option->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&select_item))) &&
            select_item && SUCCEEDED(select_item->Select());
        BOOL is_selected = FALSE;
        radio = radio && SUCCEEDED(select_item->get_CurrentIsSelected(&is_selected)) && is_selected;
        radio = radio && SUCCEEDED(selection_subscribed) && WaitForSingleObject(selection_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selection_subscribed)) automation->RemoveAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, focus_option, selection_events);
        selection_events->Release();
        radio = radio && SUCCEEDED(invoke_id(automation, root, L"fixture.offline_disabled")) && select_id(automation, mode, L"offline") == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        std::cout << "UIA04 radio selection: " << (radio ? "PASS" : "FAIL") << '\n'; pass &= radio;
        if (select_item) select_item->Release(); if (focus_option) focus_option->Release();
        if (selected) selected->Release(); if (selection) selection->Release(); if (mode) mode->Release();
        auto region = find_id(automation, root, L"form.region");
        IUIAutomationExpandCollapsePattern* expand = nullptr;
        ExpandCollapseState expanded{};
        auto expand_events = new PropertyEvents(); expand_events->expected = UIA_ExpandCollapseExpandCollapseStatePropertyId; expand_events->expected_value = ExpandCollapseState_Expanded;
        PROPERTYID expand_property = UIA_ExpandCollapseExpandCollapseStatePropertyId;
        auto expand_subscribed = region ? automation->AddPropertyChangedEventHandlerNativeArray(region, TreeScope_Element, nullptr, expand_events, &expand_property, 1) : E_FAIL;
        bool dropdown = region && SUCCEEDED(region->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand &&
            SUCCEEDED(expand->Expand()) && SUCCEEDED(expand_subscribed) && WaitForSingleObject(expand_events->received, 2000) == WAIT_OBJECT_0 && SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Expanded &&
            SUCCEEDED(expand->Collapse()) && SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Collapsed;
        dropdown = dropdown && SUCCEEDED(expand->Expand()) && SUCCEEDED(select_id(automation, region, L"south"));
        auto south = find_id(automation, region, L"south"); IUIAutomationSelectionItemPattern* south_pattern = nullptr; BOOL south_selected = FALSE;
        dropdown = dropdown && south && SUCCEEDED(south->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&south_pattern))) && south_pattern && SUCCEEDED(south_pattern->get_CurrentIsSelected(&south_selected)) && south_selected;
        if (south_pattern) south_pattern->Release(); if (south) south->Release();
        std::cout << "UIA05 dropdown: " << (dropdown ? "PASS" : "FAIL") << '\n'; pass &= dropdown;
        if (SUCCEEDED(expand_subscribed)) automation->RemovePropertyChangedEventHandler(region, expand_events); expand_events->Release();
        if (expand) expand->Release(); if (region) region->Release();
        auto volume = find_id(automation, root, L"form.volume"); IUIAutomationRangeValuePattern* range = nullptr;
        double min = 0, max = 0, step = 0, value_now = 0;
        auto range_events = new PropertyEvents(); range_events->expected = UIA_RangeValueValuePropertyId; range_events->double_value = true; range_events->expected_double = 65;
        PROPERTYID range_property = UIA_RangeValueValuePropertyId;
        auto range_subscribed = volume ? automation->AddPropertyChangedEventHandlerNativeArray(volume, TreeScope_Element, nullptr, range_events, &range_property, 1) : E_FAIL;
        bool slider = volume && SUCCEEDED(volume->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range &&
            SUCCEEDED(range->get_CurrentMinimum(&min)) && min == 0 && SUCCEEDED(range->get_CurrentMaximum(&max)) && max == 100 &&
            SUCCEEDED(range->get_CurrentSmallChange(&step)) && step == 5 && SUCCEEDED(range->SetValue(65)) && SUCCEEDED(range_subscribed) && WaitForSingleObject(range_events->received, 2000) == WAIT_OBJECT_0 &&
            range->SetValue(101) == E_INVALIDARG && range->SetValue(62.5) == E_INVALIDARG && SUCCEEDED(range->get_CurrentValue(&value_now)) && value_now == 65;
        std::cout << "UIA06 slider: " << (slider ? "PASS" : "FAIL") << '\n'; pass &= slider;
        if (SUCCEEDED(range_subscribed)) automation->RemovePropertyChangedEventHandler(volume, range_events); range_events->Release();
        if (range) range->Release(); if (volume) volume->Release();
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
        pass &= patterns(automation, root);
        auto progress = find_id(automation, root, L"form.title.pages.v03.progress"); IUIAutomationRangeValuePattern* progress_pattern = nullptr; BOOL readonly = FALSE; double progress_value = 0;
        auto progress_events = new PropertyEvents(); progress_events->expected = UIA_RangeValueValuePropertyId; progress_events->double_value = true; progress_events->expected_double = 100;
        PROPERTYID progress_property = UIA_RangeValueValuePropertyId;
        auto progress_subscribed = progress ? automation->AddPropertyChangedEventHandlerNativeArray(progress, TreeScope_Element, nullptr, progress_events, &progress_property, 1) : E_FAIL;
        bool progress_case = progress && SUCCEEDED(progress->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&progress_pattern))) && progress_pattern && SUCCEEDED(progress_pattern->get_CurrentIsReadOnly(&readonly)) && readonly && SUCCEEDED(progress_pattern->get_CurrentValue(&progress_value)) && progress_value == 50 && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v03.above")) && SUCCEEDED(progress_subscribed) && WaitForSingleObject(progress_events->received, 2000) == WAIT_OBJECT_0 && SUCCEEDED(progress_pattern->get_CurrentValue(&progress_value)) && progress_value == 100;
        std::cout << "UIA11 progress: " << (progress_case ? "PASS" : "FAIL") << '\n'; pass &= progress_case;
        if (SUCCEEDED(progress_subscribed)) automation->RemovePropertyChangedEventHandler(progress, progress_events); progress_events->Release(); if (progress_pattern) progress_pattern->Release(); if (progress) progress->Release();
        invoke_id(automation, root, L"form.title.pages.v03.half");
        auto modal_target = find_id(automation, root, L"form.title.pages.v03.temporary"); IUIAutomationInvokePattern* modal_action = nullptr;
        if (modal_target) modal_target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&modal_action));
        auto window_events = new SelectionEvents(); window_events->expected = UIA_Window_WindowOpenedEventId;
        auto window_subscribed = automation->AddAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, TreeScope_Subtree, nullptr, window_events);
        SendMessageW(search.window, WM_APP + 80, 7, 0);
        bool opened_event = SUCCEEDED(window_subscribed) && WaitForSingleObject(window_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(window_subscribed)) automation->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, window_events); window_events->Release();
        std::cout << "UIA12 window opened event: " << (opened_event ? "PASS" : "FAIL") << '\n'; pass &= opened_event;
        pass &= patterns(automation, root);
        auto dialog = find_id(automation, root, L"form.title.pages.dialog"); IUIAutomationWindowPattern* window_pattern = nullptr; BOOL is_modal = FALSE;
        bool window = dialog && SUCCEEDED(dialog->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&window_pattern))) && window_pattern &&
            SUCCEEDED(window_pattern->get_CurrentIsModal(&is_modal)) && is_modal && SUCCEEDED(window_pattern->Close());
        std::cout << "UIA12 modal window: " << (window ? "PASS" : "FAIL") << '\n'; pass &= window;
        if (window_pattern) window_pattern->Release(); if (dialog) dialog->Release();
        if (window) SendMessageW(search.window, WM_APP + 80, 7, 0);
        auto half = find_id(automation, root, L"form.title.pages.v03.half"); BOOL half_enabled = FALSE;
        auto tabs = find_id(automation, root, L"form.title.pages.tabs");
        bool modal_background = half && SUCCEEDED(half->get_CurrentIsEnabled(&half_enabled)) && half_enabled && tabs && select_id(automation, tabs, L"v01") == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        std::cout << "UIA12 background: " << (modal_background ? "PASS" : "FAIL") << '\n'; pass &= modal_background;
        if (half) half->Release(); if (tabs) tabs->Release();
        HRESULT modal_hr = modal_action ? modal_action->Invoke() : E_FAIL;
        mapping = mapping && modal_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        SendMessageW(search.window, WM_APP + 80, 8, 0);
        auto tip_events = new SelectionEvents(); tip_events->expected = UIA_ToolTipOpenedEventId;
        auto tip_subscribed = automation->AddAutomationEventHandler(UIA_ToolTipOpenedEventId, root, TreeScope_Subtree, nullptr, tip_events);
        auto tip_setup = SendMessageW(search.window, WM_APP + 80, 18, 0);
        bool tip_event = tip_setup == 0 && SUCCEEDED(tip_subscribed) && WaitForSingleObject(tip_events->received, 3000) == WAIT_OBJECT_0;
        auto tip = find_id(automation, root, L"form.title.pages.v03.half.tip"); CONTROLTYPEID tip_type = 0; BSTR tip_name = nullptr;
        bool tooltip = tip_event && tip && SUCCEEDED(tip->get_CurrentControlType(&tip_type)) && tip_type == UIA_ToolTipControlTypeId && SUCCEEDED(tip->get_CurrentName(&tip_name)) && tip_name && std::wstring(tip_name) == L"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002";
        if (!tooltip) std::cout << "tip setup=" << tip_setup << " event=" << tip_event << " element=" << bool(tip) << " type=" << tip_type << " subscribed=" << std::hex << tip_subscribed << std::dec << '\n';
        std::cout << "UIA13 tooltip: " << (tooltip ? "PASS" : "FAIL") << '\n'; pass &= tooltip;
        SysFreeString(tip_name); if (tip) tip->Release(); if (SUCCEEDED(tip_subscribed)) automation->RemoveAutomationEventHandler(UIA_ToolTipOpenedEventId, root, tip_events); tip_events->Release();
        if (modal_action) modal_action->Release(); if (modal_target) modal_target->Release();
        SendMessageW(search.window, WM_APP + 80, 5, 0);
        auto invoke_id = [&](const wchar_t* id) {
            auto target = find_id(automation, root, id); IUIAutomationInvokePattern* action = nullptr;
            HRESULT result = target ? target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&action)) : E_FAIL;
            if (SUCCEEDED(result) && action) result = action->Invoke();
            if (action) action->Release(); if (target) target->Release(); return result;
        };
        HRESULT invalid_hr = invoke_id(L"fixture.name_invalid");
        auto read_only_setup = SendMessageW(search.window, WM_APP + 80, 9, 0);
        HRESULT readonly_hr = invoke_id(L"fixture.name_write");
        SendMessageW(search.window, WM_APP + 80, 10, 0);
        auto composing_setup = SendMessageW(search.window, WM_APP + 80, 11, 0);
        HRESULT busy_hr = invoke_id(L"form.apply");
        SendMessageW(search.window, WM_APP + 80, 12, 0);
        SendMessageW(search.window, WM_APP + 80, 13, 0);
        HRESULT renderer_hr = invoke_id(L"form.apply");
        SendMessageW(search.window, WM_APP + 80, 14, 0);
        mapping = mapping && invalid_hr == E_INVALIDARG && read_only_setup == 0 && readonly_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) &&
            composing_setup == 0 && busy_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && renderer_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        if (!mapping) std::cout << "UIA15 actual disabled/hidden/modal/invalid/readonly/busy/renderer: 0x" << std::hex << disabled_hr << ' ' << hidden_hr << ' ' << modal_hr << ' ' << invalid_hr << ' ' << readonly_hr << ' ' << busy_hr << ' ' << renderer_hr << '\n';
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
    if (SUCCEEDED(focus_subscribed)) automation->RemoveFocusChangedEventHandler(focus_events); focus_events->Release();
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
