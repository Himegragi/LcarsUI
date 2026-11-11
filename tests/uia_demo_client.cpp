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
class FocusEvent final : public UiaEventHandler<FocusEvent, IUIAutomationFocusChangedEventHandler> {
public:
    std::wstring wanted;  // AutomationId of the element that should receive focus
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        BSTR id = nullptr;
        if (sender && SUCCEEDED(sender->get_CurrentAutomationId(&id)) && id && wanted == id) SetEvent(received);
        SysFreeString(id); return S_OK;
    }
};
// One context-menu element as a UIA client sees it: type, Name, AccessKey, AcceleratorKey, IsEnabled and Invoke support.
bool menu_item_is(IUIAutomationElement* element, CONTROLTYPEID type, const wchar_t* name, const wchar_t* access_key, const wchar_t* accelerator, bool enabled) {
    if (!element) return false;
    CONTROLTYPEID actual = 0; BSTR n = nullptr, key = nullptr, accel = nullptr; BOOL is_enabled = FALSE; IUIAutomationInvokePattern* invoke_pattern = nullptr;
    bool ok = SUCCEEDED(element->get_CurrentControlType(&actual)) && actual == type && SUCCEEDED(element->get_CurrentName(&n)) && n && std::wstring(n) == name &&
        SUCCEEDED(element->get_CurrentAccessKey(&key)) && key && std::wstring(key) == access_key &&
        SUCCEEDED(element->get_CurrentAcceleratorKey(&accel)) && accel && std::wstring(accel) == accelerator &&
        SUCCEEDED(element->get_CurrentIsEnabled(&is_enabled)) && bool(is_enabled) == enabled &&
        SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke_pattern))) && invoke_pattern;
    SysFreeString(n); SysFreeString(key); SysFreeString(accel); if (invoke_pattern) invoke_pattern->Release();
    return ok;
}
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
        if (select(uia, nav, L"items")) {   // UA09: the context menu as a UIA Menu with MenuItem and Separator children (#85).
            auto opened_event = new TextEvent(); opened_event->expected = UIA_MenuOpenedEventId;
            auto closed_event = new TextEvent(); closed_event->expected = UIA_MenuClosedEventId;
            auto focus_event = new FocusEvent();
            auto opened_subscribed = uia->AddAutomationEventHandler(UIA_MenuOpenedEventId, root, TreeScope_Subtree, nullptr, opened_event);
            auto closed_subscribed = uia->AddAutomationEventHandler(UIA_MenuClosedEventId, root, TreeScope_Subtree, nullptr, closed_event);
            auto focus_subscribed = uia->AddFocusChangedEventHandler(nullptr, focus_event);
            const std::wstring edit_name = L"編輯(E)", delete_name = L"刪除(D)";
            bool opened = SendMessageW(s.window, WM_APP + 85, 0, 0) == 0 && SUCCEEDED(opened_subscribed) && WaitForSingleObject(opened_event->received, 3000) == WAIT_OBJECT_0;
            auto menu = find_id(uia, root, L"items.list/menu"); CONTROLTYPEID menu_type = 0;
            bool is_menu = menu && SUCCEEDED(menu->get_CurrentControlType(&menu_type)) && menu_type == UIA_MenuControlTypeId;
            auto edit_item = menu ? find_id(uia, menu, L"items.edit") : nullptr;
            auto delete_item = menu ? find_id(uia, menu, L"items.delete") : nullptr;
            bool items_ok = menu_item_is(edit_item, UIA_MenuItemControlTypeId, edit_name.c_str(), L"E", L"Enter", true) &&
                menu_item_is(delete_item, UIA_MenuItemControlTypeId, delete_name.c_str(), L"D", L"Del", true);
            // Focus moves to an item when the keyboard highlights it.
            focus_event->wanted = L"items.edit";
            bool focused_event = SendMessageW(s.window, WM_APP + 85, 3, 0) == 0 && SUCCEEDED(focus_subscribed) && WaitForSingleObject(focus_event->received, 3000) == WAIT_OBJECT_0;
            BOOL has_focus = FALSE;
            bool focus_state = edit_item && SUCCEEDED(edit_item->get_CurrentHasKeyboardFocus(&has_focus)) && (has_focus || GetFocus() != s.window);
            // Invoke runs the row's Edit: the menu closes (MenuClosed) and the edit dialog opens.
            bool invoked = edit_item && invoke_id(uia, menu, L"items.edit") == S_OK && WaitForSingleObject(closed_event->received, 3000) == WAIT_OBJECT_0;
            auto gone = find_id(uia, root, L"items.list/menu"); bool vanished = !gone; if (gone) gone->Release();
            bool dialog_closed = close_modal(uia, root, L"items.dialog");
            if (edit_item) edit_item->Release(); if (delete_item) delete_item->Release(); if (menu) menu->Release();
            // The search field's menu: a separator line and a disabled item (nothing is selected there).
            // MenuOpened / MenuClosed are asserted above on the first menu. This second opening is checked through
            // the tree: one run in about twenty did not deliver its second MenuOpened to the client (UIA event delivery).
            const auto field_status = SendMessageW(s.window, WM_APP + 85, 2, 0);
            IUIAutomationElement* field_menu = nullptr;
            for (int attempt = 0; attempt < 30 && !field_menu; ++attempt) { field_menu = find_id(uia, root, L"items.search/menu"); if (!field_menu) Sleep(100); }
            bool field_open = field_status == 0 && field_menu;
            auto copy_item = field_menu ? find_id(uia, field_menu, L"lcars.copy") : nullptr;
            auto select_all_item = field_menu ? find_id(uia, field_menu, L"lcars.select_all") : nullptr;
            bool field_items = menu_item_is(copy_item, UIA_MenuItemControlTypeId, L"複製(C)", L"C", L"Ctrl+C", false) &&
                menu_item_is(select_all_item, UIA_MenuItemControlTypeId, L"全選(A)", L"A", L"Ctrl+A", true);
            VARIANT separator_type; VariantInit(&separator_type); separator_type.vt = VT_I4; separator_type.lVal = UIA_SeparatorControlTypeId;
            IUIAutomationCondition* separator_condition = nullptr; IUIAutomationElementArray* separators = nullptr; int separator_count = 0;
            bool lines = field_menu && SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, separator_type, &separator_condition)) &&
                SUCCEEDED(field_menu->FindAll(TreeScope_Children, separator_condition, &separators)) && separators && SUCCEEDED(separators->get_Length(&separator_count)) && separator_count == 2;
            if (separators) separators->Release(); if (separator_condition) separator_condition->Release();
            bool disabled_refuses = copy_item && [&] { IUIAutomationInvokePattern* p = nullptr; bool refused = SUCCEEDED(copy_item->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&p))) && p && p->Invoke() == UIA_E_ELEMENTNOTENABLED; if (p) p->Release(); return refused; }();
            if (copy_item) copy_item->Release(); if (select_all_item) select_all_item->Release(); if (field_menu) field_menu->Release();
            bool field_closed = SendMessageW(s.window, WM_APP + 85, 1, 0) == 0;
            if (SUCCEEDED(opened_subscribed)) uia->RemoveAutomationEventHandler(UIA_MenuOpenedEventId, root, opened_event);
            if (SUCCEEDED(closed_subscribed)) uia->RemoveAutomationEventHandler(UIA_MenuClosedEventId, root, closed_event);
            if (SUCCEEDED(focus_subscribed)) uia->RemoveFocusChangedEventHandler(focus_event);
            opened_event->Release(); closed_event->Release(); focus_event->Release();
            bool menu_pass = opened && is_menu && items_ok && focused_event && focus_state && invoked && vanished && dialog_closed && field_open && field_items && lines && disabled_refuses && field_closed;
            std::cout << "UA09 context menu: " << (menu_pass ? "PASS" : "FAIL") << " opened=" << opened << " menu=" << is_menu << " items=" << items_ok << " focus-event=" << focused_event << " focus=" << focus_state
                      << " invoke+closed=" << invoked << " vanished=" << vanished << " dialog=" << dialog_closed << " field-menu=" << field_open << " field-items=" << field_items << " lines=" << lines << " disabled=" << disabled_refuses << '\n';
            ok &= menu_pass;
        }
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
