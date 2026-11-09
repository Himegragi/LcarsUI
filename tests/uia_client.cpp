#include "uia_client_helpers.hpp"
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
    LONG expected_value = ToggleState_Off;
    bool double_value = false; double expected_double = 0;
    bool string_value = false; std::wstring expected_string;
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id, VARIANT value) override {
        if (id == expected && ((string_value && value.vt == VT_BSTR && value.bstrVal && expected_string == value.bstrVal) || (!string_value && !double_value && value.vt == VT_I4 && value.lVal == expected_value) || (double_value && value.vt == VT_R8 && value.dblVal == expected_double))) SetEvent(received);
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
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType type, SAFEARRAY*) override {
        if (type == StructureChangeType_ChildrenInvalidated) SetEvent(received); return S_OK;
    }
};

class FocusEvents final : public UiaEventHandler<FocusEvents, IUIAutomationFocusChangedEventHandler> {
public:
    std::wstring expected = L"T004";
    std::mutex mutex; std::vector<std::wstring> observed;
    HANDLE apply_received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE name_received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE opener_received = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~FocusEvents() { CloseHandle(apply_received); CloseHandle(name_received); CloseHandle(opener_received); }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        BSTR id = nullptr; if (sender && SUCCEEDED(sender->get_CurrentAutomationId(&id)) && id) {
            std::wstring value(id); if (value == L"T003" || value == L"T004") { std::lock_guard lock(mutex); observed.push_back(value); }
            if (value == expected) SetEvent(received); if (value == L"form.apply") SetEvent(apply_received); if (value == L"form.name") SetEvent(name_received); if (value == L"form.title.pages.v03.open") SetEvent(opener_received);
        }
        SysFreeString(id); return S_OK;
    }
};

class TextEvents final : public UiaEventHandler<TextEvents, IUIAutomationEventHandler> {
public:
    EVENTID expected = UIA_Text_TextSelectionChangedEventId;
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) override { if (id==expected) SetEvent(received); return S_OK; }
};

bool selected_id_is(IUIAutomationElement* container, const wchar_t* expected) {
    IUIAutomationSelectionPattern* p = nullptr; IUIAutomationElementArray* selected = nullptr; IUIAutomationElement* item = nullptr; BSTR id = nullptr; int n = 0;
    bool ok = container && SUCCEEDED(container->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->GetCurrentSelection(&selected)) && selected && SUCCEEDED(selected->get_Length(&n)) && n == 1 && SUCCEEDED(selected->GetElement(0, &item)) && item && SUCCEEDED(item->get_CurrentAutomationId(&id)) && id && std::wstring(id) == expected;
    SysFreeString(id); if (item) item->Release(); if (selected) selected->Release(); if (p) p->Release(); return ok;
}
bool ime_probe(HWND window) {
    IUIAutomation* automation = nullptr; IUIAutomationElement* root = nullptr; IUIAutomationElement* field = nullptr;
    IUIAutomationValuePattern* value = nullptr; BSTR before = nullptr; BSTR after = nullptr;
    bool ok = window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(window, &root)) && (field = find_id(automation, root, L"form.name")) &&
        SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value &&
        SUCCEEDED(value->get_CurrentValue(&before)) && before;
    BSTR attempt = SysAllocString(L"UIA replacement"); HRESULT change = value ? value->SetValue(attempt) : E_FAIL;
    HRESULT read = value ? value->get_CurrentValue(&after) : E_FAIL;
    ok = ok && before && std::wstring(before) == L"UIA baseline" && change == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && SUCCEEDED(read) && after && std::wstring(before) == after;
    std::cout << "UIA21 cross-process value: " << (ok ? "PASS" : "FAIL") << " set=0x" << std::hex << change << '\n';
    SysFreeString(attempt); SysFreeString(before); SysFreeString(after); if(value)value->Release(); if(field)field->Release(); if(root)root->Release(); if(automation)automation->Release(); return ok;
}
bool ime_simulation(const wchar_t* host, const wchar_t* appearance) {
    std::wstring command=L"\""+std::wstring(host)+L"\" "+appearance; STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) return false;
    CloseHandle(process.hThread); Search search{process.dwProcessId};
    for (int i=0;i<100 && !search.window;++i) { EnumWindows(find_window,reinterpret_cast<LPARAM>(&search)); Sleep(100); }
    bool ok=search.window && SendMessageW(search.window,WM_APP+80,31,0)==0 && SendMessageW(search.window,WM_APP+80,11,0)==0 &&
        SendMessageW(search.window,WM_APP+80,30,0)==1 && ime_probe(search.window) && SendMessageW(search.window,WM_APP+80,30,0)==1;
    if(search.window) { SendMessageW(search.window,WM_APP+80,12,0); PostMessageW(search.window,WM_CLOSE,0,0); }
    if(WaitForSingleObject(process.hProcess,5000)==WAIT_TIMEOUT) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); ok=false; }
    CloseHandle(process.hProcess); std::cout << "UIA21 simulated composition: " << (ok ? "PASS" : "FAIL") << '\n'; return ok;
}
BOOL CALLBACK collect_display(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    MONITORINFO info{sizeof(info)}; if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcWork); return TRUE;
}
struct DpiGeometry { RECT field{}; double glyph[4]{}; UINT dpi = 0; };
bool dpi_geometry(HWND window, IUIAutomationElement* field, IUIAutomationTextRange* first, DpiGeometry& result) {
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
bool dpi_case(const wchar_t* host, const wchar_t* appearance) {
    std::vector<RECT> displays; EnumDisplayMonitors(nullptr,nullptr,collect_display,reinterpret_cast<LPARAM>(&displays));
    if (displays.size()<2) { std::cout << "UIA17-DPI: BLOCKED two monitors required\n"; return false; }
    std::wstring command=L"\""+std::wstring(host)+L"\" "+appearance; STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) return false;
    CloseHandle(process.hThread); Search search{process.dwProcessId};
    for(int i=0;i<100 && !search.window;++i) { EnumWindows(find_window,reinterpret_cast<LPARAM>(&search)); Sleep(100); }
    IUIAutomation *automation=nullptr; IUIAutomationElement *root=nullptr,*field=nullptr; IUIAutomationTextPattern* pattern=nullptr; IUIAutomationTextRange* first=nullptr;
    bool ok=search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(search.window,&root)) && (field=find_id(automation,root,L"form.name")) &&
        SUCCEEDED(field->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern))) && pattern &&
        SUCCEEDED(pattern->get_DocumentRange(&first)) && first && SUCCEEDED(first->ExpandToEnclosingUnit(TextUnit_Character));
    auto events=new TextEvents(); events->expected=UIA_LayoutInvalidatedEventId;
    HRESULT subscribed=ok ? automation->AddAutomationEventHandler(UIA_LayoutInvalidatedEventId,root,TreeScope_Element,nullptr,events) : E_FAIL;
    int a=-1,b=-1; UINT dpi_a=0,dpi_b=0;
    for(size_t i=0;ok && i<displays.size();++i) {
        SetWindowPos(search.window,nullptr,displays[i].left+20,displays[i].top+20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); Sleep(700);
        UINT dpi=GetDpiForWindow(search.window);
        std::cout << "UIA17-DPI display " << i << " work=" << displays[i].left << ',' << displays[i].top << ',' << displays[i].right << ',' << displays[i].bottom << " dpi=" << dpi << '\n';
        if(a<0) { a=static_cast<int>(i); dpi_a=dpi; }
        else if(b<0 && dpi!=dpi_a) { b=static_cast<int>(i); dpi_b=dpi; }
    }
    if(ok && b<0) { std::cout << "UIA17-DPI: BLOCKED fewer than two distinct display DPIs\n"; ok=false; }
    auto move=[&](int index, DpiGeometry& geometry) {
        ResetEvent(events->received); auto r=displays[index]; SetWindowPos(search.window,nullptr,r.left+20,r.top+20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); Sleep(700);
        bool matched=dpi_geometry(search.window,field,first,geometry); DWORD received=WaitForSingleObject(events->received,3000);
        return matched && SUCCEEDED(subscribed) && received==WAIT_OBJECT_0;
    };
    DpiGeometry start{},next{},back{};
    if(ok) { auto r=displays[a]; SetWindowPos(search.window,nullptr,r.left+20,r.top+20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); Sleep(700); ok=dpi_geometry(search.window,field,first,start); }
    if(ok) ok=move(b,next) && move(a,back);
    if(ok) ok=start.dpi==dpi_a && next.dpi==dpi_b && back.dpi==dpi_a && std::abs(next.glyph[3]-start.glyph[3]*double(dpi_b)/dpi_a)<=2 && std::abs(back.glyph[3]-start.glyph[3])<=1 &&
        std::abs((next.field.bottom-next.field.top)-(start.field.bottom-start.field.top)*double(dpi_b)/dpi_a)<=2 &&
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
    for(int i=0;i<100 && !search.window;++i) { EnumWindows(find_window,reinterpret_cast<LPARAM>(&search)); if(WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0) break; Sleep(100); }
    IUIAutomation* automation=nullptr; IUIAutomationElement* root=nullptr; IUIAutomationElement* field=nullptr;
    bool ok=search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(search.window,&root)) && (field=find_id(automation,root,L"form.name"));
    std::cout << "Preflight: " << (ok ? "PASS" : "BLOCKED host OpenGL or UIA unavailable") << '\n';
    if(field)field->Release(); if(root)root->Release(); if(automation)automation->Release();
    if(search.window)PostMessageW(search.window,WM_CLOSE,0,0); if(WaitForSingleObject(process.hProcess,5000)==WAIT_TIMEOUT) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); ok=false; }
    CloseHandle(process.hProcess); return ok;
}
bool renderer_failure_case(const wchar_t* host, const wchar_t* appearance, bool unhandled) {
    std::wstring command = L"\"" + std::wstring(host) + L"\" " + appearance + (unhandled ? L" --unhandled" : L"");
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    Search search{process.dwProcessId};
    for (int i = 0; i < 100 && !search.window; ++i) { EnumWindows(find_window, reinterpret_cast<LPARAM>(&search)); Sleep(100); }
    IUIAutomation* automation = nullptr; IUIAutomationElement* root = nullptr; IUIAutomationElement* field = nullptr;
    IUIAutomationValuePattern* value = nullptr; IUIAutomationElement* apply = nullptr; IUIAutomationElement* count = nullptr;
    bool ok = search.window && SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(search.window, &root)) && (field = find_id(automation, root, L"form.name")) &&
        SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value;
    BSTR before = nullptr; BSTR count_before = nullptr;
    if (ok) { apply = find_id(automation, root, L"form.apply"); count = find_id(automation, root, L"result.count");
        ok = apply && count && SUCCEEDED(count->get_CurrentName(&count_before)) && count_before && SUCCEEDED(value->get_CurrentValue(&before)) && before; }
    if (ok) ok = SendMessageW(search.window, WM_APP + 80, 26, 0) == 0;
    if (unhandled) {
        if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) ok = false;
        DWORD code = 0; if (!GetExitCodeProcess(process.hProcess, &code) || code != 12) ok = false;
        VARIANT prop; VariantInit(&prop);
        HRESULT unavailable = field ? field->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &prop) : E_FAIL;
        VariantClear(&prop);
        HRESULT unavailable_apply = apply ? apply->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &prop) : E_FAIL;
        VariantClear(&prop); ok = ok && unavailable == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) && unavailable_apply == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
    } else {
        BSTR after = nullptr; HRESULT read = value ? value->get_CurrentValue(&after) : E_FAIL;
        HRESULT denied = invoke_id(automation, root, L"form.apply"); BSTR count_denied = nullptr;
        HRESULT count_hr = count ? count->get_CurrentName(&count_denied) : E_FAIL;
        ok = ok && SUCCEEDED(read) && after && before && std::wstring(before) == after && denied == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) &&
            SUCCEEDED(count_hr) && count_denied && std::wstring(count_before) == count_denied;
        SysFreeString(after); SysFreeString(count_denied);
        if (ok) ok = SendMessageW(search.window, WM_APP + 80, 27, 0) == 0 &&
            SendMessageW(search.window, WM_APP + 80, 28, 0) == 0 && SUCCEEDED(invoke_id(automation, root, L"form.apply"));
        BSTR count_after = nullptr; HRESULT after_hr = count ? count->get_CurrentName(&count_after) : E_FAIL;
        ok = ok && SUCCEEDED(after_hr) && count_after && std::wstring(count_before) != count_after; SysFreeString(count_after);
        if (search.window) PostMessageW(search.window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) ok = false;
    }
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
    SysFreeString(before); SysFreeString(count_before); if (apply) apply->Release(); if (count) count->Release(); if (value) value->Release(); if (field) field->Release(); if (root) root->Release(); if (automation) automation->Release();
    CloseHandle(process.hProcess); return ok;
}
bool renderer_failure_cases(const wchar_t* host, const wchar_t* appearance) {
    bool handled = renderer_failure_case(host, appearance, false);
    bool unhandled = renderer_failure_case(host, appearance, true);
    std::cout << "UIA18 renderer lifecycle: " << (handled && unhandled ? "PASS" : "FAIL") << " handled=" << handled << " unhandled=" << unhandled << '\n';
    return handled && unhandled;
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 3 && std::wstring(argv[1]) == L"--ime-probe") {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
        bool ok = ime_probe(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argv[2])))); CoUninitialize(); return ok ? 0 : 1;
    }
    if ((argc != 3 && argc != 4) || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (argc == 4) { const std::wstring mode(argv[3]); bool ok = mode == L"--uia18" ? renderer_failure_cases(argv[1], argv[2]) : mode == L"--uia-dpi" ? dpi_case(argv[1], argv[2]) : mode == L"--preflight" ? preflight(argv[1], argv[2]) : mode == L"--ime-simulation" ? ime_simulation(argv[1], argv[2]) : false; CoUninitialize(); return ok ? 0 : 1; }
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
        RECT window_rect{}, root_rect{};
        IUIAutomationCondition *title_condition=nullptr, *button_condition=nullptr;
        IUIAutomationElement *title=nullptr, *close=nullptr; IUIAutomationElementArray* buttons=nullptr;
        VARIANT type; VariantInit(&type); type.vt=VT_I4; type.lVal=UIA_TitleBarControlTypeId;
        if (SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&title_condition)) &&
            SUCCEEDED(root->FindFirst(TreeScope_Children,title_condition,&title)) && title) {
            type.lVal=UIA_ButtonControlTypeId;
            if (SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&button_condition)) &&
                SUCCEEDED(title->FindAll(TreeScope_Children,button_condition,&buttons)) && buttons) {
                int count=0; buttons->get_Length(&count); if(count>0) buttons->GetElement(count-1,&close);
            }
        }
        RECT close_rect{};
        bool root_bounds = GetWindowRect(search.window, &window_rect) &&
            SUCCEEDED(root->get_CurrentBoundingRectangle(&root_rect)) && EqualRect(&window_rect, &root_rect);
        bool close_bounds = close && SUCCEEDED(close->get_CurrentBoundingRectangle(&close_rect)) &&
            close_rect.right > close_rect.left && close_rect.bottom > close_rect.top &&
            close_rect.right <= window_rect.right && close_rect.top >= window_rect.top;
        std::cout << "UIA native frame root_bounds=" << root_bounds << " close_bounds=" << close_bounds << '\n';
        pass = root_bounds && close_bounds;
        if (buttons) buttons->Release(); if (close) close->Release(); if (title) title->Release();
        if (button_condition) button_condition->Release(); if (title_condition) title_condition->Release();
    }
    auto focus_events = new FocusEvents(); auto focus_subscribed = pass ? automation->AddFocusChangedEventHandler(nullptr, focus_events) : E_FAIL;
    if (pass) {
        CONTROLTYPEID type = 0; BSTR name = nullptr, label_id = nullptr; RECT bounds{}, client{};
        IUIAutomationElement* label = nullptr;
        auto hidden = find_id(automation, root, L"form.title.pages.v03.progress");
        bool field = SUCCEEDED(element->get_CurrentControlType(&type)) && type == UIA_EditControlTypeId &&
            SUCCEEDED(element->get_CurrentName(&name)) && name && std::wstring(name) == L"顯示名稱" &&
            SUCCEEDED(element->get_CurrentLabeledBy(&label)) && label && SUCCEEDED(label->get_CurrentAutomationId(&label_id)) &&
            label_id && std::wstring(label_id) == L"form.name.label" && SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds));
        IUIAutomationElementArray* descriptions = nullptr;
        int description_count = 0; bool described = false;
        if (SUCCEEDED(element->get_CurrentDescribedBy(&descriptions)) && descriptions && SUCCEEDED(descriptions->get_Length(&description_count))) {
            for (int i = 0; i < description_count; ++i) {
                IUIAutomationElement* label_element = nullptr; BSTR id = nullptr;
                if (SUCCEEDED(descriptions->GetElement(i, &label_element)) && label_element &&
                    SUCCEEDED(label_element->get_CurrentAutomationId(&id)) && id && std::wstring(id) == L"form.name.error") described = true;
                if (label_element) label_element->Release(); SysFreeString(id);
            }
        }
        field = field && described;
        if (descriptions) descriptions->Release();
        GetClientRect(search.window, &client); POINT origin{0, 0}; ClientToScreen(search.window, &origin);
        field = field && bounds.left >= origin.x && bounds.top >= origin.y && bounds.right <= origin.x + client.right && bounds.bottom <= origin.y + client.bottom && bounds.right > bounds.left && bounds.bottom > bounds.top && !hidden;
        if (hidden) hidden->Release(); if (label) label->Release(); SysFreeString(name); SysFreeString(label_id);
        bool name_focus = SUCCEEDED(focus_subscribed) && SUCCEEDED(element->SetFocus()) && WaitForSingleObject(focus_events->name_received, 5000) == WAIT_OBJECT_0;
        std::cout << "UIA14 field focus: " << (name_focus ? "PASS" : "FAIL") << '\n'; pass &= name_focus;
        auto structures = new StructureEvents();
        auto structure_subscribed = automation->AddStructureChangedEventHandler(root, TreeScope_Element, nullptr, structures);
        IUIAutomationValuePattern* cached_value = nullptr;
        HRESULT cached_pattern_hr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&cached_value));
        HRESULT tab_select_hr = select_id(automation, root, L"v02");
        BSTR cached_text = nullptr; HRESULT cached_read_hr = cached_value ? cached_value->get_CurrentValue(&cached_text) : E_FAIL;
        BSTR hidden_input = SysAllocString(L"hidden"); HRESULT cached_write_hr = cached_value ? cached_value->SetValue(hidden_input) : E_FAIL;
        bool cached_case = SUCCEEDED(tab_select_hr) && SUCCEEDED(cached_pattern_hr) && SUCCEEDED(cached_read_hr) && cached_text && cached_write_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        std::cout << "UIA10 cached edit: " << (cached_case ? "PASS" : "FAIL") << '\n'; pass &= cached_case;
        SysFreeString(cached_text); SysFreeString(hidden_input); if (cached_value) cached_value->Release();
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
        bool list_focus = SUCCEEDED(focus_subscribed) && SendMessageW(search.window, WM_APP + 80, 22, 0) == 0 && WaitForSingleObject(focus_events->received, 10000) == WAIT_OBJECT_0;
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
        bool focus = apply && SUCCEEDED(focus_subscribed) && SendMessageW(search.window, WM_APP + 80, 20, 0) == 0 && WaitForSingleObject(focus_events->apply_received, 2000) == WAIT_OBJECT_0;
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
        int selected_count = 0; IUIAutomationElement* initial_radio = nullptr; BSTR initial_radio_id = nullptr;
        radio = radio && SUCCEEDED(selected->get_Length(&selected_count)) && selected_count == 1 && SUCCEEDED(selected->GetElement(0, &initial_radio)) && initial_radio && SUCCEEDED(initial_radio->get_CurrentAutomationId(&initial_radio_id)) && initial_radio_id && std::wstring(initial_radio_id) == L"normal";
        SysFreeString(initial_radio_id); if (initial_radio) initial_radio->Release();
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
        auto collapse_events = new PropertyEvents(); collapse_events->expected = UIA_ExpandCollapseExpandCollapseStatePropertyId; collapse_events->expected_value = ExpandCollapseState_Collapsed;
        PROPERTYID expand_property = UIA_ExpandCollapseExpandCollapseStatePropertyId;
        auto expand_subscribed = region ? automation->AddPropertyChangedEventHandlerNativeArray(region, TreeScope_Element, nullptr, expand_events, &expand_property, 1) : E_FAIL;
        auto collapse_subscribed = region ? automation->AddPropertyChangedEventHandlerNativeArray(region, TreeScope_Element, nullptr, collapse_events, &expand_property, 1) : E_FAIL;
        bool dropdown = region && selected_id_is(region, L"north") && SUCCEEDED(region->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand &&
            SUCCEEDED(expand->Expand()) && SUCCEEDED(expand_subscribed) && WaitForSingleObject(expand_events->received, 2000) == WAIT_OBJECT_0 && SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Expanded &&
            SUCCEEDED(expand->Collapse()) && SUCCEEDED(collapse_subscribed) && WaitForSingleObject(collapse_events->received, 5000) == WAIT_OBJECT_0 && SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded == ExpandCollapseState_Collapsed && selected_id_is(region, L"north");
        dropdown = dropdown && SUCCEEDED(expand->Expand()) && SUCCEEDED(select_id(automation, region, L"south"));
        auto south = find_id(automation, region, L"south"); IUIAutomationSelectionItemPattern* south_pattern = nullptr; BOOL south_selected = FALSE;
        dropdown = dropdown && south && SUCCEEDED(south->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&south_pattern))) && south_pattern && SUCCEEDED(south_pattern->get_CurrentIsSelected(&south_selected)) && south_selected;
        if (south_pattern) south_pattern->Release(); if (south) south->Release();
        std::cout << "UIA05 dropdown: " << (dropdown ? "PASS" : "FAIL") << '\n'; pass &= dropdown;
        if (SUCCEEDED(collapse_subscribed)) automation->RemovePropertyChangedEventHandler(region, collapse_events); collapse_events->Release();
        if (SUCCEEDED(expand_subscribed)) automation->RemovePropertyChangedEventHandler(region, expand_events); expand_events->Release();
        if (expand) expand->Release(); if (region) region->Release();
        auto volume = find_id(automation, root, L"form.volume"); IUIAutomationRangeValuePattern* range = nullptr;
        double min = 0, max = 0, step = 0, large_step = 0, value_now = 0;
        auto range_events = new PropertyEvents(); range_events->expected = UIA_RangeValueValuePropertyId; range_events->double_value = true; range_events->expected_double = 65;
        PROPERTYID range_property = UIA_RangeValueValuePropertyId;
        auto range_subscribed = volume ? automation->AddPropertyChangedEventHandlerNativeArray(volume, TreeScope_Element, nullptr, range_events, &range_property, 1) : E_FAIL;
        bool slider = volume && SUCCEEDED(volume->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))) && range &&
            SUCCEEDED(range->get_CurrentMinimum(&min)) && min == 0 && SUCCEEDED(range->get_CurrentMaximum(&max)) && max == 100 &&
            SUCCEEDED(range->get_CurrentSmallChange(&step)) && step == 5 && SUCCEEDED(range->get_CurrentLargeChange(&large_step)) && large_step == 5 && SUCCEEDED(range->get_CurrentValue(&value_now)) && value_now == 40 && SUCCEEDED(range->SetValue(65)) && SUCCEEDED(range_subscribed) && WaitForSingleObject(range_events->received, 2000) == WAIT_OBJECT_0 &&
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
        bool progress_case = progress && SUCCEEDED(progress->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&progress_pattern))) && progress_pattern && SUCCEEDED(progress_pattern->get_CurrentIsReadOnly(&readonly)) && readonly && progress_pattern->SetValue(25) == static_cast<HRESULT>(UIA_E_INVALIDOPERATION) && SUCCEEDED(progress_pattern->get_CurrentValue(&progress_value)) && progress_value == 50 && SUCCEEDED(invoke_id(automation, root, L"form.title.pages.v03.above")) && SUCCEEDED(progress_subscribed) && WaitForSingleObject(progress_events->received, 2000) == WAIT_OBJECT_0 && SUCCEEDED(progress_pattern->get_CurrentValue(&progress_value)) && progress_value == 100;
        if (!progress_case) std::cout << "progress details element=" << bool(progress) << " pattern=" << bool(progress_pattern) << " readonly=" << readonly << " value=" << progress_value << " subscribe=0x" << std::hex << progress_subscribed << std::dec << '\n';
        std::cout << "UIA11 progress: " << (progress_case ? "PASS" : "FAIL") << '\n'; pass &= progress_case;
        if (SUCCEEDED(progress_subscribed)) automation->RemovePropertyChangedEventHandler(progress, progress_events); progress_events->Release(); if (progress_pattern) progress_pattern->Release(); if (progress) progress->Release();
        invoke_id(automation, root, L"form.title.pages.v03.half");
        auto modal_target = find_id(automation, root, L"form.title.pages.v03.temporary"); IUIAutomationInvokePattern* modal_action = nullptr;
        if (modal_target) modal_target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&modal_action));
        auto rapid_open = new SelectionEvents(); rapid_open->expected = UIA_Window_WindowOpenedEventId;
        auto rapid_close = new SelectionEvents(); rapid_close->expected = UIA_Window_WindowClosedEventId;
        auto rapid_open_sub = automation->AddAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, TreeScope_Subtree, nullptr, rapid_open);
        auto rapid_close_sub = automation->AddAutomationEventHandler(UIA_Window_WindowClosedEventId, root, TreeScope_Subtree, nullptr, rapid_close);
        bool rapid_events = SUCCEEDED(rapid_open_sub) && SUCCEEDED(rapid_close_sub) && SendMessageW(search.window, WM_APP + 80, 25, 0) == 0 && WaitForSingleObject(rapid_open->received, 5000) == WAIT_OBJECT_0 && WaitForSingleObject(rapid_close->received, 5000) == WAIT_OBJECT_0;
        if (SUCCEEDED(rapid_open_sub)) automation->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, root, rapid_open);
        if (SUCCEEDED(rapid_close_sub)) automation->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, root, rapid_close);
        rapid_open->Release(); rapid_close->Release();
        std::cout << "UIA12 rapid window transitions: " << (rapid_events ? "PASS" : "FAIL") << '\n'; pass &= rapid_events;
        auto closed_events = new SelectionEvents(); closed_events->expected = UIA_Window_WindowClosedEventId;
        auto closed_subscribed = automation->AddAutomationEventHandler(UIA_Window_WindowClosedEventId, root, TreeScope_Subtree, nullptr, closed_events);
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
        bool opener_focus = SUCCEEDED(focus_subscribed) && WaitForSingleObject(focus_events->opener_received, 5000) == WAIT_OBJECT_0;
        pass &= opener_focus;
        bool closed_event = SUCCEEDED(closed_subscribed) && WaitForSingleObject(closed_events->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(closed_subscribed)) automation->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, root, closed_events); closed_events->Release();
        std::cout << "UIA12 window closed event: " << (closed_event ? "PASS" : "FAIL") << '\n'; pass &= closed_event;
        std::cout << "UIA12 modal window: " << (window ? "PASS" : "FAIL") << '\n'; pass &= window;
        if (window_pattern) window_pattern->Release(); if (dialog) dialog->Release();
        if (window) SendMessageW(search.window, WM_APP + 80, 7, 0);
        auto half = find_id(automation, root, L"form.title.pages.v03.half"); BOOL half_enabled = FALSE;
        auto modal_progress = find_id(automation, root, L"form.title.pages.v03.progress"); IUIAutomationRangeValuePattern* modal_range = nullptr; double modal_value = 0;
        bool modal_read = modal_progress && SUCCEEDED(modal_progress->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&modal_range))) && modal_range && SUCCEEDED(modal_range->get_CurrentValue(&modal_value)) && modal_value == 50;
        if (modal_range) modal_range->Release(); if (modal_progress) modal_progress->Release();
        auto tabs = find_id(automation, root, L"form.title.pages.tabs");
        bool modal_background = modal_read && half && SUCCEEDED(half->get_CurrentIsEnabled(&half_enabled)) && half_enabled && tabs && select_id(automation, tabs, L"v01") == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        std::cout << "UIA12 background: " << (modal_background ? "PASS" : "FAIL") << '\n'; pass &= modal_background;
        if (half) half->Release(); if (tabs) tabs->Release();
        HRESULT modal_hr = modal_action ? modal_action->Invoke() : E_FAIL;
        mapping = mapping && modal_hr == static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
        SendMessageW(search.window, WM_APP + 80, 8, 0);
        auto tip_closed_events = new SelectionEvents(); tip_closed_events->expected = UIA_ToolTipClosedEventId;
        auto tip_closed_subscribed = automation->AddAutomationEventHandler(UIA_ToolTipClosedEventId, root, TreeScope_Subtree, nullptr, tip_closed_events);
        auto tip_events = new SelectionEvents(); tip_events->expected = UIA_ToolTipOpenedEventId;
        auto tip_subscribed = automation->AddAutomationEventHandler(UIA_ToolTipOpenedEventId, root, TreeScope_Subtree, nullptr, tip_events);
        auto tip_setup = SendMessageW(search.window, WM_APP + 80, 21, 0);
        auto tip_source = find_id(automation, root, L"form.title.pages.v03.half");
        auto focus_tip = tip_source ? tip_source->SetFocus() : E_FAIL;
        bool tip_event = tip_setup == 0 && SUCCEEDED(focus_tip) && SUCCEEDED(tip_subscribed) && WaitForSingleObject(tip_events->received, 3000) == WAIT_OBJECT_0;
        IUIAutomationElement* tip = nullptr;
        for (int attempt = 0; attempt < 30 && !tip; ++attempt) { tip = find_id(automation, root, L"form.title.pages.v03.half.tip"); if (!tip) Sleep(100); }
        CONTROLTYPEID tip_type = 0; BSTR tip_name = nullptr;
        bool tooltip = tip_event && tip && SUCCEEDED(tip->get_CurrentControlType(&tip_type)) && tip_type == UIA_ToolTipControlTypeId && SUCCEEDED(tip->get_CurrentName(&tip_name)) && tip_name && std::wstring(tip_name) == L"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002";
        if (!tooltip) std::cout << "tip setup=" << tip_setup << " event=" << tip_event << " element=" << bool(tip) << " type=" << tip_type << " subscribed=" << std::hex << tip_subscribed << std::dec << '\n';
        tooltip = tooltip && SendMessageW(search.window, WM_APP + 80, 24, 0) == 1;
        SendMessageW(search.window, WM_APP + 80, 23, 0);
        bool tip_closed = SUCCEEDED(tip_closed_subscribed) && WaitForSingleObject(tip_closed_events->received, 3000) == WAIT_OBJECT_0;
        if (SUCCEEDED(tip_closed_subscribed)) automation->RemoveAutomationEventHandler(UIA_ToolTipClosedEventId, root, tip_closed_events); tip_closed_events->Release(); if (tip_source) tip_source->Release();
        std::cout << "UIA13 tooltip closed event: " << (tip_closed ? "PASS" : "FAIL") << '\n'; pass &= tip_closed;
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
    CloseHandle(process.hProcess); pass = renderer_failure_cases(argv[1], argv[2]) && pass; CoUninitialize(); return pass ? 0 : 1;
}
