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

class TextEvents final : public IUIAutomationEventHandler {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    EVENTID expected = UIA_Text_TextSelectionChangedEventId;
    ~TextEvents() { CloseHandle(received); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { if (!out) return E_POINTER; *out = nullptr; if (iid != __uuidof(IUnknown) && iid != __uuidof(IUIAutomationEventHandler)) return E_NOINTERFACE; *out = static_cast<IUIAutomationEventHandler*>(this); AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n=--refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override { if (id==expected) SetEvent(received); return S_OK; }
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
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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
    if (pass) {
        IUIAutomationValuePattern* value_pattern = nullptr;
        HRESULT pattern_hr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern));
        auto applied = find_id(automation, root, L"result.applied");
        BSTR applied_before = nullptr, applied_after = nullptr;
        HRESULT before_hr = applied ? applied->get_CurrentName(&applied_before) : E_FAIL;
        BSTR input = SysAllocString(L"\u5de5\u4f5c\u7ad9 A"), actual = nullptr;
        HRESULT set_hr = value_pattern ? value_pattern->SetValue(input) : E_FAIL;
        HRESULT read_hr = value_pattern ? value_pattern->get_CurrentValue(&actual) : E_FAIL;
        HRESULT after_hr = applied ? applied->get_CurrentName(&applied_after) : E_FAIL;
        SendMessageW(search.window, WM_APP + 80, 9, 0);
        BOOL read_only = FALSE;
        HRESULT readonly_hr = value_pattern ? value_pattern->get_CurrentIsReadOnly(&read_only) : E_FAIL;
        HRESULT rejected = value_pattern ? value_pattern->SetValue(input) : E_FAIL;
        SendMessageW(search.window, WM_APP + 80, 10, 0);
        bool value_ok = SUCCEEDED(before_hr) && SUCCEEDED(after_hr) && applied_before && applied_after &&
            std::wstring(applied_before, SysStringLen(applied_before)) == std::wstring(applied_after, SysStringLen(applied_after)) && SUCCEEDED(pattern_hr) && SUCCEEDED(set_hr) && SUCCEEDED(read_hr) && actual &&
            std::wstring(actual, SysStringLen(actual)) == L"\u5de5\u4f5c\u7ad9 A" && SUCCEEDED(readonly_hr) && read_only && rejected == UIA_E_INVALIDOPERATION;
        std::cout << "UIA07 Value: " << (value_ok ? "PASS" : "FAIL") << '\n';
        pass &= value_ok; SysFreeString(applied_before); SysFreeString(applied_after); if (applied) applied->Release(); SysFreeString(input); SysFreeString(actual); if (value_pattern) value_pattern->Release();
    }
    if (pass) {
        SendMessageW(search.window, WM_APP + 80, 17, 14);
        IUIAutomationTextPattern* pattern = nullptr; IUIAutomationTextRange* range = nullptr;
        HRESULT ph = element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern));
        HRESULT rh = pattern ? pattern->get_DocumentRange(&range) : E_FAIL;
        BSTR content = nullptr; HRESULT th = range ? range->GetText(-1, &content) : E_FAIL;
        bool text_ok = SUCCEEDED(ph) && SUCCEEDED(rh) && SUCCEEDED(th) && content &&
            std::wstring(content, SysStringLen(content)) == L"\u5de5\u4f5c\u7ad9 A";
        auto events = new TextEvents();
        HRESULT subscribed = automation->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, element, TreeScope_Element, nullptr, events);
        int moved = 0; HRESULT move_hr = range ? range->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -3, &moved) : E_FAIL;
        HRESULT select_hr = range ? range->Select() : E_FAIL;
        auto selection = SendMessageW(search.window, WM_APP + 80, 18, 0);
        bool selected = SUCCEEDED(move_hr) && moved == -3 && SUCCEEDED(select_hr) && LOWORD(selection) == 0 && HIWORD(selection) == 2 && SUCCEEDED(subscribed) && WaitForSingleObject(events->received, 2000) == WAIT_OBJECT_0;
        IUIAutomationTextRangeArray* selected_ranges = nullptr; IUIAutomationTextRange* selected_range = nullptr;
        int selected_count = 0, start_cmp = 1, end_cmp = 1;
        if (pattern && SUCCEEDED(pattern->GetSelection(&selected_ranges)) && selected_ranges) {
            selected_ranges->get_Length(&selected_count);
            if (selected_count == 1 && SUCCEEDED(selected_ranges->GetElement(0, &selected_range)) && selected_range && range) {
                selected_range->CompareEndpoints(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_Start, &start_cmp);
                selected_range->CompareEndpoints(TextPatternRangeEndpoint_End, range, TextPatternRangeEndpoint_End, &end_cmp);
            }
        }
        selected = selected && selected_count == 1 && start_cmp == 0 && end_cmp == 0;
        if (selected_range) selected_range->Release(); if (selected_ranges) selected_ranges->Release();
        bool whole_units = true;
        for (auto unit : {TextUnit_Word, TextUnit_Line, TextUnit_Document}) {
            IUIAutomationTextRange* expanded = nullptr; BSTR all = nullptr;
            HRESULT eh = range ? range->Clone(&expanded) : E_FAIL;
            if (SUCCEEDED(eh) && expanded) eh = expanded->ExpandToEnclosingUnit(unit);
            if (SUCCEEDED(eh) && expanded) eh = expanded->GetText(-1, &all);
            whole_units = whole_units && SUCCEEDED(eh) && all && std::wstring(all, SysStringLen(all)) == L"\u5de5\u4f5c\u7ad9 A";
            SysFreeString(all); if (expanded) expanded->Release();
        }
        IUIAutomationTextRange* first = nullptr; if (range) range->Clone(&first);
        HRESULT expand = first ? first->ExpandToEnclosingUnit(TextUnit_Character) : E_FAIL;
        SAFEARRAY* rects = nullptr; HRESULT rect_hr = first ? first->GetBoundingRectangles(&rects) : E_FAIL;
        LONG lo=0, hi=-1; if (rects) { SafeArrayGetLBound(rects,1,&lo); SafeArrayGetUBound(rects,1,&hi); }
        double rect[4]{}; if (hi-lo+1==4) for (LONG i=0;i<4;++i) SafeArrayGetElement(rects,&i,&rect[i]);
        IUIAutomationTextRange* hit = nullptr; POINT point{static_cast<LONG>(rect[0]+rect[2]/2), static_cast<LONG>(rect[1]+rect[3]/2)};
        HRESULT hit_hr = pattern ? pattern->RangeFromPoint(point,&hit) : E_FAIL;
        int cmp=99; if (hit && first) hit->CompareEndpoints(TextPatternRangeEndpoint_Start,first,TextPatternRangeEndpoint_Start,&cmp);
        auto pos0=SendMessageW(search.window, WM_APP + 80, 19, 0), pos1=SendMessageW(search.window, WM_APP + 80, 19, 1);
        bool matches_paint = std::abs(rect[0]-static_cast<short>(LOWORD(pos0))) <= 1 && std::abs(rect[2]-(static_cast<short>(LOWORD(pos1))-static_cast<short>(LOWORD(pos0)))) <= 1 && std::abs(rect[1]-static_cast<short>(HIWORD(pos0))) <= 1;
        std::cout << "rect=" << rect[0] << "," << rect[1] << "," << rect[2] << "," << rect[3] << " paint=" << static_cast<short>(LOWORD(pos0)) << "," << static_cast<short>(HIWORD(pos0)) << "," << static_cast<short>(LOWORD(pos1)) << " cmp=" << cmp << "\n";
        RECT field_bounds{}; element->get_CurrentBoundingRectangle(&field_bounds);
        bool inside = rect[0] >= field_bounds.left && rect[1] >= field_bounds.top && rect[0]+rect[2] <= field_bounds.right && rect[1]+rect[3] <= field_bounds.bottom;
        bool geometry = inside && matches_paint && SUCCEEDED(expand) && SUCCEEDED(rect_hr) && hi-lo+1==4 && rect[2]>0 && rect[3]>0 && SUCCEEDED(hit_hr) && cmp==0;
        if (rects) SafeArrayDestroy(rects); rects=nullptr;
        auto layout_events = new TextEvents(); layout_events->expected = UIA_LayoutInvalidatedEventId;
        HRESULT layout_subscribed = automation->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, TreeScope_Element, nullptr, layout_events);
        SendMessageW(search.window, WM_APP + 80, 17, 28);
        HRESULT resized_hr = first ? first->GetBoundingRectangles(&rects) : E_FAIL;
        double resized[4]{}; hi=-1; if (rects) { SafeArrayGetUBound(rects,1,&hi); if (hi==3) for (LONG i=0;i<4;++i) SafeArrayGetElement(rects,&i,&resized[i]); }
        auto new0=SendMessageW(search.window, WM_APP + 80, 19, 0), new1=SendMessageW(search.window, WM_APP + 80, 19, 1);
        RECT resized_field{}; HRESULT field_hr = element->get_CurrentBoundingRectangle(&resized_field);
        bool resized_ok = SUCCEEDED(field_hr) && resized_field.bottom - resized_field.top > field_bounds.bottom - field_bounds.top && SUCCEEDED(resized_hr) && hi==3 && resized[3] > rect[3] && std::abs(resized[0]-static_cast<short>(LOWORD(new0))) <= 1 && std::abs(resized[2]-(static_cast<short>(LOWORD(new1))-static_cast<short>(LOWORD(new0)))) <= 1;
        auto layout_wait = WaitForSingleObject(layout_events->received, 2000);
        std::cout << "resized=" << resized[0] << "," << resized[1] << "," << resized[2] << "," << resized[3] << " paint=" << static_cast<short>(LOWORD(new0)) << "," << static_cast<short>(LOWORD(new1)) << " layout=" << layout_wait << "\n";
        resized_ok = resized_ok && SUCCEEDED(layout_subscribed) && layout_wait == WAIT_OBJECT_0;
        if (SUCCEEDED(layout_subscribed)) automation->RemoveAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, layout_events);
        layout_events->Release();
        SendMessageW(search.window, WM_APP + 80, 17, 16);
        IUIAutomationValuePattern* change = nullptr;
        if (element) element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&change));
        BSTR replacement = SysAllocString(L"A\U0001f680B");
        HRESULT change_hr = change ? change->SetValue(replacement) : E_FAIL;
        BSTR stale = nullptr; HRESULT stale_hr = range ? range->GetText(-1, &stale) : E_FAIL;
        IUIAutomationTextRange* unicode_range = nullptr; if (pattern) pattern->get_DocumentRange(&unicode_range);
        int unicode_moved = 0; HRESULT unicode_hr = unicode_range ? unicode_range->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -2, &unicode_moved) : E_FAIL;
        BSTR unicode = nullptr; if (unicode_range) unicode_range->GetText(-1, &unicode);
        bool revision_ok = SUCCEEDED(change_hr) && stale_hr == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && SUCCEEDED(unicode_hr) && unicode_moved == -2 && unicode && std::wstring(unicode, SysStringLen(unicode)) == L"A";
        BSTR original = SysAllocString(L"\u5de5\u4f5c\u7ad9 A");
        if (change) change->SetValue(original);
        BSTR stale_again = nullptr; auto stale_again_hr = range ? range->GetText(-1, &stale_again) : E_FAIL;
        revision_ok = revision_ok && stale_again_hr == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
        auto ime_setup = SendMessageW(search.window, WM_APP + 80, 11, 0);
        IUIAutomationTextRange* composing_range = nullptr; if (pattern) pattern->get_DocumentRange(&composing_range);
        BSTR committed = nullptr; HRESULT committed_hr = composing_range ? composing_range->GetText(-1, &committed) : E_FAIL;
        HRESULT busy_hr = change ? change->SetValue(original) : E_FAIL;
        bool composition_ok = ime_setup == 0 && SUCCEEDED(committed_hr) && committed && std::wstring(committed, SysStringLen(committed)) == L"\u5de5\u4f5c\u7ad9 A" && busy_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        SendMessageW(search.window, WM_APP + 80, 12, 0); SysFreeString(committed); if (composing_range) composing_range->Release();
        SysFreeString(stale_again); SysFreeString(original);
        SysFreeString(replacement); SysFreeString(stale); SysFreeString(unicode); if (unicode_range) unicode_range->Release(); if (change) change->Release();
        text_ok = text_ok && selected && geometry && resized_ok && revision_ok && whole_units && composition_ok;
        std::cout << "UIA08 selection=" << selected << " geometry=" << geometry << " UIA17 size=" << resized_ok << '\n';
        if (SUCCEEDED(subscribed)) automation->RemoveAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, element, events);
        events->Release(); if (rects) SafeArrayDestroy(rects); if (first) first->Release(); if (hit) hit->Release();
        std::cout << "UIA08 DocumentRange: " << (text_ok ? "PASS" : "FAIL") << '\n';
        pass &= text_ok; SysFreeString(content); if (range) range->Release(); if (pattern) pattern->Release();
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
            std::wstring(before, SysStringLen(before)) == L"套用次數：0" &&
            std::wstring(after, SysStringLen(after)) == L"套用次數：1";
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
