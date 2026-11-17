#include "uia_client_helpers.hpp"
#include "option_properties.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include "uia_pattern_drivers.hpp"

class PropertyEvents final : public UiaEventHandler<PropertyEvents, IUIAutomationPropertyChangedEventHandler> {
public:
    PROPERTYID expected = UIA_ToggleToggleStatePropertyId;
    LONG expectedValue = ToggleState_Off;
    bool doubleValue = false; double expectedDouble = 0;
    bool stringValue = false; std::wstring expectedString;
    bool boolValue = false; bool expectedBool = false;
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override {
        if (id == expected && ((stringValue && value.vt == VT_BSTR && value.bstrVal && expectedString == value.bstrVal) || (!stringValue && !doubleValue && !boolValue && value.vt == VT_I4 && value.lVal == expectedValue) || (doubleValue && value.vt == VT_R8 && value.dblVal == expectedDouble) || (boolValue && value.vt == VT_BOOL && (value.boolVal != VARIANT_FALSE) == expectedBool))) SetEvent(received);
        return S_OK;
    }
};

class SelectionEvents final : public UiaEventHandler<SelectionEvents, IUIAutomationEventHandler> {
public:
    EVENTID expected = UIA_SelectionItem_ElementSelectedEventId;
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override {
        if (id == expected) SetEvent(received); return S_OK;
    }
};

class StructureEvents final : public UiaEventHandler<StructureEvents, IUIAutomationStructureChangedEventHandler> {
public:
    bool lcarsOnly = false;
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement* sender, StructureChangeType type, SAFEARRAY*) override {
        if (lcarsOnly) {
            BSTR id = nullptr;
            const bool matches = sender && SUCCEEDED(sender->get_CurrentAutomationId(&id)) && (!id || !*id || std::wstring(id).starts_with(L"form."));
            SysFreeString(id); if (!matches) return S_OK;
        }
        if (type == StructureChangeType_ChildrenInvalidated) SetEvent(received); return S_OK;
    }
};

class FocusEvents final : public UiaEventHandler<FocusEvents, IUIAutomationFocusChangedEventHandler> {
public:
    std::wstring expected = L"T004";
    std::mutex mutex; std::vector<std::wstring> observed;
    HANDLE applyReceived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE nameReceived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE openerReceived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~FocusEvents() { CloseHandle(applyReceived); CloseHandle(nameReceived); CloseHandle(openerReceived); }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        BSTR id = nullptr; if (sender && SUCCEEDED(sender->get_CurrentAutomationId(&id)) && id) {
            std::wstring value(id); if (value == L"T003" || value == L"T004") { std::lock_guard lock(mutex); observed.push_back(value); }
            if (value == expected) SetEvent(received); if (value == L"form.apply") SetEvent(applyReceived); if (value == L"form.name") SetEvent(nameReceived); if (value == L"form.title.pages.v03.open") SetEvent(openerReceived);
        }
        SysFreeString(id); return S_OK;
    }
};

class OptionFocusEvents final : public UiaEventHandler<OptionFocusEvents, IUIAutomationFocusChangedEventHandler> {
    std::mutex mutex_;
    std::wstring expected_;
    CONTROLTYPEID type_ = 0;
public:
    void expect(const wchar_t* id, CONTROLTYPEID type) {
        std::lock_guard lock(mutex_); expected_ = id; type_ = type; ResetEvent(received);
    }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        BSTR id = nullptr; CONTROLTYPEID type = 0; BOOL focused = FALSE, focusable = FALSE;
        if (sender && SUCCEEDED(sender->get_CurrentAutomationId(&id)) && id &&
            SUCCEEDED(sender->get_CurrentControlType(&type)) && SUCCEEDED(sender->get_CurrentHasKeyboardFocus(&focused)) &&
            SUCCEEDED(sender->get_CurrentIsKeyboardFocusable(&focusable))) {
            std::lock_guard lock(mutex_);
            if (expected_ == id && type_ == type && test::optionFocusMatches(focused != FALSE, focusable != FALSE, true)) SetEvent(received);
        }
        SysFreeString(id); return S_OK;
    }
};

bool selectedIdIs(IUIAutomationElement* container, const wchar_t* expected);
bool optionFocusCase(IUIAutomation* automation, IUIAutomationElement* root, HWND window) {
    auto events = new OptionFocusEvents();
    const auto subscribed = automation->AddFocusChangedEventHandler(nullptr, events);
    auto focus = [&](const wchar_t* id) {
        auto element = findId(automation, root, id);
        const bool ok = element && SUCCEEDED(element->SetFocus());
        if (element) element->Release(); return ok;
    };
    auto key = [&](WPARAM value) { SendMessageW(window, WM_KEYDOWN, value, 1); SendMessageW(window, WM_KEYUP, value, 1); };
    auto received = [&](const wchar_t* expected, CONTROLTYPEID expectedType) {
        IUIAutomationElement* element = nullptr; BSTR id = nullptr;
        BOOL focused = FALSE, focusable = FALSE; CONTROLTYPEID type = 0;
        const bool ok = SUCCEEDED(subscribed) && WaitForSingleObject(events->received, 5000) == WAIT_OBJECT_0 &&
            SUCCEEDED(automation->GetFocusedElement(&element)) && element &&
            SUCCEEDED(element->get_CurrentAutomationId(&id)) && id && std::wstring(id) == expected &&
            SUCCEEDED(element->get_CurrentControlType(&type)) && type == expectedType &&
            SUCCEEDED(element->get_CurrentHasKeyboardFocus(&focused)) && SUCCEEDED(element->get_CurrentIsKeyboardFocusable(&focusable)) &&
            test::optionFocusMatches(focused != FALSE, focusable != FALSE, true);
        SysFreeString(id); if (element) element->Release(); return ok;
    };
    bool ok = focus(L"form.title.pages.tabs");
    events->expect(L"v02", UIA_TabItemControlTypeId); key(VK_RIGHT);
    ok = received(L"v02", UIA_TabItemControlTypeId) && ok;
    auto tabs = findId(automation, root, L"form.title.pages.tabs");
    ok = test::focusPreservesSelection(selectedIdIs(tabs, L"v01"), selectedIdIs(tabs, L"v02")) && ok;
    events->expect(L"v03", UIA_TabItemControlTypeId);
    ok = focus(L"v03") && ok;
    ok = received(L"v03", UIA_TabItemControlTypeId) &&
        test::focusPreservesSelection(selectedIdIs(tabs, L"v01"), selectedIdIs(tabs, L"v03")) && ok;
    if (tabs) tabs->Release();
    ok = focus(L"form.region") && ok;
    events->expect(L"north", UIA_ListItemControlTypeId); key(VK_RETURN);
    ok = received(L"north", UIA_ListItemControlTypeId) && ok;
    events->expect(L"central", UIA_ListItemControlTypeId); key(VK_DOWN);
    ok = received(L"central", UIA_ListItemControlTypeId) && ok;
    events->expect(L"form.region", UIA_ComboBoxControlTypeId); key(VK_ESCAPE);
    ok = received(L"form.region", UIA_ComboBoxControlTypeId) && ok;
    auto closedOption = findId(automation, root, L"north"); BOOL focusable = TRUE;
    ok = closedOption && SUCCEEDED(closedOption->get_CurrentIsKeyboardFocusable(&focusable)) &&
        test::optionFocusabilityMatches(focusable != FALSE, true, true, false) && ok;
    if (closedOption) closedOption->Release();
    ok = focus(L"form.mode") && ok;
    events->expect(L"focus", UIA_RadioButtonControlTypeId); key(VK_DOWN);
    ok = received(L"focus", UIA_RadioButtonControlTypeId) && ok;
    selectId(automation, root, L"normal");
    if (SUCCEEDED(subscribed)) automation->RemoveFocusChangedEventHandler(events);
    events->Release();
    std::cout << "UIA23 option focus: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok;
}

class TextEvents final : public UiaEventHandler<TextEvents, IUIAutomationEventHandler> {
public:
    EVENTID expected = UIA_Text_TextSelectionChangedEventId;
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override { if (id==expected) SetEvent(received); return S_OK; }
};

bool selectedIdIs(IUIAutomationElement* container, const wchar_t* expected) {
    IUIAutomationSelectionPattern* p = nullptr; IUIAutomationElementArray* selected = nullptr; IUIAutomationElement* item = nullptr; BSTR id = nullptr; int n = 0;
    bool ok = container && SUCCEEDED(container->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->GetCurrentSelection(&selected)) && selected && SUCCEEDED(selected->get_Length(&n)) && n == 1 && SUCCEEDED(selected->GetElement(0, &item)) && item && SUCCEEDED(item->get_CurrentAutomationId(&id)) && id && std::wstring(id) == expected;
    SysFreeString(id); if (item) item->Release(); if (selected) selected->Release(); if (p) p->Release(); return ok;
}
bool imeProbe(HWND window) {
    IUIAutomation* automation = nullptr; IUIAutomationElement* root = nullptr; IUIAutomationElement* field = nullptr;
    IUIAutomationValuePattern* value = nullptr; BSTR before = nullptr; BSTR after = nullptr;
    bool ok = window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(window, &root)) && (field = findId(automation, root, L"form.name")) &&
        SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value &&
        SUCCEEDED(value->get_CurrentValue(&before)) && before;
    BSTR attempt = SysAllocString(L"UIA replacement"); HRESULT change = value ? value->SetValue(attempt) : E_FAIL;
    HRESULT read = value ? value->get_CurrentValue(&after) : E_FAIL;
    ok = ok && before && std::wstring(before) == L"UIA baseline" && change == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && SUCCEEDED(read) && after && std::wstring(before) == after;
    std::cout << "UIA21 cross-process value: " << (ok ? "PASS" : "FAIL") << " set=0x" << std::hex << change << '\n';
    SysFreeString(attempt); SysFreeString(before); SysFreeString(after); if(value)value->Release(); if(field)field->Release(); if(root)root->Release(); if(automation)automation->Release(); return ok;
}
bool imeSimulation(const wchar_t* host, const wchar_t* appearance) {
    std::wstring command=L"\""+std::wstring(host)+L"\" "+appearance; STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) return false;
    CloseHandle(process.hThread); Search search{process.dwProcessId};
    for (int i=0;i<100 && !search.window;++i) { EnumWindows(findWindow,reinterpret_cast<LPARAM>(&search)); Sleep(100); }
    bool ok=search.window && SendMessageW(search.window,WM_APP+80,31,0)==0 && SendMessageW(search.window,WM_APP+80,11,0)==0 &&
        SendMessageW(search.window,WM_APP+80,30,0)==1 && imeProbe(search.window) && SendMessageW(search.window,WM_APP+80,30,0)==1;
    if(search.window) { SendMessageW(search.window,WM_APP+80,12,0); PostMessageW(search.window,WM_CLOSE,0,0); }
    if(WaitForSingleObject(process.hProcess,5000)==WAIT_TIMEOUT) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); ok=false; }
    CloseHandle(process.hProcess); std::cout << "UIA21 simulated composition: " << (ok ? "PASS" : "FAIL") << '\n'; return ok;
}
BOOL CALLBACK collectDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    MONITORINFO info{sizeof(info)}; if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcWork); return TRUE;
}
struct DpiGeometry { RECT field{}; double glyph[4]{}; UINT dpi = 0; };
bool dpiGeometry(HWND window, IUIAutomationElement* field, IUIAutomationTextRange* first, DpiGeometry& result) {
    result.dpi = GetDpiForWindow(window); SAFEARRAY* rects = nullptr;
    bool ok = SUCCEEDED(field->get_CurrentBoundingRectangle(&result.field)) && SUCCEEDED(first->GetBoundingRectangles(&rects)) && rects;
    LONG lo=0, hi=-1; if (rects) { SafeArrayGetLBound(rects,1,&lo); SafeArrayGetUBound(rects,1,&hi); }
    if (hi-lo+1 == 4) for (LONG i=0;i<4;++i) { LONG index=lo+i; SafeArrayGetElement(rects,&index,&result.glyph[i]); } else ok=false;
    if (rects) SafeArrayDestroy(rects);
    auto p0=SendMessageW(window, WM_APP+80,19,0), p1=SendMessageW(window,WM_APP+80,19,1);
    auto f0=SendMessageW(window, WM_APP+80,35,0), f1=SendMessageW(window,WM_APP+80,35,1);
    ok = ok && result.glyph[2]>0 && result.glyph[3]>0 &&
        std::abs(result.field.left-static_cast<short>(LOWORD(f0)))<=1 && std::abs(result.field.top-static_cast<short>(HIWORD(f0)))<=1 &&
        std::abs(result.field.right-static_cast<short>(LOWORD(f1)))<=1 && std::abs(result.field.bottom-static_cast<short>(HIWORD(f1)))<=1 &&
        std::abs(result.glyph[0]-static_cast<short>(LOWORD(p0)))<=1 &&
        std::abs(result.glyph[1]-static_cast<short>(HIWORD(p0)))<=1 &&
        std::abs(result.glyph[2]-(static_cast<short>(LOWORD(p1))-static_cast<short>(LOWORD(p0))))<=1 &&
        result.glyph[0]>=result.field.left && result.glyph[1]>=result.field.top &&
        result.glyph[0]+result.glyph[2]<=result.field.right && result.glyph[1]+result.glyph[3]<=result.field.bottom;
    return ok;
}
bool dpiCase(const wchar_t* host, const wchar_t* appearance) {
    std::vector<RECT> displays; EnumDisplayMonitors(nullptr,nullptr,collectDisplay,reinterpret_cast<LPARAM>(&displays));
    if (displays.size()<2) { std::cout << "UIA17-DPI: BLOCKED two monitors required\n"; return false; }
    std::wstring command=L"\""+std::wstring(host)+L"\" "+appearance; STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) return false;
    CloseHandle(process.hThread); Search search{process.dwProcessId};
    for(int i=0;i<100 && !search.window;++i) { EnumWindows(findWindow,reinterpret_cast<LPARAM>(&search)); Sleep(100); }
    IUIAutomation *automation=nullptr; IUIAutomationElement *root=nullptr,*field=nullptr; IUIAutomationTextPattern* pattern=nullptr; IUIAutomationTextRange* first=nullptr;
    bool ok=search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(search.window,&root)) && (field=findId(automation,root,L"form.name")) &&
        SUCCEEDED(field->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern))) && pattern &&
        SUCCEEDED(pattern->get_DocumentRange(&first)) && first && SUCCEEDED(first->ExpandToEnclosingUnit(TextUnit_Character));
    auto events=new TextEvents(); events->expected=UIA_LayoutInvalidatedEventId;
    HRESULT subscribed=ok ? automation->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId,root,TreeScope_Element,nullptr,events) : E_FAIL;
    int a=-1,b=-1; UINT dpiA=0,dpiB=0;
    for(size_t i=0;ok && i<displays.size();++i) {
        SetWindowPos(search.window,nullptr,displays[i].left+20,displays[i].top+20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); Sleep(700);
        UINT dpi=GetDpiForWindow(search.window);
        std::cout << "UIA17-DPI display " << i << " work=" << displays[i].left << ',' << displays[i].top << ',' << displays[i].right << ',' << displays[i].bottom << " dpi=" << dpi << '\n';
        if(a<0) { a=static_cast<int>(i); dpiA=dpi; }
        else if(b<0 && dpi!=dpiA) { b=static_cast<int>(i); dpiB=dpi; }
    }
    if(ok && b<0) { std::cout << "UIA17-DPI: BLOCKED fewer than two distinct display DPIs\n"; ok=false; }
    auto move=[&](int index, DpiGeometry& geometry) {
        ResetEvent(events->received); auto r=displays[index]; SetWindowPos(search.window,nullptr,r.left+20,r.top+20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); Sleep(700);
        bool matched=dpiGeometry(search.window,field,first,geometry); DWORD received=WaitForSingleObject(events->received,3000);
        return matched && SUCCEEDED(subscribed) && received==WAIT_OBJECT_0;
    };
    DpiGeometry start{},next{},back{};
    if(ok) { auto r=displays[a]; SetWindowPos(search.window,nullptr,r.left+20,r.top+20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); Sleep(700); ok=dpiGeometry(search.window,field,first,start); }
    if(ok) ok=move(b,next) && move(a,back);
    if(ok) ok=start.dpi==dpiA && next.dpi==dpiB && back.dpi==dpiA && std::abs(next.glyph[3]-start.glyph[3]*double(dpiB)/dpiA)<=2 && std::abs(back.glyph[3]-start.glyph[3])<=1 &&
        std::abs((next.field.bottom-next.field.top)-(start.field.bottom-start.field.top)*double(dpiB)/dpiA)<=2 &&
        std::abs((back.field.bottom-back.field.top)-(start.field.bottom-start.field.top))<=1;
    std::cout << "UIA17-DPI physical transitions: " << (ok ? "PASS" : "FAIL") << " dpi=" << start.dpi << ',' << next.dpi << ',' << back.dpi
              << " field-height=" << start.field.bottom-start.field.top << ',' << next.field.bottom-next.field.top << ',' << back.field.bottom-back.field.top
              << " glyph-height=" << start.glyph[3] << ',' << next.glyph[3] << ',' << back.glyph[3] << '\n';
    if(SUCCEEDED(subscribed)) automation->RemoveAutomationEventHandler(UIA_LayoutInvalidatedEventId,root,events); events->Release();
    if(first)first->Release(); if(pattern)pattern->Release(); if(field)field->Release(); if(root)root->Release(); if(automation)automation->Release();
    if(search.window)PostMessageW(search.window,WM_CLOSE,0,0); if(WaitForSingleObject(process.hProcess,5000)==WAIT_TIMEOUT) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); ok=false; } CloseHandle(process.hProcess); return ok;
}
bool preflight(const wchar_t* host, const wchar_t* appearance) {
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS); int width=GetSystemMetrics(SM_CXSCREEN), height=GetSystemMetrics(SM_CYSCREEN);
    std::cout << "Preflight screen=" << width << 'x' << height << " interactive=" << bool(desktop) << '\n';
    if(desktop) CloseDesktop(desktop); if(!desktop || width<=0 || height<=0) { std::cout << "Preflight: BLOCKED desktop unavailable\n"; return false; }
    std::wstring command=L"\""+std::wstring(host)+L"\" "+appearance; STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) { std::cout << "Preflight: BLOCKED host did not start\n"; return false; }
    CloseHandle(process.hThread); Search search{process.dwProcessId};
    for(int i=0;i<100 && !search.window;++i) { EnumWindows(findWindow,reinterpret_cast<LPARAM>(&search)); if(WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0) break; Sleep(100); }
    IUIAutomation* automation=nullptr; IUIAutomationElement* root=nullptr; IUIAutomationElement* field=nullptr;
    bool ok=search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(search.window,&root)) && (field=findId(automation,root,L"form.name"));
    std::cout << "Preflight: " << (ok ? "PASS" : "BLOCKED host OpenGL or UIA unavailable") << '\n';
    if(field)field->Release(); if(root)root->Release(); if(automation)automation->Release();
    if(search.window)PostMessageW(search.window,WM_CLOSE,0,0); if(WaitForSingleObject(process.hProcess,5000)==WAIT_TIMEOUT) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); ok=false; }
    CloseHandle(process.hProcess); return ok;
}
bool rendererFailureCase(const wchar_t* host, const wchar_t* appearance, bool unhandled) {
    std::wstring command = L"\"" + std::wstring(host) + L"\" " + appearance + (unhandled ? L" --unhandled" : L"");
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    Search search{process.dwProcessId};
    for (int i = 0; i < 100 && !search.window; ++i) { EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search)); Sleep(100); }
    IUIAutomation* automation = nullptr; IUIAutomationElement* root = nullptr; IUIAutomationElement* field = nullptr;
    IUIAutomationValuePattern* value = nullptr; IUIAutomationElement* apply = nullptr; IUIAutomationElement* count = nullptr;
    bool ok = search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(search.window, &root)) && (field = findId(automation, root, L"form.name")) &&
        SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value;
    BSTR before = nullptr; BSTR countBefore = nullptr;
    if (ok) { apply = findId(automation, root, L"form.apply"); count = findId(automation, root, L"result.count");
        ok = apply && count && SUCCEEDED(count->get_CurrentName(&countBefore)) && countBefore && SUCCEEDED(value->get_CurrentValue(&before)) && before; }
    if (ok) ok = SendMessageW(search.window, WM_APP + 80, 26, 0) == 0;
    if (unhandled) {
        if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) ok = false;
        DWORD code = 0; if (!GetExitCodeProcess(process.hProcess, &code) || code != 12) ok = false;
        VARIANT prop; VariantInit(&prop);
        HRESULT unavailable = field ? field->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &prop) : E_FAIL;
        VariantClear(&prop);
        HRESULT unavailableApply = apply ? apply->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &prop) : E_FAIL;
        VariantClear(&prop); ok = ok && unavailable == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && unavailableApply == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
    } else {
        BSTR after = nullptr; HRESULT read = value ? value->get_CurrentValue(&after) : E_FAIL;
        HRESULT denied = invokeId(automation, root, L"form.apply"); BSTR countDenied = nullptr;
        HRESULT countHr = count ? count->get_CurrentName(&countDenied) : E_FAIL;
        ok = ok && SUCCEEDED(read) && after && before && std::wstring(before) == after && denied == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) &&
            SUCCEEDED(countHr) && countDenied && std::wstring(countBefore) == countDenied;
        SysFreeString(after); SysFreeString(countDenied);
        if (ok) ok = SendMessageW(search.window, WM_APP + 80, 27, 0) == 0 &&
            SendMessageW(search.window, WM_APP + 80, 28, 0) == 0 && SUCCEEDED(invokeId(automation, root, L"form.apply"));
        BSTR countAfter = nullptr; HRESULT afterHr = count ? count->get_CurrentName(&countAfter) : E_FAIL;
        ok = ok && SUCCEEDED(afterHr) && countAfter && std::wstring(countBefore) != countAfter; SysFreeString(countAfter);
        if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) ok = false;
    }
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
    SysFreeString(before); SysFreeString(countBefore); if (apply) apply->Release(); if (count) count->Release(); if (value) value->Release(); if (field) field->Release(); if (root) root->Release(); if (automation) automation->Release();
    CloseHandle(process.hProcess); return ok;
}
bool rendererFailureCases(const wchar_t* host, const wchar_t* appearance) {
    bool handled = rendererFailureCase(host, appearance, false);
    bool unhandled = rendererFailureCase(host, appearance, true);
    std::cout << "UIA18 renderer lifecycle: " << (handled && unhandled ? "PASS" : "FAIL") << " handled=" << handled << " unhandled=" << unhandled << '\n';
    return handled && unhandled;
}
bool accessibilityCase(const wchar_t* host, const wchar_t* appearance) {
    bool pass = true;
    for (const auto mode : {L"", L"--accessibility-enabled", L"--accessibility-disabled", L"--accessibility-mixed"}) {
        const bool enabled = std::wstring(mode) != L"--accessibility-disabled";
        std::wstring command = L"\"" + std::wstring(host) + L"\" " + appearance + L" " + mode;
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
        CloseHandle(process.hThread);
        Search search{process.dwProcessId};
        for (int i=0; i<100 && !search.window; ++i) { EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search)); Sleep(50); }
        IUIAutomation* uia = nullptr; IUIAutomationElement* root = nullptr;
        bool ok = search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && SUCCEEDED(uia->ElementFromHandle(search.window, &root));
        if (ok) {
            if (std::wstring(mode) == L"--accessibility-mixed" && SendMessageW(search.window, WM_APP+81, 0, 0) == reinterpret_cast<LRESULT>(search.window)) {
                // EnumWindows can discover the companion first; its owner is the other window in this process.
                HWND next = GetWindow(search.window, GW_HWNDNEXT);
                while (next) { DWORD pid=0; GetWindowThreadProcessId(next, &pid); if (pid == process.dwProcessId && IsWindowVisible(next)) break; next=GetWindow(next, GW_HWNDNEXT); }
                if (next) { root->Release(); root=nullptr; search.window=next; ok &= SUCCEEDED(uia->ElementFromHandle(next, &root)); }
            }
            auto field = findId(uia, root, L"form.name");
            ok = enabled ? field != nullptr : field == nullptr;
            if (field) field->Release();
            auto events = new PropertyEvents();
            events->expected = UIA_ValueValuePropertyId; events->stringValue = true; events->expectedString = L"UIA baseline";
            PROPERTYID property = UIA_ValueValuePropertyId;
            if (enabled) ok &= SUCCEEDED(invokeId(uia, root, L"fixture.name_write"));
            else ok &= FAILED(invokeId(uia, root, L"fixture.name_write"));
            ok &= SUCCEEDED(uia->AddPropertyChangedEventHandlerNativeArray(root, TreeScope_Subtree, nullptr, events, &property, 1));
            ok &= SendMessageW(search.window, WM_APP+80, 31, 0) == 0;
            ok &= WaitForSingleObject(events->received, enabled ? 5000 : 500) == (enabled ? WAIT_OBJECT_0 : WAIT_TIMEOUT);
            auto focus = new FocusEvents();
            auto structure = new StructureEvents(); structure->lcarsOnly = true;
            ok &= SUCCEEDED(uia->AddFocusChangedEventHandler(nullptr, focus));
            ok &= SUCCEEDED(uia->AddStructureChangedEventHandler(root, TreeScope_Subtree, nullptr, structure));
            ok &= SendMessageW(search.window, WM_APP+80, 20, 0) == 0;
            ok &= WaitForSingleObject(focus->applyReceived, enabled ? 5000 : 500) == (enabled ? WAIT_OBJECT_0 : WAIT_TIMEOUT);
            ok &= SendMessageW(search.window, WM_APP+80, 2, 0) == 0;
            ok &= WaitForSingleObject(structure->received, enabled ? 5000 : 500) == (enabled ? WAIT_OBJECT_0 : WAIT_TIMEOUT);
            if (std::wstring(mode) == L"--accessibility-mixed") {
                HWND other = nullptr;
                for (int i=0; i<100 && !other; ++i) { other = reinterpret_cast<HWND>(SendMessageW(search.window, WM_APP+81, 0, 0)); if (!other) Sleep(50); }
                IUIAutomationElement* disabled = nullptr;
                ok &= other && SUCCEEDED(uia->ElementFromHandle(other, &disabled));
                if (disabled) {
                    auto missing = findId(uia, disabled, L"form.name"); ok &= !missing; if (missing) missing->Release();
                    ok &= FAILED(invokeId(uia, disabled, L"fixture.name_write"));
                    Sleep(1000);
                    ResetEvent(events->received); ResetEvent(focus->applyReceived); ResetEvent(structure->received);
                    ok &= SUCCEEDED(uia->AddPropertyChangedEventHandlerNativeArray(disabled, TreeScope_Subtree, nullptr, events, &property, 1));
                    ok &= SUCCEEDED(uia->AddStructureChangedEventHandler(disabled, TreeScope_Subtree, nullptr, structure));
                    ok &= SendMessageW(other, WM_APP+80, 31, 0) == 0;
                    ok &= SendMessageW(other, WM_APP+82, 0, 0) == 1;
                    ok &= SendMessageW(other, WM_APP+80, 20, 0) == 0;
                    ok &= SendMessageW(other, WM_APP+82, 1, 0) == 1;
                    ok &= SendMessageW(other, WM_APP+80, 2, 0) == 0;
                    ok &= SendMessageW(other, WM_APP+82, 2, 0) == 1;
                    ok &= WaitForSingleObject(events->received, 500) == WAIT_TIMEOUT && WaitForSingleObject(focus->applyReceived, 500) == WAIT_TIMEOUT && WaitForSingleObject(structure->received, 500) == WAIT_TIMEOUT;
                    disabled->Release();
                }
                if (other) PostMessageW(other, WM_CLOSE, 0, 0);
            }
            uia->RemoveAllEventHandlers(); events->Release(); focus->Release(); structure->Release();
        }
        if (root) root->Release(); if (uia) uia->Release();
        if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 1); ok = false; }
        CloseHandle(process.hProcess);
        std::wcout << L"Accessibility " << mode << L": " << (ok ? L"PASS" : L"FAIL") << L'\n'; pass &= ok;
    }
    return pass;
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 3 && std::wstring(argv[1]) == L"--ime-probe") {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
        bool ok = imeProbe(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argv[2])))); CoUninitialize(); return ok ? 0 : 1;
    }
    if ((argc != 3 && argc != 4) || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (argc == 4) { const std::wstring mode(argv[3]); bool ok = mode == L"--accessibility" ? accessibilityCase(argv[1], argv[2]) : mode == L"--uia18" ? rendererFailureCases(argv[1], argv[2]) : mode == L"--uia-dpi" ? dpiCase(argv[1], argv[2]) : mode == L"--preflight" ? preflight(argv[1], argv[2]) : mode == L"--ime-simulation" ? imeSimulation(argv[1], argv[2]) : false; CoUninitialize(); return ok ? 0 : 1; }
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2];
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 1;
    CloseHandle(process.hThread);
    Search search{process.dwProcessId};
    for (int i = 0; i < 100 && !search.window; ++i) {
        EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search)); Sleep(100);
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
        RECT windowRect{}, rootRect{};
        IUIAutomationCondition *titleCondition=nullptr, *buttonCondition=nullptr;
        IUIAutomationElement *title=nullptr, *close=nullptr; IUIAutomationElementArray* buttons=nullptr;
        VARIANT type; VariantInit(&type); type.vt=VT_I4; type.lVal=UIA_TitleBarControlTypeId;
        if (SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&titleCondition)) &&
            SUCCEEDED(root->FindFirst(TreeScope_Children,titleCondition,&title)) && title) {
            type.lVal=UIA_ButtonControlTypeId;
            if (SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&buttonCondition)) &&
                SUCCEEDED(title->FindAll(TreeScope_Children,buttonCondition,&buttons)) && buttons) {
                int count=0; buttons->get_Length(&count); if(count>0) buttons->GetElement(count-1,&close);
            }
        }
        RECT closeRect{};
        bool rootBounds = GetWindowRect(search.window, &windowRect) &&
            SUCCEEDED(root->get_CurrentBoundingRectangle(&rootRect)) && EqualRect(&windowRect, &rootRect);
        bool closeBounds = close && SUCCEEDED(close->get_CurrentBoundingRectangle(&closeRect)) &&
            closeRect.right > closeRect.left && closeRect.bottom > closeRect.top &&
            closeRect.right <= windowRect.right && closeRect.top >= windowRect.top;
        std::cout << "UIA native frame root_bounds=" << rootBounds << " close_bounds=" << closeBounds << '\n';
        pass = rootBounds && closeBounds;
        if (buttons) buttons->Release(); if (close) close->Release(); if (title) title->Release();
        if (buttonCondition) buttonCondition->Release(); if (titleCondition) titleCondition->Release();
    }
    if (pass) pass = optionFocusCase(automation, root, search.window);
    auto focusEvents = new FocusEvents(); auto focusSubscribed = pass ? automation->AddFocusChangedEventHandler(nullptr, focusEvents) : E_FAIL;
    if (pass) {
        CONTROLTYPEID type = 0; BSTR name = nullptr, labelId = nullptr; RECT bounds{}, client{};
        IUIAutomationElement* label = nullptr;
        auto hidden = findId(automation, root, L"form.title.pages.v03.progress");
        bool field = SUCCEEDED(element->get_CurrentControlType(&type)) && type == UIA_EditControlTypeId &&
            SUCCEEDED(element->get_CurrentName(&name)) && name && std::wstring(name) == L"顯示名稱" &&
            SUCCEEDED(element->get_CurrentLabeledBy(&label)) && label && SUCCEEDED(label->get_CurrentAutomationId(&labelId)) &&
            labelId && std::wstring(labelId) == L"form.name.label" && SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds));
        IUIAutomationElementArray* descriptions = nullptr;
        int descriptionCount = 0; bool described = false;
        if (SUCCEEDED(element->get_CurrentDescribedBy(&descriptions)) && descriptions && SUCCEEDED(descriptions->get_Length(&descriptionCount))) {
            for (int i = 0; i < descriptionCount; ++i) {
                IUIAutomationElement* labelElement = nullptr; BSTR id = nullptr;
                if (SUCCEEDED(descriptions->GetElement(i, &labelElement)) && labelElement &&
                    SUCCEEDED(labelElement->get_CurrentAutomationId(&id)) && id && std::wstring(id) == L"form.name.error") described = true;
                if (labelElement) labelElement->Release(); SysFreeString(id);
            }
        }
        field = field && described;
        if (descriptions) descriptions->Release();
        GetClientRect(search.window, &client); POINT origin{0, 0}; ClientToScreen(search.window, &origin);
        field = field && bounds.left >= origin.x && bounds.top >= origin.y && bounds.right <= origin.x + client.right && bounds.bottom <= origin.y + client.bottom && bounds.right > bounds.left && bounds.bottom > bounds.top && !hidden;
        if (hidden) hidden->Release(); if (label) label->Release(); SysFreeString(name); SysFreeString(labelId);
        bool nameFocus = SUCCEEDED(focusSubscribed) && SUCCEEDED(element->SetFocus()) && WaitForSingleObject(focusEvents->nameReceived, 5000) == WAIT_OBJECT_0;
        std::cout << "UIA14 field focus: " << (nameFocus ? "PASS" : "FAIL") << '\n'; pass &= nameFocus;
        auto structures = new StructureEvents();
        auto structureSubscribed = automation->AddStructureChangedEventHandler(root, TreeScope_Element, nullptr, structures);
        IUIAutomationValuePattern* cachedValue = nullptr;
        HRESULT cachedPatternHr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&cachedValue));
        HRESULT tabSelectHr = selectId(automation, root, L"v02");
        BSTR cachedText = nullptr; HRESULT cachedReadHr = cachedValue ? cachedValue->get_CurrentValue(&cachedText) : E_FAIL;
        BSTR hiddenInput = SysAllocString(L"hidden"); HRESULT cachedWriteHr = cachedValue ? cachedValue->SetValue(hiddenInput) : E_FAIL;
        bool cachedCase = SUCCEEDED(tabSelectHr) && SUCCEEDED(cachedPatternHr) && SUCCEEDED(cachedReadHr) && cachedText && cachedWriteHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        std::cout << "UIA10 cached edit: " << (cachedCase ? "PASS" : "FAIL") << '\n'; pass &= cachedCase;
        SysFreeString(cachedText); SysFreeString(hiddenInput); if (cachedValue) cachedValue->Release();
        bool structure = SUCCEEDED(structureSubscribed) && WaitForSingleObject(structures->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(structureSubscribed)) automation->RemoveStructureChangedEventHandler(root, structures);
        structures->Release();
        std::cout << "UIA10 structure event: " << (structure ? "PASS" : "FAIL") << '\n'; pass &= structure;
        auto first = findId(automation, root, L"T005"); auto second = findId(automation, root, L"T006");
        BSTR firstName = nullptr, secondName = nullptr, firstId = nullptr, secondId = nullptr; CONTROLTYPEID firstType = 0, secondType = 0;
        bool items = first && second && SUCCEEDED(first->get_CurrentControlType(&firstType)) && SUCCEEDED(second->get_CurrentControlType(&secondType)) &&
            firstType == UIA_ListItemControlTypeId && secondType == UIA_ListItemControlTypeId &&
            SUCCEEDED(first->get_CurrentName(&firstName)) && SUCCEEDED(second->get_CurrentName(&secondName)) && firstName && secondName &&
            std::wstring(firstName) == L"共用範本" && std::wstring(firstName) == std::wstring(secondName) &&
            SUCCEEDED(first->get_CurrentAutomationId(&firstId)) && SUCCEEDED(second->get_CurrentAutomationId(&secondId)) && firstId && secondId && std::wstring(firstId) != std::wstring(secondId);
        bool listCase = SUCCEEDED(selectId(automation, root, L"T006")) && SUCCEEDED(invokeId(automation, root, L"form.title.pages.v02.sort"));
        IUIAutomationSelectionItemPattern* listItem = nullptr; BOOL selectedItem = FALSE;
        listCase = listCase && second && SUCCEEDED(second->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&listItem))) && listItem && SUCCEEDED(listItem->get_CurrentIsSelected(&selectedItem)) && selectedItem;
        if (listItem) listItem->Release();
        listCase = listCase && SUCCEEDED(invokeId(automation, root, L"form.title.pages.v02.disable")) && selectId(automation, root, L"T008") == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        BSTR itemHelp = nullptr;
        const bool itemHelpEmpty = first && SUCCEEDED(first->get_CurrentHelpText(&itemHelp)) && test::optionHelpTextIsEmpty(SysStringLen(itemHelp));
        SysFreeString(itemHelp);
        std::cout << "UIA09 option HelpText empty: " << (itemHelpEmpty ? "PASS" : "FAIL") << '\n'; pass &= itemHelpEmpty;
        auto disabledRow = findId(automation, root, L"T008"); BOOL rowFocusable = TRUE, rowEnabled = TRUE;
        const bool disabledNotFocusable = disabledRow && SUCCEEDED(disabledRow->get_CurrentIsEnabled(&rowEnabled)) && !rowEnabled &&
            SUCCEEDED(disabledRow->get_CurrentIsKeyboardFocusable(&rowFocusable)) && test::disabledRowCannotTakeFocus(rowEnabled != FALSE, rowFocusable != FALSE);
        if (disabledRow) disabledRow->Release();
        std::cout << "UIA09 disabled row not focusable: " << (disabledNotFocusable ? "PASS" : "FAIL") << '\n'; pass &= disabledNotFocusable;
        listCase = listCase && SUCCEEDED(invokeId(automation, root, L"form.title.pages.v02.sort"));
        bool listFocus = SUCCEEDED(focusSubscribed) && SendMessageW(search.window, WM_APP + 80, 22, 0) == 0 && WaitForSingleObject(focusEvents->received, 10000) == WAIT_OBJECT_0;
        { std::lock_guard lock(focusEvents->mutex); listFocus = listFocus && focusEvents->observed.size() >= 2 && focusEvents->observed[0] == L"T003" && focusEvents->observed[1] == L"T004"; }
        std::cout << "UIA14 list focus: " << (listFocus ? "PASS" : "FAIL") << '\n'; pass &= listFocus;
        pass &= patterns(automation, root);
        listCase = listCase && SUCCEEDED(invokeId(automation, root, L"form.title.pages.v02.hundred"));
        auto lastItem = findId(automation, root, L"T100"); BOOL lastOffscreen = FALSE;
        listCase = listCase && lastItem && SUCCEEDED(lastItem->get_CurrentIsOffscreen(&lastOffscreen)) && lastOffscreen;
        if (lastItem) lastItem->Release();
        listCase = listCase && SUCCEEDED(invokeId(automation, root, L"form.title.pages.v02.twenty"));
        std::cout << "UIA09 list: " << (listCase ? "PASS" : "FAIL") << '\n'; pass &= listCase;
        SysFreeString(firstName); SysFreeString(secondName); SysFreeString(firstId); SysFreeString(secondId); if (first) first->Release(); if (second) second->Release();
        SendMessageW(search.window, WM_APP + 80, 5, 0);
        pass = pass && field && items && patterns(automation, root);
        std::cout << "UIA01 details field=" << field << " items=" << items << '\n';
    }
    if (pass) {
        IUIAutomationValuePattern* valuePattern = nullptr;
        HRESULT patternHr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern));
        auto applied = findId(automation, root, L"result.applied");
        BSTR appliedBefore = nullptr, appliedAfter = nullptr;
        HRESULT beforeHr = applied ? applied->get_CurrentName(&appliedBefore) : E_FAIL;
        BSTR input = SysAllocString(L"\u5de5\u4f5c\u7ad9 A"), actual = nullptr;
        HRESULT setHr = valuePattern ? valuePattern->SetValue(input) : E_FAIL;
        HRESULT readHr = valuePattern ? valuePattern->get_CurrentValue(&actual) : E_FAIL;
        HRESULT afterHr = applied ? applied->get_CurrentName(&appliedAfter) : E_FAIL;
        SendMessageW(search.window, WM_APP + 80, 9, 0);
        BOOL readOnly = FALSE;
        HRESULT readonlyHr = valuePattern ? valuePattern->get_CurrentIsReadOnly(&readOnly) : E_FAIL;
        HRESULT rejected = valuePattern ? valuePattern->SetValue(input) : E_FAIL;
        SendMessageW(search.window, WM_APP + 80, 10, 0);
        bool valueOk = SUCCEEDED(beforeHr) && SUCCEEDED(afterHr) && appliedBefore && appliedAfter &&
            std::wstring(appliedBefore, SysStringLen(appliedBefore)) == std::wstring(appliedAfter, SysStringLen(appliedAfter)) && SUCCEEDED(patternHr) && SUCCEEDED(setHr) && SUCCEEDED(readHr) && actual &&
            std::wstring(actual, SysStringLen(actual)) == L"\u5de5\u4f5c\u7ad9 A" && SUCCEEDED(readonlyHr) && readOnly && rejected == UIA_E_INVALIDOPERATION;
        std::cout << "UIA07 Value: " << (valueOk ? "PASS" : "FAIL") << '\n';
        pass &= valueOk; SysFreeString(appliedBefore); SysFreeString(appliedAfter); if (applied) applied->Release(); SysFreeString(input); SysFreeString(actual); if (valuePattern) valuePattern->Release();
    }
    if (pass) {
        SendMessageW(search.window, WM_APP + 80, 17, 14);
        IUIAutomationTextPattern* pattern = nullptr; IUIAutomationTextRange* range = nullptr;
        HRESULT ph = element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern));
        HRESULT rh = pattern ? pattern->get_DocumentRange(&range) : E_FAIL;
        BSTR content = nullptr; HRESULT th = range ? range->GetText(-1, &content) : E_FAIL;
        bool textOk = SUCCEEDED(ph) && SUCCEEDED(rh) && SUCCEEDED(th) && content &&
            std::wstring(content, SysStringLen(content)) == L"\u5de5\u4f5c\u7ad9 A";
        auto events = new TextEvents();
        HRESULT subscribed = automation->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, element, TreeScope_Element, nullptr, events);
        int moved = 0; HRESULT moveHr = range ? range->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -3, &moved) : E_FAIL;
        HRESULT selectHr = range ? range->Select() : E_FAIL;
        auto selection = SendMessageW(search.window, WM_APP + 80, 18, 0);
        bool selected = SUCCEEDED(moveHr) && moved == -3 && SUCCEEDED(selectHr) && LOWORD(selection) == 0 && HIWORD(selection) == 2 && SUCCEEDED(subscribed) && WaitForSingleObject(events->received, 2000) == WAIT_OBJECT_0;
        IUIAutomationTextRangeArray* selectedRanges = nullptr; IUIAutomationTextRange* selectedRange = nullptr;
        int selectedCount = 0, startCmp = 1, endCmp = 1;
        if (pattern && SUCCEEDED(pattern->GetSelection(&selectedRanges)) && selectedRanges) {
            selectedRanges->get_Length(&selectedCount);
            if (selectedCount == 1 && SUCCEEDED(selectedRanges->GetElement(0, &selectedRange)) && selectedRange && range) {
                selectedRange->CompareEndpoints(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_Start, &startCmp);
                selectedRange->CompareEndpoints(TextPatternRangeEndpoint_End, range, TextPatternRangeEndpoint_End, &endCmp);
            }
        }
        selected = selected && selectedCount == 1 && startCmp == 0 && endCmp == 0;
        if (selectedRange) selectedRange->Release(); if (selectedRanges) selectedRanges->Release();
        bool wholeUnits = true;
        // Words are real units now (#83, checked on ASCII text by UA11 in the demo client): this Chinese text's words
        // would depend on the system dictionary, so only the units that stay whole here are listed.
        for (auto unit : {TextUnit_Line, TextUnit_Paragraph, TextUnit_Document}) {
            IUIAutomationTextRange* expanded = nullptr; BSTR all = nullptr;
            HRESULT eh = range ? range->Clone(&expanded) : E_FAIL;
            if (SUCCEEDED(eh) && expanded) eh = expanded->ExpandToEnclosingUnit(unit);
            if (SUCCEEDED(eh) && expanded) eh = expanded->GetText(-1, &all);
            wholeUnits = wholeUnits && SUCCEEDED(eh) && all && std::wstring(all, SysStringLen(all)) == L"\u5de5\u4f5c\u7ad9 A";
            SysFreeString(all); if (expanded) expanded->Release();
        }
        IUIAutomationTextRange* first = nullptr; if (range) range->Clone(&first);
        HRESULT expand = first ? first->ExpandToEnclosingUnit(TextUnit_Character) : E_FAIL;
        SAFEARRAY* rects = nullptr; HRESULT rectHr = first ? first->GetBoundingRectangles(&rects) : E_FAIL;
        LONG lo=0, hi=-1; if (rects) { SafeArrayGetLBound(rects,1,&lo); SafeArrayGetUBound(rects,1,&hi); }
        double rect[4]{}; if (hi-lo+1==4) for (LONG i=0;i<4;++i) SafeArrayGetElement(rects,&i,&rect[i]);
        IUIAutomationTextRange* hit = nullptr; POINT point{static_cast<LONG>(rect[0]+rect[2]/2), static_cast<LONG>(rect[1]+rect[3]/2)};
        HRESULT hitHr = pattern ? pattern->RangeFromPoint(point,&hit) : E_FAIL;
        int cmp=99; if (hit && first) hit->CompareEndpoints(TextPatternRangeEndpoint_Start,first,TextPatternRangeEndpoint_Start,&cmp);
        auto pos0=SendMessageW(search.window, WM_APP + 80, 19, 0), pos1=SendMessageW(search.window, WM_APP + 80, 19, 1);
        bool matchesPaint = std::abs(rect[0]-static_cast<short>(LOWORD(pos0))) <= 1 && std::abs(rect[2]-(static_cast<short>(LOWORD(pos1))-static_cast<short>(LOWORD(pos0)))) <= 1 && std::abs(rect[1]-static_cast<short>(HIWORD(pos0))) <= 1;
        std::cout << "rect=" << rect[0] << "," << rect[1] << "," << rect[2] << "," << rect[3] << " paint=" << static_cast<short>(LOWORD(pos0)) << "," << static_cast<short>(HIWORD(pos0)) << "," << static_cast<short>(LOWORD(pos1)) << " cmp=" << cmp << "\n";
        RECT fieldBounds{}; element->get_CurrentBoundingRectangle(&fieldBounds);
        bool inside = rect[0] >= fieldBounds.left && rect[1] >= fieldBounds.top && rect[0]+rect[2] <= fieldBounds.right && rect[1]+rect[3] <= fieldBounds.bottom;
        auto inkCell = SendMessageW(search.window, WM_APP + 80, 36, 0);
        bool matchesPixels = inkCell != -1 && std::abs(rect[0] - static_cast<short>(LOWORD(inkCell))) <= 1 &&
            std::abs(rect[0] + rect[2] - static_cast<short>(HIWORD(inkCell))) <= 1;
        std::cout << "GPU ink-derived advance cell=" << static_cast<short>(LOWORD(inkCell)) << "," << static_cast<short>(HIWORD(inkCell)) << " matches=" << matchesPixels << "\n";
        bool geometry = inside && matchesPaint && matchesPixels && SUCCEEDED(expand) && SUCCEEDED(rectHr) && hi-lo+1==4 && rect[2]>0 && rect[3]>0 && SUCCEEDED(hitHr) && cmp==0;
        if (rects) SafeArrayDestroy(rects); rects=nullptr;
        auto layoutEvents = new TextEvents(); layoutEvents->expected = UIA_LayoutInvalidatedEventId;
        HRESULT layoutSubscribed = automation->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, TreeScope_Element, nullptr, layoutEvents);
        SendMessageW(search.window, WM_APP + 80, 17, 28);
        HRESULT resizedHr = first ? first->GetBoundingRectangles(&rects) : E_FAIL;
        double resized[4]{}; hi=-1; if (rects) { SafeArrayGetUBound(rects,1,&hi); if (hi==3) for (LONG i=0;i<4;++i) SafeArrayGetElement(rects,&i,&resized[i]); }
        auto new0=SendMessageW(search.window, WM_APP + 80, 19, 0), new1=SendMessageW(search.window, WM_APP + 80, 19, 1);
        RECT resizedField{}; HRESULT fieldHr = element->get_CurrentBoundingRectangle(&resizedField);
        bool resizedOk = SUCCEEDED(fieldHr) && resizedField.bottom - resizedField.top > fieldBounds.bottom - fieldBounds.top && SUCCEEDED(resizedHr) && hi==3 && resized[3] > rect[3] && std::abs(resized[0]-static_cast<short>(LOWORD(new0))) <= 1 && std::abs(resized[2]-(static_cast<short>(LOWORD(new1))-static_cast<short>(LOWORD(new0)))) <= 1;
        auto layoutWait = WaitForSingleObject(layoutEvents->received, 2000);
        std::cout << "resized=" << resized[0] << "," << resized[1] << "," << resized[2] << "," << resized[3] << " paint=" << static_cast<short>(LOWORD(new0)) << "," << static_cast<short>(LOWORD(new1)) << " layout=" << layoutWait << "\n";
        resizedOk = resizedOk && SUCCEEDED(layoutSubscribed) && layoutWait == WAIT_OBJECT_0;
        if (SUCCEEDED(layoutSubscribed)) automation->RemoveAutomationEventHandler(UIA_LayoutInvalidatedEventId, root, layoutEvents);
        layoutEvents->Release();
        SendMessageW(search.window, WM_APP + 80, 17, 16);
        IUIAutomationValuePattern* change = nullptr;
        if (element) element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&change));
        BSTR replacement = SysAllocString(L"A\U0001f680B");
        HRESULT changeHr = change ? change->SetValue(replacement) : E_FAIL;
        BSTR stale = nullptr; HRESULT staleHr = range ? range->GetText(-1, &stale) : E_FAIL;
        IUIAutomationTextRange* unicodeRange = nullptr; if (pattern) pattern->get_DocumentRange(&unicodeRange);
        int unicodeMoved = 0; HRESULT unicodeHr = unicodeRange ? unicodeRange->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -2, &unicodeMoved) : E_FAIL;
        BSTR unicode = nullptr; if (unicodeRange) unicodeRange->GetText(-1, &unicode);
        bool revisionOk = SUCCEEDED(changeHr) && staleHr == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && SUCCEEDED(unicodeHr) && unicodeMoved == -2 && unicode && std::wstring(unicode, SysStringLen(unicode)) == L"A";
        BSTR original = SysAllocString(L"\u5de5\u4f5c\u7ad9 A");
        if (change) change->SetValue(original);
        BSTR staleAgain = nullptr; auto staleAgainHr = range ? range->GetText(-1, &staleAgain) : E_FAIL;
        revisionOk = revisionOk && staleAgainHr == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
        auto imeSetup = SendMessageW(search.window, WM_APP + 80, 11, 0);
        IUIAutomationTextRange* composingRange = nullptr; if (pattern) pattern->get_DocumentRange(&composingRange);
        BSTR committed = nullptr; HRESULT committedHr = composingRange ? composingRange->GetText(-1, &committed) : E_FAIL;
        HRESULT busyHr = change ? change->SetValue(original) : E_FAIL;
        bool compositionOk = imeSetup == 0 && SUCCEEDED(committedHr) && committed && std::wstring(committed, SysStringLen(committed)) == L"\u5de5\u4f5c\u7ad9 A" && busyHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        SendMessageW(search.window, WM_APP + 80, 12, 0); SysFreeString(committed); if (composingRange) composingRange->Release();
        SysFreeString(staleAgain); SysFreeString(original);
        SysFreeString(replacement); SysFreeString(stale); SysFreeString(unicode); if (unicodeRange) unicodeRange->Release(); if (change) change->Release();
        textOk = textOk && selected && geometry && resizedOk && revisionOk && wholeUnits && compositionOk;
        std::cout << "UIA08 selection=" << selected << " geometry=" << geometry << " UIA17 size=" << resizedOk << '\n';
        if (SUCCEEDED(subscribed)) automation->RemoveAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, element, events);
        events->Release(); if (rects) SafeArrayDestroy(rects); if (first) first->Release(); if (hit) hit->Release();
        std::cout << "UIA08 DocumentRange: " << (textOk ? "PASS" : "FAIL") << '\n';
        pass &= textOk; SysFreeString(content); if (range) range->Release(); if (pattern) pattern->Release();
    }
    std::cout << "UIA01 tree and identity: " << (pass ? "PASS" : "FAIL") << " hr=0x" << std::hex << hr << '\n';
    if (pass) {
        auto apply = findId(automation, root, L"form.apply");
        bool focus = apply && SUCCEEDED(focusSubscribed) && SendMessageW(search.window, WM_APP + 80, 20, 0) == 0 && WaitForSingleObject(focusEvents->applyReceived, 2000) == WAIT_OBJECT_0;
        std::cout << "UIA14 focus event: " << (focus ? "PASS" : "FAIL") << '\n'; pass &= focus;
        auto count = findId(automation, root, L"result.count");
        auto nameEvents = new PropertyEvents(); nameEvents->expected = UIA_NamePropertyId; nameEvents->stringValue = true; nameEvents->expectedString = L"\u5957\u7528\u6b21\u6578\uff1a1";
        PROPERTYID nameProperty = UIA_NamePropertyId;
        auto nameSubscribed = count ? automation->AddPropertyChangedEventHandlerNativeArray(count, TreeScope_Element, nullptr, nameEvents, &nameProperty, 1) : E_FAIL;
        IUIAutomationInvokePattern* invoke = nullptr; BSTR before = nullptr; BSTR after = nullptr;
        HRESULT getName = count ? count->get_CurrentName(&before) : E_FAIL;
        HRESULT getInvoke = apply ? apply->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)) : E_FAIL;
        HRESULT invokeHr = invoke ? invoke->Invoke() : E_FAIL;
        HRESULT getAfter = count ? count->get_CurrentName(&after) : E_FAIL;
        bool invoked = apply && count && SUCCEEDED(getName) && SUCCEEDED(getInvoke) && invoke &&
            SUCCEEDED(invokeHr) && SUCCEEDED(getAfter) && before && after &&
            std::wstring(before, SysStringLen(before)) == L"套用次數：0" &&
            std::wstring(after, SysStringLen(after)) == L"套用次數：1";
        bool nameEvent = SUCCEEDED(nameSubscribed) && WaitForSingleObject(nameEvents->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(nameSubscribed)) automation->RemovePropertyChangedEventHandler(count, nameEvents); nameEvents->Release();
        std::cout << "Name event: " << (nameEvent ? "PASS" : "FAIL") << '\n'; pass &= nameEvent;
        std::cout << "UIA02 Invoke: " << (invoked ? "PASS" : "FAIL") << '\n'; pass &= invoked;
        if (!invoked) std::cout << "  hr " << getName << ' ' << getInvoke << ' ' << invokeHr << ' ' << getAfter << '\n';
        SysFreeString(before); SysFreeString(after);
        if (invoke) invoke->Release(); if (apply) apply->Release(); if (count) count->Release();
        auto notices = findId(automation, root, L"form.notices"); IUIAutomationTogglePattern* toggle = nullptr;
        auto events = new PropertyEvents(); PROPERTYID property = UIA_ToggleToggleStatePropertyId;
        HRESULT subscribed = notices ? automation->AddPropertyChangedEventHandlerNativeArray(notices, TreeScope_Element, nullptr, events, &property, 1) : E_FAIL;
        ToggleState oldState{}, newState{};
        bool toggled = notices && SUCCEEDED(notices->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle &&
            SUCCEEDED(toggle->get_CurrentToggleState(&oldState)) && oldState == ToggleState_On && SUCCEEDED(toggle->Toggle()) &&
            SUCCEEDED(toggle->get_CurrentToggleState(&newState)) && newState == ToggleState_Off && SUCCEEDED(subscribed) &&
            WaitForSingleObject(events->received, 2000) == WAIT_OBJECT_0;
        std::cout << "UIA03 Toggle: " << (toggled ? "PASS" : "FAIL") << '\n'; pass &= toggled;
        if (SUCCEEDED(subscribed)) automation->RemovePropertyChangedEventHandler(notices, events);
        events->Release(); if (toggle) toggle->Release(); if (notices) notices->Release();
        pass &= patterns(automation, root);
        auto mode = findId(automation, root, L"form.mode");
        IUIAutomationSelectionPattern* selection = nullptr;
        IUIAutomationElementArray* selected = nullptr;
        BOOL multiple = TRUE;
        bool radio = mode && SUCCEEDED(mode->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection))) && selection &&
            SUCCEEDED(selection->get_CurrentCanSelectMultiple(&multiple)) && !multiple &&
            SUCCEEDED(selection->GetCurrentSelection(&selected)) && selected;
        int selectedCount = 0; IUIAutomationElement* initialRadio = nullptr; BSTR initialRadioId = nullptr;
        radio = radio && SUCCEEDED(selected->get_Length(&selectedCount)) && selectedCount == 1 && SUCCEEDED(selected->GetElement(0, &initialRadio)) && initialRadio && SUCCEEDED(initialRadio->get_CurrentAutomationId(&initialRadioId)) && initialRadioId && std::wstring(initialRadioId) == L"normal";
        SysFreeString(initialRadioId); if (initialRadio) initialRadio->Release();
        auto focusOption = findId(automation, mode, L"focus");
        IUIAutomationSelectionItemPattern* selectItem = nullptr;
        auto selectionEvents = new SelectionEvents();
        HRESULT selectionSubscribed = focusOption ? automation->AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, focusOption, TreeScope_Element, nullptr, selectionEvents) : E_FAIL;
        radio = radio && focusOption && SUCCEEDED(focusOption->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectItem))) &&
            selectItem && SUCCEEDED(selectItem->Select());
        BOOL isSelected = FALSE;
        radio = radio && SUCCEEDED(selectItem->get_CurrentIsSelected(&isSelected)) && isSelected;
        radio = radio && SUCCEEDED(selectionSubscribed) && WaitForSingleObject(selectionEvents->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(selectionSubscribed)) automation->RemoveAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, focusOption, selectionEvents);
        selectionEvents->Release();
        radio = radio && SUCCEEDED(invokeId(automation, root, L"fixture.offline_disabled")) && selectId(automation, mode, L"offline") == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        // #101: semantic enabled is the item's own setting; UIA combines it with its parent,
        // and parent transitions still raise availability events on enabled items.
        auto enabledEvents = new PropertyEvents();
        enabledEvents->expected = UIA_IsEnabledPropertyId; enabledEvents->boolValue = true;
        PROPERTYID enabledProperty = UIA_IsEnabledPropertyId;
        const auto enabledSubscribed = focusOption ? automation->AddPropertyChangedEventHandlerNativeArray(focusOption, TreeScope_Element, nullptr, enabledEvents, &enabledProperty, 1) : E_FAIL;
        BOOL itemEnabled = TRUE;
        radio = radio && SUCCEEDED(enabledSubscribed) && SUCCEEDED(invokeId(automation, root, L"fixture.mode_disabled")) &&
            SUCCEEDED(focusOption->get_CurrentIsEnabled(&itemEnabled)) && !itemEnabled &&
            WaitForSingleObject(enabledEvents->received, 2000) == WAIT_OBJECT_0;
        ResetEvent(enabledEvents->received); enabledEvents->expectedBool = true;
        radio = radio && SUCCEEDED(invokeId(automation, root, L"fixture.mode_enabled")) &&
            SUCCEEDED(focusOption->get_CurrentIsEnabled(&itemEnabled)) && itemEnabled &&
            WaitForSingleObject(enabledEvents->received, 2000) == WAIT_OBJECT_0;
        auto offline = findId(automation, mode, L"offline");
        radio = radio && offline && SUCCEEDED(offline->get_CurrentIsEnabled(&itemEnabled)) && !itemEnabled;
        if (offline) offline->Release();
        if (SUCCEEDED(enabledSubscribed)) automation->RemovePropertyChangedEventHandler(focusOption, enabledEvents);
        enabledEvents->Release();
        std::cout << "UIA04 radio selection: " << (radio ? "PASS" : "FAIL") << '\n'; pass &= radio;
        if (selectItem) selectItem->Release(); if (focusOption) focusOption->Release();
        if (selected) selected->Release(); if (selection) selection->Release(); if (mode) mode->Release();
        auto region = findId(automation, root, L"form.region");
        IUIAutomationExpandCollapsePattern* expand = nullptr;
        ExpandCollapseState expanded{};
        auto expandEvents = new PropertyEvents(); expandEvents->expected = UIA_ExpandCollapseExpandCollapseStatePropertyId; expandEvents->expectedValue = ExpandCollapseState_Expanded;
        auto collapseEvents = new PropertyEvents(); collapseEvents->expected = UIA_ExpandCollapseExpandCollapseStatePropertyId; collapseEvents->expectedValue = ExpandCollapseState_Collapsed;
        PROPERTYID expandProperty = UIA_ExpandCollapseExpandCollapseStatePropertyId;
        auto expandSubscribed = region ? automation->AddPropertyChangedEventHandlerNativeArray(region, TreeScope_Element, nullptr, expandEvents, &expandProperty, 1) : E_FAIL;
        auto collapseSubscribed = region ? automation->AddPropertyChangedEventHandlerNativeArray(region, TreeScope_Element, nullptr, collapseEvents, &expandProperty, 1) : E_FAIL;
        bool dropdown = region && selectedIdIs(region, L"north") && SUCCEEDED(region->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand &&
            SUCCEEDED(expand->Expand()) && SUCCEEDED(expandSubscribed) && WaitForSingleObject(expandEvents->received, 2000) == WAIT_OBJECT_0 && SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Expanded &&
            SUCCEEDED(expand->Collapse()) && SUCCEEDED(collapseSubscribed) && WaitForSingleObject(collapseEvents->received, 5000) == WAIT_OBJECT_0 && SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Collapsed && selectedIdIs(region, L"north");
        dropdown = dropdown && SUCCEEDED(expand->Expand()) && SUCCEEDED(selectId(automation, region, L"south"));
        auto south = findId(automation, region, L"south"); IUIAutomationSelectionItemPattern* southPattern = nullptr; BOOL southSelected = FALSE;
        dropdown = dropdown && south && SUCCEEDED(south->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&southPattern))) && southPattern && SUCCEEDED(southPattern->get_CurrentIsSelected(&southSelected)) && southSelected;
        if (southPattern) southPattern->Release(); if (south) south->Release();
        std::cout << "UIA05 dropdown: " << (dropdown ? "PASS" : "FAIL") << '\n'; pass &= dropdown;
        if (SUCCEEDED(collapseSubscribed)) automation->RemovePropertyChangedEventHandler(region, collapseEvents); collapseEvents->Release();
        if (SUCCEEDED(expandSubscribed)) automation->RemovePropertyChangedEventHandler(region, expandEvents); expandEvents->Release();
        if (expand) expand->Release(); if (region) region->Release();
        auto volume = findId(automation, root, L"form.volume"); IUIAutomationRangeValuePattern* range = nullptr;
        double min = 0, max = 0, step = 0, largeStep = 0, valueNow = 0;
        auto rangeEvents = new PropertyEvents(); rangeEvents->expected = UIA_RangeValueValuePropertyId; rangeEvents->doubleValue = true; rangeEvents->expectedDouble = 65;
        PROPERTYID rangeProperty = UIA_RangeValueValuePropertyId;
        auto rangeSubscribed = volume ? automation->AddPropertyChangedEventHandlerNativeArray(volume, TreeScope_Element, nullptr, rangeEvents, &rangeProperty, 1) : E_FAIL;
        bool slider = volume && SUCCEEDED(volume->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range &&
            SUCCEEDED(range->get_CurrentMinimum(&min)) && min == 0 && SUCCEEDED(range->get_CurrentMaximum(&max)) && max == 100 &&
            SUCCEEDED(range->get_CurrentSmallChange(&step)) && step == 5 && SUCCEEDED(range->get_CurrentLargeChange(&largeStep)) && largeStep == 5 && SUCCEEDED(range->get_CurrentValue(&valueNow)) && valueNow == 40 && SUCCEEDED(range->SetValue(65)) && SUCCEEDED(rangeSubscribed) && WaitForSingleObject(rangeEvents->received, 2000) == WAIT_OBJECT_0 &&
            range->SetValue(101) == E_INVALIDARG && range->SetValue(62.5) == E_INVALIDARG && SUCCEEDED(range->get_CurrentValue(&valueNow)) && valueNow == 65;
        std::cout << "UIA06 slider: " << (slider ? "PASS" : "FAIL") << '\n'; pass &= slider;
        if (SUCCEEDED(rangeSubscribed)) automation->RemovePropertyChangedEventHandler(volume, rangeEvents); rangeEvents->Release();
        if (range) range->Release(); if (volume) volume->Release();
        const HRESULT expected[] = {S_OK, static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED),
            static_cast<HRESULT>(UIA_E_INVALIDOPERATION), static_cast<HRESULT>(UIA_E_INVALIDOPERATION), static_cast<HRESULT>(UIA_E_INVALIDOPERATION), E_INVALIDARG,
            E_FAIL, E_FAIL, static_cast<HRESULT>(UIA_E_NOTSUPPORTED), static_cast<HRESULT>(UIA_E_INVALIDOPERATION),
            static_cast<HRESULT>(UIA_E_INVALIDOPERATION), static_cast<HRESULT>(UIA_E_INVALIDOPERATION)};
        bool mapping = true;
        for (unsigned i = 0; i < sizeof(expected) / sizeof(*expected); ++i) {
            auto id = L"fixture.status." + std::to_wstring(i); auto target = findId(automation, root, id.c_str()); IUIAutomationInvokePattern* action = nullptr;
            HRESULT actual = target ? target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&action)) : E_FAIL;
            if (SUCCEEDED(actual) && action) actual = action->Invoke();
            if (actual != expected[i]) { std::cout << "UIA15 status=" << i << " expected=0x" << std::hex << expected[i] << " actual=0x" << actual << '\n'; mapping = false; }
            if (action) action->Release(); if (target) target->Release();
        }
        auto disabled = findId(automation, root, L"fixture.real_disabled"); IUIAutomationInvokePattern* disabledAction = nullptr;
        HRESULT disabledHr = disabled ? disabled->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&disabledAction)) : E_FAIL;
        if (SUCCEEDED(disabledHr) && disabledAction) disabledHr = disabledAction->Invoke();
        mapping = mapping && disabledHr == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        if (disabledAction) disabledAction->Release(); if (disabled) disabled->Release();
        apply = findId(automation, root, L"form.apply"); invoke = nullptr;
        if (apply) apply->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
        SendMessageW(search.window, WM_APP + 80, 2, 0);
        HRESULT hiddenHr = invoke ? invoke->Invoke() : E_FAIL;
        mapping = mapping && hiddenHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        if (invoke) invoke->Release(); if (apply) apply->Release();
        pass &= patterns(automation, root);
        auto progress = findId(automation, root, L"form.title.pages.v03.progress"); IUIAutomationRangeValuePattern* progressPattern = nullptr; BOOL readonly = FALSE; double progressValue = 0;
        auto progressEvents = new PropertyEvents(); progressEvents->expected = UIA_RangeValueValuePropertyId; progressEvents->doubleValue = true; progressEvents->expectedDouble = 100;
        PROPERTYID progressProperty = UIA_RangeValueValuePropertyId;
        auto progressSubscribed = progress ? automation->AddPropertyChangedEventHandlerNativeArray(progress, TreeScope_Element, nullptr, progressEvents, &progressProperty, 1) : E_FAIL;
        bool progressCase = progress && SUCCEEDED(progress->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&progressPattern))) && progressPattern && SUCCEEDED(progressPattern->get_CurrentIsReadOnly(&readonly)) && readonly && progressPattern->SetValue(25) == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && SUCCEEDED(progressPattern->get_CurrentValue(&progressValue)) && progressValue == 50 && SUCCEEDED(invokeId(automation, root, L"form.title.pages.v03.above")) && SUCCEEDED(progressSubscribed) && WaitForSingleObject(progressEvents->received, 2000) == WAIT_OBJECT_0 && SUCCEEDED(progressPattern->get_CurrentValue(&progressValue)) && progressValue == 100;
        if (!progressCase) std::cout << "progress details element=" << bool(progress) << " pattern=" << bool(progressPattern) << " readonly=" << readonly << " value=" << progressValue << " subscribe=0x" << std::hex << progressSubscribed << std::dec << '\n';
        std::cout << "UIA11 progress: " << (progressCase ? "PASS" : "FAIL") << '\n'; pass &= progressCase;
        if (SUCCEEDED(progressSubscribed)) automation->RemovePropertyChangedEventHandler(progress, progressEvents); progressEvents->Release(); if (progressPattern) progressPattern->Release(); if (progress) progress->Release();
        invokeId(automation, root, L"form.title.pages.v03.half");
        auto modalTarget = findId(automation, root, L"form.title.pages.v03.temporary"); IUIAutomationInvokePattern* modalAction = nullptr;
        if (modalTarget) modalTarget->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&modalAction));
        auto rapidOpen = new SelectionEvents(); rapidOpen->expected = UIA_Window_WindowOpenedEventId;
        auto rapidClose = new SelectionEvents(); rapidClose->expected = UIA_Window_WindowClosedEventId;
        auto rapidOpenSub = automation->AddAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, TreeScope_Subtree, nullptr, rapidOpen);
        auto rapidCloseSub = automation->AddAutomationEventHandler(UIA_Window_WindowClosedEventId, root, TreeScope_Subtree, nullptr, rapidClose);
        bool rapidEvents = SUCCEEDED(rapidOpenSub) && SUCCEEDED(rapidCloseSub) && SendMessageW(search.window, WM_APP + 80, 25, 0) == 0 && WaitForSingleObject(rapidOpen->received, 5000) == WAIT_OBJECT_0 && WaitForSingleObject(rapidClose->received, 5000) == WAIT_OBJECT_0;
        if (SUCCEEDED(rapidOpenSub)) automation->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, rapidOpen);
        if (SUCCEEDED(rapidCloseSub)) automation->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, root, rapidClose);
        rapidOpen->Release(); rapidClose->Release();
        std::cout << "UIA12 rapid window transitions: " << (rapidEvents ? "PASS" : "FAIL") << '\n'; pass &= rapidEvents;
        auto closedEvents = new SelectionEvents(); closedEvents->expected = UIA_Window_WindowClosedEventId;
        auto closedSubscribed = automation->AddAutomationEventHandler(UIA_Window_WindowClosedEventId, root, TreeScope_Subtree, nullptr, closedEvents);
        auto windowEvents = new SelectionEvents(); windowEvents->expected = UIA_Window_WindowOpenedEventId;
        auto windowSubscribed = automation->AddAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, TreeScope_Subtree, nullptr, windowEvents);
        SendMessageW(search.window, WM_APP + 80, 7, 0);
        bool openedEvent = SUCCEEDED(windowSubscribed) && WaitForSingleObject(windowEvents->received, 2000) == WAIT_OBJECT_0;
        if (SUCCEEDED(windowSubscribed)) automation->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, windowEvents); windowEvents->Release();
        std::cout << "UIA12 window opened event: " << (openedEvent ? "PASS" : "FAIL") << '\n'; pass &= openedEvent;
        pass &= patterns(automation, root);
        auto dialog = findId(automation, root, L"form.title.pages.dialog"); IUIAutomationWindowPattern* windowPattern = nullptr; BOOL isModal = FALSE;
        bool window = dialog && SUCCEEDED(dialog->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&windowPattern))) && windowPattern &&
            SUCCEEDED(windowPattern->get_CurrentIsModal(&isModal)) && isModal && SUCCEEDED(windowPattern->Close());
        bool openerFocus = SUCCEEDED(focusSubscribed) && WaitForSingleObject(focusEvents->openerReceived, 5000) == WAIT_OBJECT_0;
        pass &= openerFocus;
        bool closedEvent = SUCCEEDED(closedSubscribed) && WaitForSingleObject(closedEvents->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(closedSubscribed)) automation->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, root, closedEvents); closedEvents->Release();
        std::cout << "UIA12 window closed event: " << (closedEvent ? "PASS" : "FAIL") << '\n'; pass &= closedEvent;
        std::cout << "UIA12 modal window: " << (window ? "PASS" : "FAIL") << '\n'; pass &= window;
        if (windowPattern) windowPattern->Release(); if (dialog) dialog->Release();
        if (window) SendMessageW(search.window, WM_APP + 80, 7, 0);
        auto half = findId(automation, root, L"form.title.pages.v03.half"); BOOL halfEnabled = FALSE;
        auto modalProgress = findId(automation, root, L"form.title.pages.v03.progress"); IUIAutomationRangeValuePattern* modalRange = nullptr; double modalValue = 0;
        bool modalRead = modalProgress && SUCCEEDED(modalProgress->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&modalRange))) && modalRange && SUCCEEDED(modalRange->get_CurrentValue(&modalValue)) && modalValue == 50;
        if (modalRange) modalRange->Release(); if (modalProgress) modalProgress->Release();
        auto tabs = findId(automation, root, L"form.title.pages.tabs");
        bool modalBackground = modalRead && half && SUCCEEDED(half->get_CurrentIsEnabled(&halfEnabled)) && halfEnabled && tabs && selectId(automation, tabs, L"v01") == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        std::cout << "UIA12 background: " << (modalBackground ? "PASS" : "FAIL") << '\n'; pass &= modalBackground;
        if (half) half->Release(); if (tabs) tabs->Release();
        HRESULT modalHr = modalAction ? modalAction->Invoke() : E_FAIL;
        mapping = mapping && modalHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        SendMessageW(search.window, WM_APP + 80, 8, 0);
        auto tipClosedEvents = new SelectionEvents(); tipClosedEvents->expected = UIA_ToolTipClosedEventId;
        auto tipClosedSubscribed = automation->AddAutomationEventHandler(UIA_ToolTipClosedEventId, root, TreeScope_Subtree, nullptr, tipClosedEvents);
        auto tipEvents = new SelectionEvents(); tipEvents->expected = UIA_ToolTipOpenedEventId;
        auto tipSubscribed = automation->AddAutomationEventHandler(UIA_ToolTipOpenedEventId, root, TreeScope_Subtree, nullptr, tipEvents);
        auto tipSetup = SendMessageW(search.window, WM_APP + 80, 21, 0);
        auto tipSource = findId(automation, root, L"form.title.pages.v03.half");
        auto focusTip = tipSource ? tipSource->SetFocus() : E_FAIL;
        bool tipEvent = tipSetup == 0 && SUCCEEDED(focusTip) && SUCCEEDED(tipSubscribed) && WaitForSingleObject(tipEvents->received, 3000) == WAIT_OBJECT_0;
        IUIAutomationElement* tip = nullptr;
        for (int attempt = 0; attempt < 30 && !tip; ++attempt) { tip = findId(automation, root, L"form.title.pages.v03.half.tip"); if (!tip) Sleep(100); }
        CONTROLTYPEID tipType = 0; BSTR tipName = nullptr;
        bool tooltip = tipEvent && tip && SUCCEEDED(tip->get_CurrentControlType(&tipType)) && tipType == UIA_ToolTipControlTypeId && SUCCEEDED(tip->get_CurrentName(&tipName)) && tipName && std::wstring(tipName) == L"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002";
        if (!tooltip) std::cout << "tip setup=" << tipSetup << " event=" << tipEvent << " element=" << bool(tip) << " type=" << tipType << " subscribed=" << std::hex << tipSubscribed << std::dec << '\n';
        tooltip = tooltip && SendMessageW(search.window, WM_APP + 80, 24, 0) == 1;
        SendMessageW(search.window, WM_APP + 80, 23, 0);
        bool tipClosed = SUCCEEDED(tipClosedSubscribed) && WaitForSingleObject(tipClosedEvents->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(tipClosedSubscribed)) automation->RemoveAutomationEventHandler(UIA_ToolTipClosedEventId, root, tipClosedEvents); tipClosedEvents->Release(); if (tipSource) tipSource->Release();
        std::cout << "UIA13 tooltip closed event: " << (tipClosed ? "PASS" : "FAIL") << '\n'; pass &= tipClosed;
        std::cout << "UIA13 tooltip: " << (tooltip ? "PASS" : "FAIL") << '\n'; pass &= tooltip;
        SysFreeString(tipName); if (tip) tip->Release(); if (SUCCEEDED(tipSubscribed)) automation->RemoveAutomationEventHandler(UIA_ToolTipOpenedEventId, root, tipEvents); tipEvents->Release();
        if (modalAction) modalAction->Release(); if (modalTarget) modalTarget->Release();
        SendMessageW(search.window, WM_APP + 80, 5, 0);
        auto invokeId = [&](const wchar_t* id) {
            auto target = findId(automation, root, id); IUIAutomationInvokePattern* action = nullptr;
            HRESULT result = target ? target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&action)) : E_FAIL;
            if (SUCCEEDED(result) && action) result = action->Invoke();
            if (action) action->Release(); if (target) target->Release(); return result;
        };
        HRESULT invalidHr = invokeId(L"fixture.name_invalid");
        auto readOnlySetup = SendMessageW(search.window, WM_APP + 80, 9, 0);
        HRESULT readonlyHr = invokeId(L"fixture.name_write");
        SendMessageW(search.window, WM_APP + 80, 10, 0);
        auto composingSetup = SendMessageW(search.window, WM_APP + 80, 11, 0);
        HRESULT busyHr = invokeId(L"form.apply");
        SendMessageW(search.window, WM_APP + 80, 12, 0);
        SendMessageW(search.window, WM_APP + 80, 13, 0);
        HRESULT rendererHr = invokeId(L"form.apply");
        SendMessageW(search.window, WM_APP + 80, 14, 0);
        mapping = mapping && invalidHr == E_INVALIDARG && readOnlySetup == 0 && readonlyHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) &&
            composingSetup == 0 && busyHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && rendererHr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        if (!mapping) std::cout << "UIA15 actual disabled/hidden/modal/invalid/readonly/busy/renderer: 0x" << std::hex << disabledHr << ' ' << hiddenHr << ' ' << modalHr << ' ' << invalidHr << ' ' << readonlyHr << ' ' << busyHr << ' ' << rendererHr << '\n';
        std::cout << "UIA15 status mapping: " << (mapping ? "PASS" : "FAIL") << '\n'; pass &= mapping;
        SendMessageW(search.window, WM_APP + 80, 2, 0);
        auto temporary = findId(automation, root, L"form.title.pages.v03.temporary"); SAFEARRAY* oldId = nullptr; SAFEARRAY* newId = nullptr;
        if (temporary) temporary->GetRuntimeId(&oldId);
        SendMessageW(search.window, WM_APP + 80, 3, 0);
        VARIANT removed; VariantInit(&removed);
        HRESULT removedHr = temporary ? temporary->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &removed) : E_FAIL; VariantClear(&removed);
        SendMessageW(search.window, WM_APP + 80, 4, 0);
        auto replacement = findId(automation, root, L"form.title.pages.v03.temporary");
        if (replacement) replacement->GetRuntimeId(&newId);
        BOOL equal = TRUE; if (oldId && newId) automation->CompareRuntimeIds(oldId, newId, &equal);
        HRESULT stillRemoved = temporary ? temporary->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &removed) : E_FAIL; VariantClear(&removed);
        bool lifecycle = removedHr == UIA_E_ELEMENTNOTAVAILABLE && stillRemoved == UIA_E_ELEMENTNOTAVAILABLE && replacement && oldId && newId && !equal;
        if (replacement) { retained = replacement; retained->AddRef(); }
        std::cout << "UIA19 removal: " << (lifecycle ? "PASS" : "FAIL") << " hr=0x" << std::hex << removedHr << ' ' << stillRemoved << '\n'; pass &= lifecycle;
        if (oldId) SafeArrayDestroy(oldId); if (newId) SafeArrayDestroy(newId); if (temporary) temporary->Release(); if (replacement) replacement->Release();
    }
    if (SUCCEEDED(focusSubscribed)) automation->RemoveFocusChangedEventHandler(focusEvents); focusEvents->Release();
    if (element) element->Release(); if (condition) condition->Release(); VariantClear(&value);
    if (root) root->Release(); if (automation) automation->Release();
    if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); pass = false; }
    VARIANT closed; VariantInit(&closed);
    HRESULT closedHr = retained ? retained->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &closed) : E_FAIL;
    bool closedPass = closedHr == UIA_E_ELEMENTNOTAVAILABLE;
    std::cout << "UIA19 close: " << (closedPass ? "PASS" : "FAIL") << " hr=0x" << std::hex << closedHr << '\n';
    VariantClear(&closed); if (retained) retained->Release(); pass &= closedPass;
    CloseHandle(process.hProcess); pass = rendererFailureCases(argv[1], argv[2]) && pass; CoUninitialize(); return pass ? 0 : 1;
}
