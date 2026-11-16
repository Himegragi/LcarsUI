#include "uia_demo_messages.hpp"
#include "uia_client_helpers.hpp"
#include "vertical_expectations.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include <atomic>
#include <iostream>
#include "uia_pattern_drivers.hpp"
#include "text_unit_expectations.hpp"
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
// For the hover check, which wants silence: any event of the kind at all counts.
class AnyPropertyEvent final : public UiaEventHandler<AnyPropertyEvent, IUIAutomationPropertyChangedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID, VARIANT) override { SetEvent(received); return S_OK; }
};
class AnyAutomationEvent final : public UiaEventHandler<AnyAutomationEvent, IUIAutomationEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID) override { SetEvent(received); return S_OK; }
};
class AnyStructureEvent final : public UiaEventHandler<AnyStructureEvent, IUIAutomationStructureChangedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType, SAFEARRAY*) override { SetEvent(received); return S_OK; }
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
bool menuItemIs(IUIAutomationElement* element, CONTROLTYPEID type, const wchar_t* name, const wchar_t* accessKey, const wchar_t* accelerator, bool enabled) {
    if (!element) return false;
    CONTROLTYPEID actual = 0; BSTR n = nullptr, key = nullptr, accel = nullptr; BOOL isEnabled = FALSE; IUIAutomationInvokePattern* invokePattern = nullptr;
    bool ok = SUCCEEDED(element->get_CurrentControlType(&actual)) && actual == type && SUCCEEDED(element->get_CurrentName(&n)) && n && std::wstring(n) == name &&
        SUCCEEDED(element->get_CurrentAccessKey(&key)) && key && std::wstring(key) == accessKey &&
        SUCCEEDED(element->get_CurrentAcceleratorKey(&accel)) && accel && std::wstring(accel) == accelerator &&
        SUCCEEDED(element->get_CurrentIsEnabled(&isEnabled)) && bool(isEnabled) == enabled &&
        SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invokePattern))) && invokePattern;
    SysFreeString(n); SysFreeString(key); SysFreeString(accel); if (invokePattern) invokePattern->Release();
    return ok;
}
bool select(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) { return SUCCEEDED(selectId(uia, root, id)); }
bool invoke(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) { return SUCCEEDED(invokeId(uia, root, id)); }
bool closeModal(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    auto e = findId(uia, root, id); IUIAutomationWindowPattern* p = nullptr; BOOL modal = FALSE; CONTROLTYPEID type = 0;
    bool ok = e && SUCCEEDED(e->get_CurrentControlType(&type)) && type == UIA_WindowControlTypeId && SUCCEEDED(e->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->get_CurrentIsModal(&modal)) && modal && SUCCEEDED(p->Close());
    if (p) p->Release(); if (e) e->Release();
    auto closed = findId(uia, root, id); ok = ok && !closed; if (closed) closed->Release(); return ok;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2]; STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 1; CloseHandle(pi.hThread);
    Search s{pi.dwProcessId}; for (int i = 0; i < 100 && !s.window; ++i) { EnumWindows(findWindow, reinterpret_cast<LPARAM>(&s)); Sleep(100); }
    IUIAutomation* uia = nullptr; IUIAutomationElement* root = nullptr; bool ok = s.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && SUCCEEDED(uia->ElementFromHandle(s.window, &root)) && root;
    if (ok) {
        auto layoutEvents = new TextEvent(); layoutEvents->expected = UIA_LayoutInvalidatedEventId;
        auto layoutSubscribed = uia->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, TreeScope_Element, nullptr, layoutEvents);
        RECT outer{}; GetWindowRect(s.window, &outer); SetWindowPos(s.window, nullptr, 0, 0, outer.right - outer.left + 40, outer.bottom - outer.top + 40, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        bool layoutEvent = SUCCEEDED(layoutSubscribed) && WaitForSingleObject(layoutEvents->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(layoutSubscribed)) uia->RemoveAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, layoutEvents); layoutEvents->Release();
        std::cout << "layout event=" << layoutEvent << '\n'; ok &= layoutEvent;
        auto save = findId(uia, root, L"app.save"); BSTR accelerator = nullptr; BOOL saveEnabled = TRUE;
        bool shortcut = save && SUCCEEDED(save->get_CurrentAcceleratorKey(&accelerator)) && accelerator && std::wstring(accelerator) == L"Ctrl+S" && SUCCEEDED(save->get_CurrentIsEnabled(&saveEnabled)) && !saveEnabled;
        SysFreeString(accelerator); if (save) save->Release();
        std::cout << "UA03: " << (shortcut ? "PASS" : "FAIL") << '\n'; ok &= shortcut;
        auto nav = findId(uia, root, L"app.nav"); IUIAutomationSelectionPattern* selection = nullptr; BOOL required = FALSE, multiple = TRUE; CONTROLTYPEID type = 0;
        bool navigation = nav && SUCCEEDED(nav->get_CurrentControlType(&type)) && type == UIA_ListControlTypeId && SUCCEEDED(nav->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection))) && selection && SUCCEEDED(selection->get_CurrentIsSelectionRequired(&required)) && required && SUCCEEDED(selection->get_CurrentCanSelectMultiple(&multiple)) && !multiple;
        navigation = navigation && patterns(uia, root);
        auto cachedName = findId(uia, root, L"general.name");
        auto navItem = findId(uia, nav, L"items"); auto selectedEvent = new TextEvent(); selectedEvent->expected = UIA_SelectionItem_ElementSelectedEventId;
        auto selectedSubscribed = navItem ? uia->AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, navItem, TreeScope_Element, nullptr, selectedEvent) : E_FAIL;
        auto structureEvent = new StructureEvent(); auto structureSubscribed = uia->AddStructureChangedEventHandler(root, TreeScope_Element, nullptr, structureEvent);
        bool navChanged = select(uia, nav, L"items") && SUCCEEDED(selectedSubscribed) && SUCCEEDED(structureSubscribed) && WaitForSingleObject(selectedEvent->received, 3000) == WAIT_OBJECT_0 && WaitForSingleObject(structureEvent->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selectedSubscribed)) uia->RemoveAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, navItem, selectedEvent); selectedEvent->Release(); if (navItem) navItem->Release();
        if (SUCCEEDED(structureSubscribed)) uia->RemoveStructureChangedEventHandler(root, structureEvent); structureEvent->Release();
        std::cout << "UA01: " << (navigation && navChanged ? "PASS" : "FAIL") << '\n';
        auto hiddenName = findId(uia, root, L"general.name"); navigation = navigation && navChanged && !hiddenName && patterns(uia, root); if (hiddenName) hiddenName->Release();
        IUIAutomationValuePattern* cachedValue = nullptr; BSTR cachedText = nullptr;
        bool ua02 = !hiddenName && cachedName && SUCCEEDED(cachedName->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&cachedValue))) && cachedValue && SUCCEEDED(cachedValue->get_CurrentValue(&cachedText)) && cachedValue->SetValue(L"hidden edit") == UIA_E_INVALIDOPERATION;
        SysFreeString(cachedText); if (cachedValue) cachedValue->Release(); if (cachedName) cachedName->Release();
        std::cout << "UA02: " << (ua02 ? "PASS" : "FAIL") << '\n'; ok &= ua02;
        bool itemModal = invoke(uia, root, L"items.add") && closeModal(uia, root, L"items.dialog");
        std::cout << "UA04 item dialog: " << (itemModal ? "PASS" : "FAIL") << '\n'; ok &= itemModal;
        // #79: the dialog's note is a three-line multi-line field: TextPattern reads it by line, ScrollPattern scrolls it.
        {
            bool note = invoke(uia, root, L"items.add");
            auto field = note ? findId(uia, root, L"items.dialog.note") : nullptr;
            IUIAutomationValuePattern* noteValue = nullptr; IUIAutomationTextPattern* noteText = nullptr; IUIAutomationScrollPattern* noteScroll = nullptr;
            note = note && field && SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&noteValue))) && noteValue && SUCCEEDED(noteValue->SetValue(L"l1\nl2\nl3\nl4\nl5\nl6"));
            RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            BSTR whole = nullptr;
            note = note && SUCCEEDED(field->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&noteText))) && noteText;
            IUIAutomationTextRange* range = nullptr; BSTR firstLine = nullptr, secondLine = nullptr, lastLine = nullptr; int moved = 0;
            note = note && SUCCEEDED(noteText->get_DocumentRange(&range)) && range && SUCCEEDED(range->GetText(-1, &whole)) && whole && std::wstring(whole) == L"l1\nl2\nl3\nl4\nl5\nl6" &&
                SUCCEEDED(range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range, TextPatternRangeEndpoint_Start)) && SUCCEEDED(range->ExpandToEnclosingUnit(TextUnit_Line)) &&
                SUCCEEDED(range->GetText(-1, &firstLine)) && firstLine && std::wstring(firstLine) == L"l1\n" &&
                SUCCEEDED(range->Move(TextUnit_Line, 1, &moved)) && moved == 1 && SUCCEEDED(range->GetText(-1, &secondLine)) && secondLine && std::wstring(secondLine) == L"l2\n" &&
                SUCCEEDED(range->Move(TextUnit_Line, 10, &moved)) && moved == 4 && SUCCEEDED(range->GetText(-1, &lastLine)) && lastLine && std::wstring(lastLine) == L"l6";
            BOOL vertical = FALSE, horizontal = TRUE; double percent = -1, view = -1;
            note = note && SUCCEEDED(field->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&noteScroll))) && noteScroll &&
                SUCCEEDED(noteScroll->get_CurrentVerticallyScrollable(&vertical)) && vertical && SUCCEEDED(noteScroll->get_CurrentHorizontallyScrollable(&horizontal)) && !horizontal &&
                SUCCEEDED(noteScroll->get_CurrentVerticalViewSize(&view)) && view > 49.9 && view < 50.1 &&
                SUCCEEDED(noteScroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 99.9 &&
                SUCCEEDED(noteScroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 0)) && SUCCEEDED(noteScroll->get_CurrentVerticalScrollPercent(&percent)) && percent == 0 &&
                SUCCEEDED(noteScroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100)) && SUCCEEDED(noteScroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 99.9 &&
                SUCCEEDED(noteScroll->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallDecrement)) && SUCCEEDED(noteScroll->get_CurrentVerticalScrollPercent(&percent)) && percent > 66 && percent < 67 &&
                noteScroll->Scroll(ScrollAmount_SmallIncrement, ScrollAmount_NoAmount) == UIA_E_INVALIDOPERATION;
            SysFreeString(whole); SysFreeString(firstLine); SysFreeString(secondLine); SysFreeString(lastLine);
            if (range) range->Release(); if (noteScroll) noteScroll->Release(); if (noteText) noteText->Release(); if (noteValue) noteValue->Release(); if (field) field->Release();
            note = closeModal(uia, root, L"items.dialog") && note;
            std::cout << "UA08 multi-line note: " << (note ? "PASS" : "FAIL") << '\n'; ok &= note;
        }
        // #80: the text range reports the field's horizontal alignment (an int) and line spacing (a BSTR multiple).
        {
            auto searchField = findId(uia, root, L"items.search"); IUIAutomationTextPattern* searchText = nullptr; IUIAutomationTextRange* range = nullptr;
            auto attributeCase = [&](int mode, int alignment, const wchar_t* spacing) {  // mode: 0 defaults, 1 centered 1.5, 2 justified, 3 justified with "justified text as left" on (#84)
                VARIANT align{}, line{}; VariantInit(&align); VariantInit(&line);
                bool good = SendMessageW(s.window, uiaDemo::messageTextFlow, static_cast<WPARAM>(mode), 0) == 0 && searchField &&
                    SUCCEEDED(searchField->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&searchText))) && searchText && SUCCEEDED(searchText->get_DocumentRange(&range)) && range &&
                    SUCCEEDED(range->GetAttributeValue(UIA_HorizontalTextAlignmentAttributeId, &align)) && align.vt == VT_I4 && align.lVal == alignment &&
                    SUCCEEDED(range->GetAttributeValue(UIA_LineSpacingAttributeId, &line)) && line.vt == VT_BSTR && line.bstrVal && std::wstring(line.bstrVal) == spacing;
                VariantClear(&align); VariantClear(&line);
                if (range) range->Release(); range = nullptr; if (searchText) searchText->Release(); searchText = nullptr;
                return good;
            };
            bool attributes = attributeCase(0, HorizontalTextAlignment_Left, L"1") && attributeCase(1, HorizontalTextAlignment_Centered, L"1.5") && attributeCase(2, HorizontalTextAlignment_Justified, L"1") && attributeCase(3, HorizontalTextAlignment_Left, L"1") && attributeCase(0, HorizontalTextAlignment_Left, L"1");
            if (searchField) searchField->Release();
            std::cout << "UA09 text alignment and line spacing attributes: " << (attributes ? "PASS" : "FAIL") << '\n'; ok &= attributes;
        }
        // #81: the rich note's TextPattern reports the weight, slant, underline and foreground color of each range as
        // the field draws them now (a color name resolved for the current theme); a mixed range reports the mixed value.
        {
            bool rich = invoke(uia, root, L"items.add");
            auto field = rich ? findId(uia, root, L"items.dialog.note") : nullptr;
            rich = rich && field && SendMessageW(s.window, uiaDemo::messageRichText, 1, 0) == 0;
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
            IUIAutomationTextRange* boldRange = rich ? slice(0, 2) : nullptr; IUIAutomationTextRange* italicRange = rich ? slice(2, 2) : nullptr; IUIAutomationTextRange* redRange = rich ? slice(4, 2) : nullptr;
            VARIANT weight, slant, line, plainSlant, plainLine, plainWeight, red, plainLight, mixedWeight, mixedColor;
            IUnknown* reservedMixed = nullptr;
            rich = rich && boldRange && italicRange && redRange && SUCCEEDED(UiaGetReservedMixedAttributeValue(&reservedMixed));
            rich = rich && read(boldRange, UIA_FontWeightAttributeId, weight) && weight.vt == VT_I4 && weight.lVal == 700 &&
                read(boldRange, UIA_IsItalicAttributeId, plainSlant) && plainSlant.vt == VT_BOOL && plainSlant.boolVal == VARIANT_FALSE &&
                read(boldRange, UIA_UnderlineStyleAttributeId, plainLine) && plainLine.vt == VT_I4 && plainLine.lVal == 0 &&
                read(italicRange, UIA_IsItalicAttributeId, slant) && slant.vt == VT_BOOL && slant.boolVal == VARIANT_TRUE &&
                read(italicRange, UIA_UnderlineStyleAttributeId, line) && line.vt == VT_I4 && line.lVal == 1 &&
                read(italicRange, UIA_FontWeightAttributeId, plainWeight) && plainWeight.vt == VT_I4 && plainWeight.lVal == 400;
            rich = rich && read(redRange, UIA_ForegroundColorAttributeId, red) && red.vt == VT_I4 && red.lVal == (176 | (0 << 8) | (32 << 16)) &&
                read(italicRange, UIA_ForegroundColorAttributeId, plainLight) && plainLight.vt == VT_I4;
            rich = rich && read(whole, UIA_FontWeightAttributeId, mixedWeight) && mixedWeight.vt == VT_UNKNOWN && mixedWeight.punkVal == reservedMixed &&
                read(whole, UIA_ForegroundColorAttributeId, mixedColor) && mixedColor.vt == VT_UNKNOWN && mixedColor.punkVal == reservedMixed;
            // FindAttribute walks the runs of equal value.
            VARIANT wanted; VariantInit(&wanted); wanted.vt = VT_BOOL; wanted.boolVal = VARIANT_TRUE;
            IUIAutomationTextRange* found = nullptr; BSTR foundText = nullptr;
            rich = rich && SUCCEEDED(whole->FindAttribute(UIA_IsItalicAttributeId, wanted, FALSE, &found)) && found && SUCCEEDED(found->GetText(-1, &foundText)) && foundText && std::wstring(foundText) == L"cd";
            // The dark theme repaints the same names in their dark values; the normal text color follows the theme too.
            bool themed = rich && SendMessageW(s.window, uiaDemo::messageRichText, 2, 1) == 0;
            RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            VARIANT darkRed, plainDark;
            themed = themed && read(redRange, UIA_ForegroundColorAttributeId, darkRed) && darkRed.vt == VT_I4 && darkRed.lVal == (255 | (138 << 8) | (148 << 16)) &&
                read(italicRange, UIA_ForegroundColorAttributeId, plainDark) && plainDark.vt == VT_I4 && plainDark.lVal != plainLight.lVal;
            SendMessageW(s.window, uiaDemo::messageRichText, 2, 0);
            // A plain (non-rich) text field reports no such attributes.
            auto searchField = findId(uia, root, L"items.dialog.name"); IUIAutomationTextPattern* namePattern = nullptr; IUIAutomationTextRange* nameRange = nullptr; VARIANT unsupported;
            bool plainUnsupported = searchField && SUCCEEDED(searchField->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&namePattern))) && namePattern && SUCCEEDED(namePattern->get_DocumentRange(&nameRange)) && nameRange &&
                read(nameRange, UIA_FontWeightAttributeId, unsupported) && unsupported.vt == VT_UNKNOWN;
            if (searchField) searchField->Release(); if (namePattern) namePattern->Release(); if (nameRange) nameRange->Release();
            SysFreeString(foundText); if (found) found->Release(); if (boldRange) boldRange->Release(); if (italicRange) italicRange->Release(); if (redRange) redRange->Release();
            if (whole) whole->Release(); if (pattern) pattern->Release(); if (field) field->Release();
            rich = closeModal(uia, root, L"items.dialog") && rich;
            std::cout << "UA10 rich text attributes: " << (rich && themed && plainUnsupported ? "PASS" : "FAIL") << " (values=" << rich << " theme=" << themed << " plain=" << plainUnsupported << ")\n"; ok &= rich && themed && plainUnsupported;
        }
        // #83: TextUnit_Word and TextUnit_Paragraph follow the framework's word and paragraph boundaries. ASCII text, so
        // the system dictionary cannot change the words. The expectations are shared with the core seam (tests/word_select.cpp).
        {
            namespace shared = test::units;
            auto wide = [](const std::u16string& value) { return std::wstring(reinterpret_cast<const wchar_t*>(value.data()), value.size()); };
            const auto& words = shared::words(); const auto& paragraphUnits = shared::paragraphs();
            bool units = invoke(uia, root, L"items.add");
            auto field = units ? findId(uia, root, L"items.dialog.note") : nullptr;
            IUIAutomationValuePattern* unitValue = nullptr; IUIAutomationTextPattern* unitText = nullptr; IUIAutomationTextRange* range = nullptr;
            units = units && field && SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&unitValue))) && unitValue && SUCCEEDED(unitValue->SetValue(const_cast<wchar_t*>(wide(shared::text).c_str())));
            RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            auto read = [](IUIAutomationTextRange* r) { BSTR t = nullptr; std::wstring out; if (r && SUCCEEDED(r->GetText(-1, &t)) && t) out.assign(t, SysStringLen(t)); SysFreeString(t); return out; };
            int moved = -99;
            units = units && SUCCEEDED(field->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&unitText))) && unitText && SUCCEEDED(unitText->get_DocumentRange(&range)) && range &&
                SUCCEEDED(range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range, TextPatternRangeEndpoint_Start)) && SUCCEEDED(range->ExpandToEnclosingUnit(TextUnit_Word)) && read(range) == wide(words[0]);
            for (std::size_t i = 1; i < words.size(); ++i)
                units = units && SUCCEEDED(range->Move(TextUnit_Word, 1, &moved)) && moved == 1 && read(range) == wide(words[i]);
            units = units && SUCCEEDED(range->Move(TextUnit_Word, 5, &moved)) && moved == 0 && read(range) == wide(words.back()) &&
                SUCCEEDED(range->Move(TextUnit_Word, -9, &moved)) && moved == -static_cast<int>(words.size() - 1) && read(range) == wide(words[0]);
            units = units && SUCCEEDED(range->ExpandToEnclosingUnit(TextUnit_Paragraph)) && read(range) == wide(paragraphUnits[0]) &&
                SUCCEEDED(range->Move(TextUnit_Paragraph, 3, &moved)) && moved == 1 && read(range) == wide(paragraphUnits[1]) &&
                SUCCEEDED(range->Move(TextUnit_Paragraph, -1, &moved)) && moved == -1 && read(range) == wide(paragraphUnits[0]);
            IUIAutomationTextRange* grown = nullptr; IUIAutomationTextRange* paragraphs = nullptr;
            units = units && SUCCEEDED(unitText->get_DocumentRange(&grown)) && grown &&
                SUCCEEDED(grown->MoveEndpointByRange(TextPatternRangeEndpoint_End, grown, TextPatternRangeEndpoint_Start)) &&
                SUCCEEDED(grown->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Word, 3, &moved)) && moved == 3 && read(grown) == wide(shared::join(words, 0, 3)) &&
                SUCCEEDED(grown->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Word, 1, &moved)) && moved == 1 && read(grown) == wide(shared::join(words, 1, 3)) &&
                SUCCEEDED(unitText->get_DocumentRange(&paragraphs)) && paragraphs &&
                SUCCEEDED(paragraphs->MoveEndpointByRange(TextPatternRangeEndpoint_End, paragraphs, TextPatternRangeEndpoint_Start)) &&
                SUCCEEDED(paragraphs->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Paragraph, 1, &moved)) && moved == 1 && read(paragraphs) == wide(paragraphUnits[0]) &&
                SUCCEEDED(paragraphs->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Paragraph, 1, &moved)) && moved == 1 && read(paragraphs) == wide(shared::join(paragraphUnits, 0, 2)) &&
                SUCCEEDED(paragraphs->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Paragraph, 1, &moved)) && moved == 0;
            if (paragraphs) paragraphs->Release(); if (grown) grown->Release(); if (range) range->Release(); if (unitText) unitText->Release(); if (unitValue) unitValue->Release(); if (field) field->Release();
            units = closeModal(uia, root, L"items.dialog") && units;
            std::cout << "UA11 word and paragraph units: " << (units ? "PASS" : "FAIL") << '\n'; ok &= units;
        }
        auto itemsList = findId(uia, root, L"items.list"); IUIAutomationSelectionPattern* itemSelection = nullptr; BOOL listRequired = TRUE;
        bool optionalList = itemsList && SUCCEEDED(itemsList->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&itemSelection))) && itemSelection && SUCCEEDED(itemSelection->get_CurrentIsSelectionRequired(&listRequired)) && !listRequired;
        if (itemSelection) itemSelection->Release(); if (itemsList) itemsList->Release();
        std::cout << "optional list=" << optionalList << '\n'; navigation &= optionalList;
        auto edit = findId(uia, root, L"items.edit"); auto events = new EnabledEvent(); PROPERTYID enabled = UIA_IsEnabledPropertyId;
        auto subscribed = edit ? uia->AddPropertyChangedEventHandlerNativeArray(edit, TreeScope_Element, nullptr, events, &enabled, 1) : E_FAIL;
        bool enabledEvent = SUCCEEDED(subscribed) && select(uia, root, L"T001") && WaitForSingleObject(events->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(subscribed)) uia->RemovePropertyChangedEventHandler(edit, events); events->Release(); if (edit) edit->Release();
        std::cout << "enabled event=" << enabledEvent << '\n'; navigation &= enabledEvent;
        auto search = findId(uia, root, L"items.search"); auto textEvents = new TextEvent();
        auto valueEvents = new ValueEvent(); PROPERTYID valueProperty = UIA_ValueValuePropertyId;
        auto valueSubscribed = search ? uia->AddPropertyChangedEventHandlerNativeArray(search, TreeScope_Element, nullptr, valueEvents, &valueProperty, 1) : E_FAIL;
        auto textSubscribed = search ? uia->AddAutomationEventHandler(UIA_Text_TextChangedEventId, search, TreeScope_Element, nullptr, textEvents) : E_FAIL;
        auto changed = SendMessageW(s.window, uiaDemo::messageSetSearchValue, 1, 0);
        bool textEvent = changed == 0 && SUCCEEDED(textSubscribed) && WaitForSingleObject(textEvents->received, 2000) == WAIT_OBJECT_0;
        bool valueEvent = SUCCEEDED(valueSubscribed) && WaitForSingleObject(valueEvents->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(valueSubscribed)) uia->RemovePropertyChangedEventHandler(search, valueEvents); valueEvents->Release();
        std::cout << "value event=" << valueEvent << '\n'; navigation &= valueEvent;
        if (SUCCEEDED(textSubscribed)) uia->RemoveAutomationEventHandler(UIA_Text_TextChangedEventId, search, textEvents); textEvents->Release();
        auto selectionEvents = new TextEvent(); selectionEvents->expected = UIA_Text_TextSelectionChangedEventId;
        auto selectionSubscribed = search ? uia->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, search, TreeScope_Element, nullptr, selectionEvents) : E_FAIL;
        auto selectionChanged = SendMessageW(s.window, uiaDemo::messageSelectInSearch, 0, 0);
        bool selectionEvent = selectionChanged == 0 && SUCCEEDED(selectionSubscribed) && WaitForSingleObject(selectionEvents->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selectionSubscribed)) uia->RemoveAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, search, selectionEvents); selectionEvents->Release(); if (search) search->Release();
        std::cout << "text selection event=" << selectionEvent << '\n'; navigation &= selectionEvent;
        SendMessageW(s.window, uiaDemo::messageSetSearchValue, 0, 0); std::cout << "text event=" << textEvent << '\n'; navigation &= textEvent;
        {   // UA14 (#74): pointer hover is not part of the UIA tree. Simulating a hover on the search box changes no property
            // of it, adds or removes no element and raises no event of any kind (the brief: "UIA tree and events unchanged").
            struct Snapshot { std::wstring name, value; BOOL enabled = FALSE, focused = FALSE; RECT box{}; int count = -1; };
            auto take = [&] {
                Snapshot snap; BSTR text = nullptr;
                if (auto element = findId(uia, root, L"items.search")) {
                    if (SUCCEEDED(element->get_CurrentName(&text)) && text) snap.name = text;
                    SysFreeString(text); text = nullptr;
                    IUIAutomationValuePattern* value = nullptr;
                    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value) {
                        if (SUCCEEDED(value->get_CurrentValue(&text)) && text) snap.value = text;
                        SysFreeString(text); value->Release();
                    }
                    element->get_CurrentIsEnabled(&snap.enabled); element->get_CurrentHasKeyboardFocus(&snap.focused);
                    element->get_CurrentBoundingRectangle(&snap.box); element->Release();
                }
                IUIAutomationCondition* any = nullptr; IUIAutomationElementArray* nodes = nullptr;
                if (SUCCEEDED(uia->CreateTrueCondition(&any)) && any && SUCCEEDED(root->FindAll(TreeScope_Subtree, any, &nodes)) && nodes) nodes->get_Length(&snap.count);
                if (nodes) nodes->Release();
                if (any) any->Release();
                return snap;
            };
            const auto before = take();
            auto propertyEvents = new AnyPropertyEvent(); auto automationEvents = new AnyAutomationEvent(); auto structureEvents = new AnyStructureEvent();
            PROPERTYID watched[] = {UIA_NamePropertyId, UIA_ValueValuePropertyId, UIA_IsEnabledPropertyId, UIA_HasKeyboardFocusPropertyId, UIA_BoundingRectanglePropertyId, UIA_IsOffscreenPropertyId};
            bool subscribed = SUCCEEDED(uia->AddPropertyChangedEventHandlerNativeArray(root, TreeScope_Subtree, nullptr, propertyEvents, watched, 6)) &&
                SUCCEEDED(uia->AddStructureChangedEventHandler(root, TreeScope_Subtree, nullptr, structureEvents));
            for (EVENTID id : {UIA_Text_TextChangedEventId, UIA_Text_TextSelectionChangedEventId, UIA_LayoutInvalidatedEventId})
                subscribed = subscribed && SUCCEEDED(uia->AddAutomationEventHandler(id, root, TreeScope_Subtree, nullptr, automationEvents));
            const bool hovered = SendMessageW(s.window, uiaDemo::messageHover, 1, 0) == 0;
            Sleep(500);  // the repaint and any event it would raise have long arrived
            const bool silent = WaitForSingleObject(propertyEvents->received, 0) != WAIT_OBJECT_0 && WaitForSingleObject(automationEvents->received, 0) != WAIT_OBJECT_0 &&
                WaitForSingleObject(structureEvents->received, 0) != WAIT_OBJECT_0;
            const auto during = take();
            const bool cleared = SendMessageW(s.window, uiaDemo::messageHover, 0, 0) == 0;
            uia->RemoveAllEventHandlers(); propertyEvents->Release(); automationEvents->Release(); structureEvents->Release();
            const bool same = before.name == during.name && before.value == during.value && before.enabled == during.enabled && before.focused == during.focused &&
                before.count == during.count && before.count > 0 && EqualRect(&before.box, &during.box);
            const bool hoverHidden = subscribed && hovered && cleared && silent && same;
            std::cout << "UA14 hover is not exposed: " << (hoverHidden ? "PASS" : "FAIL") << " (subscribed=" << subscribed << " hovered=" << hovered << " silent=" << silent << " same=" << same << ")\n"; ok &= hoverHidden;
        }
        navigation = navigation && select(uia, nav, L"appearance") && patterns(uia, root);
        auto font = findId(uia, root, L"appearance.font"); IUIAutomationExpandCollapsePattern* expand = nullptr;
        auto defaultOption = font ? findId(uia, font, L"default") : nullptr; BOOL closedOffscreen = FALSE;
        bool collapsed = defaultOption && SUCCEEDED(defaultOption->get_CurrentIsOffscreen(&closedOffscreen)) && closedOffscreen;
        bool dropdown = font && SUCCEEDED(font->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand && SUCCEEDED(expand->Expand());
        IUIAutomationCondition* allOptions = nullptr; IUIAutomationElementArray* optionNodes = nullptr; int optionCount = 0;
        bool allPresent = font && SUCCEEDED(uia->CreateTrueCondition(&allOptions)) && allOptions && SUCCEEDED(font->FindAll(TreeScope_Children, allOptions, &optionNodes)) && optionNodes && SUCCEEDED(optionNodes->get_Length(&optionCount)) && optionCount == 151;
        if (optionNodes) optionNodes->Release(); if (allOptions) allOptions->Release(); dropdown = dropdown && allPresent;
        auto last = font ? findId(uia, font, L"family:Font149") : nullptr; BOOL offscreen = FALSE;
        dropdown = dropdown && last && SUCCEEDED(last->get_CurrentIsOffscreen(&offscreen)) && offscreen && patterns(uia, root);
        if (expand) { collapsed = collapsed && SUCCEEDED(expand->Collapse()); closedOffscreen = FALSE; collapsed = collapsed && SUCCEEDED(defaultOption->get_CurrentIsOffscreen(&closedOffscreen)) && closedOffscreen; }
        std::cout << "UA07: " << (dropdown ? "PASS" : "FAIL") << '\n';
        dropdown = dropdown && collapsed; if (defaultOption) defaultOption->Release();
        if (last) last->Release(); if (expand) expand->Release(); if (font) font->Release();
        bool about = select(uia, nav, L"about"); RedrawWindow(s.window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); about = about && patterns(uia, root); auto text = findId(uia, root, L"about.license_text"); BSTR name = nullptr; type = 0; BOOL focusable = FALSE;
        about = about && text && SUCCEEDED(text->get_CurrentControlType(&type)) && type == UIA_TextControlTypeId && SUCCEEDED(text->get_CurrentName(&name)) && name && SysStringLen(name) == 3009 && SUCCEEDED(text->get_CurrentIsKeyboardFocusable(&focusable)) && focusable;
        if (!about) std::cout << "about details element=" << bool(text) << " type=" << type << " name-length=" << (name ? SysStringLen(name) : 0) << " focusable=" << focusable << '\n';
        SysFreeString(name); if (text) text->Release();
        {
            // #82: the vertical label reports its text in reading order as the name, and the box it is drawn in.
            auto vertical = findId(uia, root, L"about.vertical"); BSTR vname = nullptr; RECT box{}; CONTROLTYPEID vtype = 0;
            // The box is the one drawn: inside the window, as tall as the application asked (200 DIP) and as wide as its columns.
            RECT windowBox{}; bool inside = SUCCEEDED(root->get_CurrentBoundingRectangle(&windowBox));
            bool label = vertical && SUCCEEDED(vertical->get_CurrentControlType(&vtype)) && vtype == UIA_TextControlTypeId && SUCCEEDED(vertical->get_CurrentName(&vname)) && vname &&
                std::wstring(vname) == std::wstring(reinterpret_cast<const wchar_t*>(test::vertical::aboutSample.data()), test::vertical::aboutSample.size()) &&
                SUCCEEDED(vertical->get_CurrentBoundingRectangle(&box)) && inside &&
                test::vertical::drawnBox(box.left, box.top, box.right, box.bottom, windowBox.left, windowBox.top, windowBox.right, windowBox.bottom, 150);
            if (!label) std::cout << "vertical element=" << bool(vertical) << " type=" << vtype << " name=" << (vname ? SysStringLen(vname) : 0) << " box=" << box.left << ',' << box.top << ',' << box.right << ',' << box.bottom << " window=" << windowBox.left << ',' << windowBox.top << ',' << windowBox.right << ',' << windowBox.bottom << '\n';
            std::cout << "UA13 vertical label: " << (label ? "PASS" : "FAIL") << '\n'; ok &= label;
            SysFreeString(vname); if (vertical) vertical->Release();
        }
        std::cout << "navigation=" << navigation << " long-dropdown=" << dropdown << " about-text=" << about << '\n'; ok = ok && navigation && dropdown && about;
        if (select(uia, nav, L"items")) {   // UA12: the context menu as a UIA Menu with MenuItem and Separator children (#85).
            auto openedEvent = new TextEvent(); openedEvent->expected = UIA_MenuOpenedEventId;
            auto closedEvent = new TextEvent(); closedEvent->expected = UIA_MenuClosedEventId;
            auto focusEvent = new FocusEvent();
            auto openedSubscribed = uia->AddAutomationEventHandler(UIA_MenuOpenedEventId, root, TreeScope_Subtree, nullptr, openedEvent);
            auto closedSubscribed = uia->AddAutomationEventHandler(UIA_MenuClosedEventId, root, TreeScope_Subtree, nullptr, closedEvent);
            auto focusSubscribed = uia->AddFocusChangedEventHandler(nullptr, focusEvent);
            const std::wstring editName = L"編輯(E)", deleteName = L"刪除(D)";
            bool opened = SendMessageW(s.window, uiaDemo::messageContextMenu, 0, 0) == 0 && SUCCEEDED(openedSubscribed) && WaitForSingleObject(openedEvent->received, 3000) == WAIT_OBJECT_0;
            auto menu = findId(uia, root, L"items.list/menu"); CONTROLTYPEID menuType = 0;
            bool isMenu = menu && SUCCEEDED(menu->get_CurrentControlType(&menuType)) && menuType == UIA_MenuControlTypeId;
            auto editItem = menu ? findId(uia, menu, L"items.edit") : nullptr;
            auto deleteItem = menu ? findId(uia, menu, L"items.delete") : nullptr;
            bool itemsOk = menuItemIs(editItem, UIA_MenuItemControlTypeId, editName.c_str(), L"E", L"Enter", true) &&
                menuItemIs(deleteItem, UIA_MenuItemControlTypeId, deleteName.c_str(), L"D", L"Del", true);
            // Focus moves to an item when the keyboard highlights it.
            focusEvent->wanted = L"items.edit";
            bool focusedEvent = SendMessageW(s.window, uiaDemo::messageContextMenu, 3, 0) == 0 && SUCCEEDED(focusSubscribed) && WaitForSingleObject(focusEvent->received, 3000) == WAIT_OBJECT_0;
            // HasKeyboardFocus is the semantic focus AND the window owning the keyboard focus. The old check (`|| GetFocus() != s.window`)
            // was always true from this process. Now: bring the window forward (best effort); when it is in front the highlighted item
            // must report focus; when Windows will not let us, say so instead of passing.
            SetForegroundWindow(s.window); Sleep(200);
            BOOL hasFocus = FALSE;
            const bool inFront = GetForegroundWindow() == s.window;
            bool focusState = editItem && SUCCEEDED(editItem->get_CurrentHasKeyboardFocus(&hasFocus)) && (!inFront || hasFocus);
            if (!inFront) std::cout << "UA12 focus state: N/A (the demo window could not be brought to the foreground)\n";
            // Invoke runs the row's Edit: the menu closes (MenuClosed) and the edit dialog opens.
            bool invoked = editItem && invokeId(uia, menu, L"items.edit") == S_OK && WaitForSingleObject(closedEvent->received, 3000) == WAIT_OBJECT_0;
            auto gone = findId(uia, root, L"items.list/menu"); bool vanished = !gone; if (gone) gone->Release();
            bool dialogClosed = closeModal(uia, root, L"items.dialog");
            if (editItem) editItem->Release(); if (deleteItem) deleteItem->Release(); if (menu) menu->Release();
            // The search field's menu: a separator line and a disabled item (nothing is selected there).
            // The second opening raises its own MenuOpened (the event is reset first, so the first one cannot satisfy this wait).
            ResetEvent(openedEvent->received);
            const auto fieldStatus = SendMessageW(s.window, uiaDemo::messageContextMenu, 2, 0);
            const bool secondOpenedEvent = SUCCEEDED(openedSubscribed) && WaitForSingleObject(openedEvent->received, 3000) == WAIT_OBJECT_0;
            IUIAutomationElement* fieldMenu = nullptr;
            for (int attempt = 0; attempt < 30 && !fieldMenu; ++attempt) { fieldMenu = findId(uia, root, L"items.search/menu"); if (!fieldMenu) Sleep(100); }
            bool fieldOpen = fieldStatus == 0 && fieldMenu && secondOpenedEvent;
            auto copyItem = fieldMenu ? findId(uia, fieldMenu, L"lcars.copy") : nullptr;
            auto selectAllItem = fieldMenu ? findId(uia, fieldMenu, L"lcars.select_all") : nullptr;
            bool fieldItems = menuItemIs(copyItem, UIA_MenuItemControlTypeId, L"複製(C)", L"C", L"Ctrl+C", false) &&
                menuItemIs(selectAllItem, UIA_MenuItemControlTypeId, L"全選(A)", L"A", L"Ctrl+A", true);
            VARIANT separatorType; VariantInit(&separatorType); separatorType.vt = VT_I4; separatorType.lVal = UIA_SeparatorControlTypeId;
            IUIAutomationCondition* separatorCondition = nullptr; IUIAutomationElementArray* separators = nullptr; int separatorCount = 0;
            bool lines = fieldMenu && SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, separatorType, &separatorCondition)) &&
                SUCCEEDED(fieldMenu->FindAll(TreeScope_Children, separatorCondition, &separators)) && separators && SUCCEEDED(separators->get_Length(&separatorCount)) && separatorCount == 2;
            if (separators) separators->Release(); if (separatorCondition) separatorCondition->Release();
            bool disabledRefuses = copyItem && [&] { IUIAutomationInvokePattern* p = nullptr; bool refused = SUCCEEDED(copyItem->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&p))) && p && p->Invoke() == UIA_E_ELEMENTNOTENABLED; if (p) p->Release(); return refused; }();
            if (copyItem) copyItem->Release(); if (selectAllItem) selectAllItem->Release(); if (fieldMenu) fieldMenu->Release();
            // MenuClosed arrives for every way of closing: the application's closeContextMenu (1), Escape (4) and the window
            // losing activation (6). Each round opens the search field's menu again and waits for its MenuOpened first.
            bool fieldClosed = true;
            for (int how : {1, 4, 6}) {
                if (how == 6) SendMessageW(s.window, uiaDemo::messageContextMenu, 5, 0);  // active, so deactivating is a change
                ResetEvent(openedEvent->received); ResetEvent(closedEvent->received);
                if (how != 1) {  // the menu opened above is still open for the first way
                    const bool reopened = SendMessageW(s.window, uiaDemo::messageContextMenu, 2, 0) == 0 && WaitForSingleObject(openedEvent->received, 3000) == WAIT_OBJECT_0;
                    fieldClosed = fieldClosed && reopened;
                    ResetEvent(closedEvent->received);
                }
                const bool closedOk = SendMessageW(s.window, uiaDemo::messageContextMenu, how, 0) == 0 && SUCCEEDED(closedSubscribed) &&
                    WaitForSingleObject(closedEvent->received, 3000) == WAIT_OBJECT_0;
                if (!closedOk) std::cout << "UA12 MenuClosed missing for way " << how << '\n';
                fieldClosed = fieldClosed && closedOk;
            }
            if (SUCCEEDED(openedSubscribed)) uia->RemoveAutomationEventHandler(UIA_MenuOpenedEventId, root, openedEvent);
            if (SUCCEEDED(closedSubscribed)) uia->RemoveAutomationEventHandler(UIA_MenuClosedEventId, root, closedEvent);
            if (SUCCEEDED(focusSubscribed)) uia->RemoveFocusChangedEventHandler(focusEvent);
            openedEvent->Release(); closedEvent->Release(); focusEvent->Release();
            bool menuPass = opened && isMenu && itemsOk && focusedEvent && focusState && invoked && vanished && dialogClosed && fieldOpen && fieldItems && lines && disabledRefuses && fieldClosed;
            std::cout << "UA12 context menu: " << (menuPass ? "PASS" : "FAIL") << " opened=" << opened << " menu=" << isMenu << " items=" << itemsOk << " focus-event=" << focusedEvent << " focus=" << focusState
                      << " invoke+closed=" << invoked << " vanished=" << vanished << " dialog=" << dialogClosed << " field-menu=" << fieldOpen << " field-items=" << fieldItems << " lines=" << lines << " disabled=" << disabledRefuses << '\n';
            ok &= menuPass;
        }
        bool general = select(uia, nav, L"general"); BSTR beforeTitle = nullptr, afterTitle = nullptr;
        const std::wstring expectedTitle = std::wstring(argv[2]) == L"material" ? L"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Material-inspired" : L"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Minimal Web";
        bool title = general && SUCCEEDED(root->get_CurrentName(&beforeTitle)) && beforeTitle && std::wstring(beforeTitle) == expectedTitle;
        auto notices = findId(uia, root, L"general.notices"); IUIAutomationTogglePattern* toggle = nullptr;
        title = title && notices && SUCCEEDED(notices->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle && SUCCEEDED(toggle->Toggle()) && SUCCEEDED(root->get_CurrentName(&afterTitle)) && beforeTitle && afterTitle && std::wstring(afterTitle) == L"*" + std::wstring(beforeTitle);
        SysFreeString(beforeTitle); SysFreeString(afterTitle); if (toggle) toggle->Release(); if (notices) notices->Release();
        std::cout << "UA06: " << (title ? "PASS" : "FAIL") << '\n'; ok &= title;
        {   // UA15 (#90): the focus ring controls on the appearance page work through UIA: a slider (RangeValue), a checkbox (Toggle)
            // and dropdown options (SelectionItem); the indicator off disables the thickness slider but keeps its value.
            const bool onPage = select(uia, nav, L"appearance") && patterns(uia, root);
            auto mode = findId(uia, root, L"appearance.focus_mode"); auto width = findId(uia, root, L"appearance.focus_width"); auto same = findId(uia, root, L"appearance.focus_radius_same");
            auto never = mode ? findId(uia, mode, L"never") : nullptr; auto automatic = mode ? findId(uia, mode, L"automatic") : nullptr;
            IUIAutomationRangeValuePattern* range = nullptr; IUIAutomationTogglePattern* toggle = nullptr; IUIAutomationSelectionItemPattern* pickNever = nullptr; IUIAutomationSelectionItemPattern* pickAuto = nullptr;
            bool ring = onPage && mode && width && same && never && automatic &&
                SUCCEEDED(width->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range && SUCCEEDED(range->SetValue(5)) &&
                SUCCEEDED(same->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle && SUCCEEDED(toggle->Toggle()) &&
                SUCCEEDED(never->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&pickNever))) && pickNever && SUCCEEDED(automatic->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&pickAuto))) && pickAuto;
            double value = 0; BOOL enabledNow = FALSE; ToggleState state = ToggleState_On;
            ring = ring && SUCCEEDED(range->get_CurrentValue(&value)) && value == 5 && SUCCEEDED(toggle->get_CurrentToggleState(&state)) && state == ToggleState_Off &&
                SUCCEEDED(width->get_CurrentIsEnabled(&enabledNow)) && enabledNow;
            ring = ring && SUCCEEDED(pickNever->Select()) && SUCCEEDED(width->get_CurrentIsEnabled(&enabledNow)) && !enabledNow && SUCCEEDED(range->get_CurrentValue(&value)) && value == 5;
            ring = ring && SUCCEEDED(pickAuto->Select()) && SUCCEEDED(width->get_CurrentIsEnabled(&enabledNow)) && enabledNow;
            // The radius slider (on the page once the checkbox is unchecked) takes RangeValue; the color dropdown expands and collapses
            // (ExpandCollapse) and its options are picked with SelectionItem.
            auto radius = findId(uia, root, L"appearance.focus_radius"); auto color = findId(uia, root, L"appearance.focus_color"); auto green = color ? findId(uia, color, L"green") : nullptr;
            IUIAutomationRangeValuePattern* radiusRange = nullptr; IUIAutomationExpandCollapsePattern* colorExpand = nullptr; IUIAutomationSelectionItemPattern* pickGreen = nullptr;
            double radiusValue = 0, radiusMin = -1, radiusMax = -1; BOOL selected = FALSE;
            const bool radiusOk = radius && SUCCEEDED(radius->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&radiusRange))) && radiusRange &&
                SUCCEEDED(radiusRange->get_CurrentMinimum(&radiusMin)) && SUCCEEDED(radiusRange->get_CurrentMaximum(&radiusMax)) && radiusMin == 0 && radiusMax == 32 &&
                SUCCEEDED(radiusRange->SetValue(20)) && SUCCEEDED(radiusRange->get_CurrentValue(&radiusValue)) && radiusValue == 20;
            const bool colorOk = color && green && SUCCEEDED(color->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&colorExpand))) && colorExpand && SUCCEEDED(colorExpand->Expand()) && SUCCEEDED(colorExpand->Collapse()) &&
                SUCCEEDED(green->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&pickGreen))) && pickGreen && SUCCEEDED(pickGreen->Select()) && SUCCEEDED(pickGreen->get_CurrentIsSelected(&selected)) && selected;
            if (!radiusOk || !colorOk) std::cout << "UA15 radius slider=" << radiusOk << " color dropdown=" << colorOk << '\n';
            ring = ring && radiusOk && colorOk;
            if (pickGreen) pickGreen->Release(); if (colorExpand) colorExpand->Release(); if (radiusRange) radiusRange->Release();
            if (green) green->Release(); if (color) color->Release(); if (radius) radius->Release();
            if (pickNever) pickNever->Release(); if (pickAuto) pickAuto->Release(); if (toggle) toggle->Release(); if (range) range->Release();
            if (never) never->Release(); if (automatic) automatic->Release(); if (mode) mode->Release(); if (width) width->Release(); if (same) same->Release();
            std::cout << "UA15 focus ring controls: " << (ring ? "PASS" : "FAIL") << '\n'; ok &= ring;
        }
        IUIAutomationWindowPattern* window = nullptr;
        bool closeReady = SUCCEEDED(root->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&window))) && window;
        bool closeDialog = closeReady && SUCCEEDED(window->Close()) && closeModal(uia, root, L"app.close_dialog");
        std::cout << "UA04 close dialog: " << (closeDialog ? "PASS" : "FAIL") << '\n'; ok &= closeDialog;
        auto opened = new TextEvent(); opened->expected = UIA_Window_WindowOpenedEventId;
        auto openedSubscribed = uia->AddAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, TreeScope_Subtree, nullptr, opened);
        bool discard = closeReady && SUCCEEDED(openedSubscribed) && SUCCEEDED(window->Close()) && WaitForSingleObject(opened->received, 3000) == WAIT_OBJECT_0 && invoke(uia, root, L"app.close_dialog.discard");
        if (SUCCEEDED(openedSubscribed)) uia->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, opened); opened->Release(); if (window) window->Release();
        std::cout << "UA05: " << (discard ? "PASS" : "FAIL") << '\n'; ok &= discard;
        if (selection) selection->Release(); if (nav) nav->Release();
    }
    if (root) root->Release(); if (uia) uia->Release(); if (s.window) PostMessageW(s.window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); ok = false; } DWORD exitCode = 1; if (!GetExitCodeProcess(pi.hProcess, &exitCode) || exitCode != 0) ok = false; CloseHandle(pi.hProcess); CoUninitialize(); return ok ? 0 : 1;
}
