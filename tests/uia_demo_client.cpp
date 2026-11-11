#include "uia_client_helpers.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include <atomic>
#include <iostream>
#include "uia_pattern_drivers.hpp"
class EnabledEvent final : public UiaEventHandler<EnabledEvent, IUIAutomationPropertyChangedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override { if (id == UIA_IsEnabledPropertyId && value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE) SetEvent(received); return S_OK; }
};
class ValueEvent final : public UiaEventHandler<ValueEvent, IUIAutomationPropertyChangedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override { if (id == UIA_ValueValuePropertyId && value.vt == VT_BSTR && value.bstrVal && std::wstring(value.bstrVal) == L"sample") SetEvent(received); return S_OK; }
};
class TextEvent final : public UiaEventHandler<TextEvent, IUIAutomationEventHandler> {
public:
    EVENTID expected = UIA_Text_TextChangedEventId;
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override { if (id == expected) SetEvent(received); return S_OK; }
};
class StructureEvent final : public UiaEventHandler<StructureEvent, IUIAutomationStructureChangedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType type, SAFEARRAY*) override { if (type == StructureChangeType_ChildrenInvalidated) SetEvent(received); return S_OK; }
};
bool select(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) { return SUCCEEDED(select_id(uia, root, id)); }
bool invoke(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) { return SUCCEEDED(invoke_id(uia, root, id)); }
bool close_modal(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = find_id(uia, root, id); IUIAutomationWindowPattern* p = nullptr; BOOL modal = FALSE; CONTROLTYPEID type = 0;
    bool ok = e && SUCCEEDED(e->get_CurrentControlType(&type)) && type == UIA_WindowControlTypeId && SUCCEEDED(e->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->get_CurrentIsModal(&modal)) && modal && SUCCEEDED(p->Close());
    if (p) p->Release(); if (e) e->Release();
    auto closed = find_id(uia, root, id); ok = ok && !closed; if (closed) closed->Release(); return ok;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2]; STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 1; CloseHandle(pi.hThread);
    Search s{pi.dwProcessId}; for (int i = 0; i < 100 && !s.window; ++i) { EnumWindows(find_window, reinterpret_cast<LPARAM>(&s)); Sleep(100); }
    IUIAutomation* uia = nullptr; IUIAutomationElement* root = nullptr; bool ok = s.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && SUCCEEDED(uia->ElementFromHandle(s.window, &root)) && root;
    if (ok) {
        auto layout_events = new TextEvent(); layout_events->expected = UIA_LayoutInvalidatedEventId;
        auto layout_subscribed = uia->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, TreeScope_Element, nullptr, layout_events);
        RECT outer{}; GetWindowRect(s.window, &outer); SetWindowPos(s.window, nullptr, 0, 0, outer.right - outer.left + 40, outer.bottom - outer.top + 40, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        bool layout_event = SUCCEEDED(layout_subscribed) && WaitForSingleObject(layout_events->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(layout_subscribed)) uia->RemoveAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, layout_events); layout_events->Release();
        std::cout << "layout event=" << layout_event << '\n'; ok &= layout_event;
        auto save = find_id(uia, root, L"app.save"); BSTR accelerator = nullptr; BOOL save_enabled = TRUE;
        bool shortcut = save && SUCCEEDED(save->get_CurrentAcceleratorKey(&accelerator)) && accelerator && std::wstring(accelerator) == L"Ctrl+S" && SUCCEEDED(save->get_CurrentIsEnabled(&save_enabled)) && !save_enabled;
        SysFreeString(accelerator); if (save) save->Release();
        std::cout << "UA03: " << (shortcut ? "PASS" : "FAIL") << '\n'; ok &= shortcut;
        auto nav = find_id(uia, root, L"app.nav"); IUIAutomationSelectionPattern* selection = nullptr; BOOL required = FALSE, multiple = TRUE; CONTROLTYPEID type = 0;
        bool navigation = nav && SUCCEEDED(nav->get_CurrentControlType(&type)) && type == UIA_ListControlTypeId && SUCCEEDED(nav->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection))) && selection && SUCCEEDED(selection->get_CurrentIsSelectionRequired(&required)) && required && SUCCEEDED(selection->get_CurrentCanSelectMultiple(&multiple)) && !multiple;
        navigation = navigation && patterns(uia, root);
        auto cached_name = find_id(uia, root, L"general.name");
        auto nav_item = find_id(uia, nav, L"items"); auto selected_event = new TextEvent(); selected_event->expected = UIA_SelectionItem_ElementSelectedEventId;
        auto selected_subscribed = nav_item ? uia->AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, nav_item, TreeScope_Element, nullptr, selected_event) : E_FAIL;
        auto structure_event = new StructureEvent(); auto structure_subscribed = uia->AddStructureChangedEventHandler(root, TreeScope_Element, nullptr, structure_event);
        bool nav_changed = select(uia, nav, L"items") && SUCCEEDED(selected_subscribed) && SUCCEEDED(structure_subscribed) && WaitForSingleObject(selected_event->received, 3000) == WAIT_OBJECT_0 && WaitForSingleObject(structure_event->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selected_subscribed)) uia->RemoveAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, nav_item, selected_event); selected_event->Release(); if (nav_item) nav_item->Release();
        if (SUCCEEDED(structure_subscribed)) uia->RemoveStructureChangedEventHandler(root, structure_event); structure_event->Release();
        std::cout << "UA01: " << (navigation && nav_changed ? "PASS" : "FAIL") << '\n';
        auto hidden_name = find_id(uia, root, L"general.name"); navigation = navigation && nav_changed && !hidden_name && patterns(uia, root); if (hidden_name) hidden_name->Release();
        IUIAutomationValuePattern* cached_value = nullptr; BSTR cached_text = nullptr;
        bool ua02 = !hidden_name && cached_name && SUCCEEDED(cached_name->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&cached_value))) && cached_value && SUCCEEDED(cached_value->get_CurrentValue(&cached_text)) && cached_value->SetValue(L"hidden edit") == UIA_E_INVALIDOPERATION;
        SysFreeString(cached_text); if (cached_value) cached_value->Release(); if (cached_name) cached_name->Release();
        std::cout << "UA02: " << (ua02 ? "PASS" : "FAIL") << '\n'; ok &= ua02;
        bool item_modal = invoke(uia, root, L"items.add") && close_modal(uia, root, L"items.dialog");
        std::cout << "UA04 item dialog: " << (item_modal ? "PASS" : "FAIL") << '\n'; ok &= item_modal;
        // #79: the dialog's note is a three-line multi-line field: TextPattern reads it by line, ScrollPattern scrolls it.
        {
            bool note = invoke(uia, root, L"items.add");
            auto field = note ? find_id(uia, root, L"items.dialog.note") : nullptr;
            IUIAutomationValuePattern* note_value = nullptr; IUIAutomationTextPattern* note_text = nullptr; IUIAutomationScrollPattern* note_scroll = nullptr;
            note = note && field && SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&note_value))) && note_value && SUCCEEDED(note_value->SetValue(L"l1\nl2\nl3\nl4\nl5\nl6"));
            RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            BSTR whole = nullptr;
            note = note && SUCCEEDED(field->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&note_text))) && note_text;
            IUIAutomationTextRange* range = nullptr; BSTR first_line = nullptr, second_line = nullptr, last_line = nullptr; int moved = 0;
            note = note && SUCCEEDED(note_text->get_DocumentRange(&range)) && range && SUCCEEDED(range->GetText(-1, &whole)) && whole && std::wstring(whole) == L"l1\nl2\nl3\nl4\nl5\nl6" &&
                SUCCEEDED(range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range, TextPatternRangeEndpoint_Start)) && SUCCEEDED(range->ExpandToEnclosingUnit(TextUnit_Line)) &&
                SUCCEEDED(range->GetText(-1, &first_line)) && first_line && std::wstring(first_line) == L"l1\n" &&
                SUCCEEDED(range->Move(TextUnit_Line, 1, &moved)) && moved == 1 && SUCCEEDED(range->GetText(-1, &second_line)) && second_line && std::wstring(second_line) == L"l2\n" &&
                SUCCEEDED(range->Move(TextUnit_Line, 10, &moved)) && moved == 4 && SUCCEEDED(range->GetText(-1, &last_line)) && last_line && std::wstring(last_line) == L"l6";
            BOOL vertical = FALSE, horizontal = TRUE; double percent = -1, view = -1;
            note = note && SUCCEEDED(field->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&note_scroll))) && note_scroll &&
                SUCCEEDED(note_scroll->get_CurrentVerticallyScrollable(&vertical)) && vertical && SUCCEEDED(note_scroll->get_CurrentHorizontallyScrollable(&horizontal)) && !horizontal &&
                SUCCEEDED(note_scroll->get_CurrentVerticalViewSize(&view)) && view > 49.9 && view < 50.1 &&
                SUCCEEDED(note_scroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 99.9 &&
                SUCCEEDED(note_scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 0)) && SUCCEEDED(note_scroll->get_CurrentVerticalScrollPercent(&percent)) && percent == 0 &&
                SUCCEEDED(note_scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100)) && SUCCEEDED(note_scroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 99.9 &&
                SUCCEEDED(note_scroll->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallDecrement)) && SUCCEEDED(note_scroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 66 && percent < 67 &&
                note_scroll->Scroll(ScrollAmount_SmallIncrement, ScrollAmount_NoAmount) == UIA_E_INVALIDOPERATION;
            SysFreeString(whole); SysFreeString(first_line); SysFreeString(second_line); SysFreeString(last_line);
            if (range) range->Release(); if (note_scroll) note_scroll->Release(); if (note_text) note_text->Release(); if (note_value) note_value->Release(); if (field) field->Release();
            note = close_modal(uia, root, L"items.dialog") && note;
            std::cout << "UA08 multi-line note: " << (note ? "PASS" : "FAIL") << '\n'; ok &= note;
        }
        // #81: the rich note's TextPattern reports the weight, slant, underline and foreground color of each range as
        // the field draws them now (a color name resolved for the current theme); a mixed range reports the mixed value.
        {
            bool rich = invoke(uia, root, L"items.add");
            auto field = rich ? find_id(uia, root, L"items.dialog.note") : nullptr;
            rich = rich && field && SendMessageW(s.window, WM_APP + 85, 1, 0) == 0;
            RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            IUIAutomationTextPattern* pattern = nullptr; IUIAutomationTextRange* whole = nullptr;
            rich = rich && SUCCEEDED(field->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))) && pattern && SUCCEEDED(pattern->get_DocumentRange(&whole)) && whole;
            auto slice = [&](int from, int count) -> IUIAutomationTextRange* {
                IUIAutomationTextRange* r = nullptr; int moved = 0;
                if (!whole || FAILED(whole->Clone(&r)) || !r) return nullptr;
                if (FAILED(r->MoveEndpointByRange(TextPatternRangeEndpoint_End, r, TextPatternRangeEndpoint_Start)) || FAILED(r->Move(TextUnit_Character, from, &moved)) ||
                    FAILED(r->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, count, &moved)) || moved != count) { r->Release(); return nullptr; }
                return r;
            };
            auto read = [&](IUIAutomationTextRange* range, TEXTATTRIBUTEID id, VARIANT& out) { VariantInit(&out); return range && SUCCEEDED(range->GetAttributeValue(id, &out)); };
            IUIAutomationTextRange* bold_range = rich ? slice(0, 2) : nullptr; IUIAutomationTextRange* italic_range = rich ? slice(2, 2) : nullptr; IUIAutomationTextRange* red_range = rich ? slice(4, 2) : nullptr;
            VARIANT weight, slant, line, plain_slant, plain_line, plain_weight, red, plain_light, mixed_weight, mixed_color;
            IUnknown* reserved_mixed = nullptr;
            rich = rich && bold_range && italic_range && red_range && SUCCEEDED(UiaGetReservedMixedAttributeValue(&reserved_mixed));
            rich = rich && read(bold_range, UIA_FontWeightAttributeId, weight) && weight.vt == VT_I4 && weight.lVal == 700 &&
                read(bold_range, UIA_IsItalicAttributeId, plain_slant) && plain_slant.vt == VT_BOOL && plain_slant.boolVal == VARIANT_FALSE &&
                read(bold_range, UIA_UnderlineStyleAttributeId, plain_line) && plain_line.vt == VT_I4 && plain_line.lVal == 0 &&
                read(italic_range, UIA_IsItalicAttributeId, slant) && slant.vt == VT_BOOL && slant.boolVal == VARIANT_TRUE &&
                read(italic_range, UIA_UnderlineStyleAttributeId, line) && line.vt == VT_I4 && line.lVal == 1 &&
                read(italic_range, UIA_FontWeightAttributeId, plain_weight) && plain_weight.vt == VT_I4 && plain_weight.lVal == 400;
            rich = rich && read(red_range, UIA_ForegroundColorAttributeId, red) && red.vt == VT_I4 && red.lVal == (176 | (0 << 8) | (32 << 16)) &&
                read(italic_range, UIA_ForegroundColorAttributeId, plain_light) && plain_light.vt == VT_I4;
            rich = rich && read(whole, UIA_FontWeightAttributeId, mixed_weight) && mixed_weight.vt == VT_UNKNOWN && mixed_weight.punkVal == reserved_mixed &&
                read(whole, UIA_ForegroundColorAttributeId, mixed_color) && mixed_color.vt == VT_UNKNOWN && mixed_color.punkVal == reserved_mixed;
            // FindAttribute walks the runs of equal value.
            VARIANT wanted; VariantInit(&wanted); wanted.vt = VT_BOOL; wanted.boolVal = VARIANT_TRUE;
            IUIAutomationTextRange* found = nullptr; BSTR found_text = nullptr;
            rich = rich && SUCCEEDED(whole->FindAttribute(UIA_IsItalicAttributeId, wanted, FALSE, &found)) && found && SUCCEEDED(found->GetText(-1, &found_text)) && found_text && std::wstring(found_text) == L"cd";
            // The dark theme repaints the same names in their dark values; the normal text color follows the theme too.
            bool themed = rich && SendMessageW(s.window, WM_APP + 85, 2, 1) == 0;
            RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            VARIANT dark_red, plain_dark;
            themed = themed && read(red_range, UIA_ForegroundColorAttributeId, dark_red) && dark_red.vt == VT_I4 && dark_red.lVal == (255 | (138 << 8) | (148 << 16)) &&
                read(italic_range, UIA_ForegroundColorAttributeId, plain_dark) && plain_dark.vt == VT_I4 && plain_dark.lVal != plain_light.lVal;
            SendMessageW(s.window, WM_APP + 85, 2, 0);
            // A plain (non-rich) text field reports no such attributes.
            auto search_field = find_id(uia, root, L"items.dialog.name"); IUIAutomationTextPattern* name_pattern = nullptr; IUIAutomationTextRange* name_range = nullptr; VARIANT unsupported;
            bool plain_unsupported = search_field && SUCCEEDED(search_field->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&name_pattern))) && name_pattern && SUCCEEDED(name_pattern->get_DocumentRange(&name_range)) && name_range &&
                read(name_range, UIA_FontWeightAttributeId, unsupported) && unsupported.vt == VT_UNKNOWN;
            if (search_field) search_field->Release(); if (name_pattern) name_pattern->Release(); if (name_range) name_range->Release();
            SysFreeString(found_text); if (found) found->Release(); if (bold_range) bold_range->Release(); if (italic_range) italic_range->Release(); if (red_range) red_range->Release();
            if (whole) whole->Release(); if (pattern) pattern->Release(); if (field) field->Release();
            rich = close_modal(uia, root, L"items.dialog") && rich;
            std::cout << "UA09 rich text attributes: " << (rich && themed && plain_unsupported ? "PASS" : "FAIL") << " (values=" << rich << " theme=" << themed << " plain=" << plain_unsupported << ")\n"; ok &= rich && themed && plain_unsupported;
        }
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
        auto changed = SendMessageW(s.window, WM_APP + 81, 1, 0);
        bool text_event = changed == 0 && SUCCEEDED(text_subscribed) && WaitForSingleObject(text_events->received, 2000) == WAIT_OBJECT_0;
        bool value_event = SUCCEEDED(value_subscribed) && WaitForSingleObject(value_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(value_subscribed)) uia->RemovePropertyChangedEventHandler(search, value_events); value_events->Release();
        std::cout << "value event=" << value_event << '\n'; navigation &= value_event;
        if (SUCCEEDED(text_subscribed)) uia->RemoveAutomationEventHandler(UIA_Text_TextChangedEventId, search, text_events); text_events->Release();
        auto selection_events = new TextEvent(); selection_events->expected = UIA_Text_TextSelectionChangedEventId;
        auto selection_subscribed = search ? uia->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, search, TreeScope_Element, nullptr, selection_events) : E_FAIL;
        auto selection_changed = SendMessageW(s.window, WM_APP + 82, 0, 0);
        bool selection_event = selection_changed == 0 && SUCCEEDED(selection_subscribed) && WaitForSingleObject(selection_events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selection_subscribed)) uia->RemoveAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, search, selection_events); selection_events->Release(); if (search) search->Release();
        std::cout << "text selection event=" << selection_event << '\n'; navigation &= selection_event;
        SendMessageW(s.window, WM_APP + 81, 0, 0); std::cout << "text event=" << text_event << '\n'; navigation &= text_event;
        navigation = navigation && select(uia, nav, L"appearance") && patterns(uia, root);
        auto font = find_id(uia, root, L"appearance.font"); IUIAutomationExpandCollapsePattern* expand = nullptr;
        auto default_option = font ? find_id(uia, font, L"default") : nullptr; BOOL closed_offscreen = FALSE;
        bool collapsed = default_option && SUCCEEDED(default_option->get_CurrentIsOffscreen(&closed_offscreen)) && closed_offscreen;
        bool dropdown = font && SUCCEEDED(font->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand && SUCCEEDED(expand->Expand());
        IUIAutomationCondition* all_options = nullptr; IUIAutomationElementArray* option_nodes = nullptr; int option_count = 0;
        bool all_present = font && SUCCEEDED(uia->CreateTrueCondition(&all_options)) && all_options && SUCCEEDED(font->FindAll(TreeScope_Children, all_options, &option_nodes)) && option_nodes && SUCCEEDED(option_nodes->get_Length(&option_count)) && option_count == 151;
        if (option_nodes) option_nodes->Release(); if (all_options) all_options->Release(); dropdown = dropdown && all_present;
        auto last = font ? find_id(uia, font, L"family:Font149") : nullptr; BOOL offscreen = FALSE;
        dropdown = dropdown && last && SUCCEEDED(last->get_CurrentIsOffscreen(&offscreen)) && offscreen && patterns(uia, root);
        if (expand) { collapsed = collapsed && SUCCEEDED(expand->Collapse()); closed_offscreen = FALSE; collapsed = collapsed && SUCCEEDED(default_option->get_CurrentIsOffscreen(&closed_offscreen)) && closed_offscreen; }
        std::cout << "UA07: " << (dropdown ? "PASS" : "FAIL") << '\n';
        dropdown = dropdown && collapsed; if (default_option) default_option->Release();
        if (last) last->Release(); if (expand) expand->Release(); if (font) font->Release();
        bool about = select(uia, nav, L"about"); RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); about = about && patterns(uia, root); auto text = find_id(uia, root, L"about.license_text"); BSTR name = nullptr; type = 0; BOOL focusable = FALSE;
        about = about && text && SUCCEEDED(text->get_CurrentControlType(&type)) && type == UIA_TextControlTypeId && SUCCEEDED(text->get_CurrentName(&name)) && name && SysStringLen(name) == 3009 && SUCCEEDED(text->get_CurrentIsKeyboardFocusable(&focusable)) && focusable;
        if (!about) std::cout << "about details element=" << bool(text) << " type=" << type << " name-length=" << (name ? SysStringLen(name) : 0) << " focusable=" << focusable << '\n';
        SysFreeString(name); if (text) text->Release();
        std::cout << "navigation=" << navigation << " long-dropdown=" << dropdown << " about-text=" << about << '\n'; ok = ok && navigation && dropdown && about;
        bool general = select(uia, nav, L"general"); BSTR before_title = nullptr, after_title = nullptr;
        const std::wstring expected_title = std::wstring(argv[2]) == L"material" ? L"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Material-inspired" : L"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Minimal Web";
        bool title = general && SUCCEEDED(root->get_CurrentName(&before_title)) && before_title && std::wstring(before_title) == expected_title;
        auto notices = find_id(uia, root, L"general.notices"); IUIAutomationTogglePattern* toggle = nullptr;
        title = title && notices && SUCCEEDED(notices->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle && SUCCEEDED(toggle->Toggle()) && SUCCEEDED(root->get_CurrentName(&after_title)) && before_title && after_title && std::wstring(after_title) == L"*" + std::wstring(before_title);
        SysFreeString(before_title); SysFreeString(after_title); if (toggle) toggle->Release(); if (notices) notices->Release();
        std::cout << "UA06: " << (title ? "PASS" : "FAIL") << '\n'; ok &= title;
        IUIAutomationWindowPattern* window = nullptr;
        bool close_ready = SUCCEEDED(root->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&window))) && window;
        bool close_dialog = close_ready && SUCCEEDED(window->Close()) && close_modal(uia, root, L"app.close_dialog");
        std::cout << "UA04 close dialog: " << (close_dialog ? "PASS" : "FAIL") << '\n'; ok &= close_dialog;
        auto opened = new TextEvent(); opened->expected = UIA_Window_WindowOpenedEventId;
        auto opened_subscribed = uia->AddAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, TreeScope_Subtree, nullptr, opened);
        bool discard = close_ready && SUCCEEDED(opened_subscribed) && SUCCEEDED(window->Close()) && WaitForSingleObject(opened->received, 3000) == WAIT_OBJECT_0 && invoke(uia, root, L"app.close_dialog.discard");
        if (SUCCEEDED(opened_subscribed)) uia->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, opened); opened->Release(); if (window) window->Release();
        std::cout << "UA05: " << (discard ? "PASS" : "FAIL") << '\n'; ok &= discard;
        if (selection) selection->Release(); if (nav) nav->Release();
    }
    if (root) root->Release(); if (uia) uia->Release(); if (s.window) PostMessageW(s.window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); ok = false; } DWORD exit_code = 1; if (!GetExitCodeProcess(pi.hProcess, &exit_code) || exit_code != 0) ok = false; CloseHandle(pi.hProcess); CoUninitialize(); return ok ? 0 : 1;
}
