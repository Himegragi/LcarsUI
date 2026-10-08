// THROWAWAY: Win32 controls are a harness for the state model, not LcarsUI rendering.
#include <windows.h>
#include "state_model.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>

static std::wstring get_text(HWND h) {
    const auto length=GetWindowTextLengthW(h);
    std::wstring text(length+1,L'\0'); GetWindowTextW(h,text.data(),length+1); text.resize(length); return text;
}
static std::string utf8(const std::wstring& text) {
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0'); WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr); return result;
}
struct App {
    struct Native { HWND edit{},label{}; std::uint64_t instance{}; };
    HWND window{},summary{};
    HFONT font{};
    bool silent=false, reverse=false, extra=false, show_name=true;
    probe::Runtime runtime;
    probe::Value name{L"Alice",0}, email{L"alice@example.com",0}, note{L"Optional note",0};
    std::map<std::string,Native> views;
    unsigned changes=0;
    std::wstring status=L"點選欄位輸入，再按重排／插入。草稿應保留；右側顯示完整狀態。";
    probe::Form declare() const {
        // Proposal: stable semantic keys, not vector indexes or native handles.
        probe::Form fields;
        if(show_name) fields.push_back(probe::TextField("profile.name",L"名稱",name));
        fields.push_back(probe::TextField("profile.email",L"電子郵件",email));
        if(extra) fields.insert(fields.begin(),probe::TextField("profile.note",L"備註",note));
        if(reverse) std::reverse(fields.begin(),fields.end());
        return fields;
    }
    HWND make(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
        auto control=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!control) throw std::runtime_error("Create control failed");
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return control;
    }
    void init() {
        font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        make(L"STATIC",L"C++ 宣告式狀態原型（可丟棄）\r\n問題：更新畫面結構時，輸入草稿、焦點與選取能否沿用？\r\nWin32 控制項僅作測試載體；不是最終外觀或文字引擎。",0,20,15,900,65);
        const wchar_t* buttons[]={L"重排欄位",L"插入／移除備註",L"外部更新名稱",L"儲存名稱",L"復原名称",L"移除／重建名稱",L"重複 ID（應拒絕）"};
        for(int i=0;i<7;++i) make(L"BUTTON",buttons[i],WS_TABSTOP|BS_PUSHBUTTON,20+(i%4)*220,95+(i/4)*38,210,30,100+i);
        summary=make(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_BORDER,480,185,450,360);
        refresh();
    }
    void remember_selection() {
        for(auto& [id,v]:views) if(runtime.nodes.contains(id)) {
            DWORD begin{},end{}; SendMessageW(v.edit,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
            runtime.nodes.at(id).selection_begin=begin; runtime.nodes.at(id).selection_end=end;
        }
    }
    std::wstring snapshot() const {
        std::wostringstream s;
        s << L"已派送的使用者變更事件：" << changes << L"\r\n邏輯焦點：" << std::wstring(runtime.focus.begin(),runtime.focus.end()) << L"\r\n\r\n";
        for(const auto& id:runtime.order) {
            const auto& n=runtime.nodes.at(id);
            s << std::wstring(id.begin(),id.end()) << L"  instance=" << n.instance << L"\r\n"
              << L"  model: " << n.declared.value.text << L" (rev " << n.declared.value.revision << L")\r\n"
              << L"  draft: " << n.draft << L"\r\n"
              << L"  dirty=" << n.dirty << L" conflict=" << n.conflict
              << L" selection=" << n.selection_begin << L":" << n.selection_end << L"\r\n\r\n";
        }
        s << status;
        return s.str();
    }
    void show_snapshot() { SetWindowTextW(summary,snapshot().c_str()); }
    void refresh() {
        remember_selection();
        runtime.apply(declare());
        silent=true;
        std::erase_if(views,[&](auto& entry) {
            auto it=runtime.nodes.find(entry.first);
            if(it!=runtime.nodes.end() && entry.second.instance==it->second.instance) return false;
            DestroyWindow(entry.second.edit); DestroyWindow(entry.second.label); return true;
        });
        int row=0;
        HWND previous=summary;
        for(const auto& id:runtime.order) {
            auto& n=runtime.nodes.at(id);
            if(!views.contains(id)) {
                Native v;
                v.instance=n.instance;
                v.label=make(L"STATIC",n.declared.label.c_str(),0,20,190,420,22);
                v.edit=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,20,215,420,30);
                views.emplace(id,v);
            }
            auto& v=views.at(id);
            MoveWindow(v.label,20,190+row*90,420,22,TRUE);
            MoveWindow(v.edit,20,215+row*90,420,30,TRUE);
            SetWindowPos(v.label,previous,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            SetWindowPos(v.edit,v.label,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            previous=v.edit;
            SetWindowTextW(v.label,n.declared.label.c_str());
            if(get_text(v.edit)!=n.draft) SetWindowTextW(v.edit,n.draft.c_str());
            SendMessageW(v.edit,EM_SETSEL,n.selection_begin,n.selection_end);
            ++row;
        }
        if(!runtime.focus.empty() && views.contains(runtime.focus)) SetFocus(views.at(runtime.focus).edit);
        silent=false;
        show_snapshot();
    }
    void drain() {
        // Application notification happens after the native edit callback unwinds.
        while(auto change=runtime.next_event()) {
            ++changes;
            status=L"已收到變更事件；重新宣告不會再次發出變更。";
        }
        refresh();
    }
    void command(WPARAM w,LPARAM l) {
        if(silent) return;
        const auto source=reinterpret_cast<HWND>(l);
        for(const auto& [id,v]:views) if(v.edit==source) {
            if(HIWORD(w)==EN_SETFOCUS) { runtime.focus=id; show_snapshot(); }
            if(HIWORD(w)==EN_KILLFOCUS) { remember_selection(); show_snapshot(); }
            if(HIWORD(w)==EN_CHANGE) { runtime.edit(id,get_text(v.edit)); PostMessageW(window,WM_APP+1,0,0); }
            return;
        }
        if(HIWORD(w)!=BN_CLICKED) return;
        switch(LOWORD(w)) {
        case 100: reverse=!reverse; status=L"欄位順序改變；既有 instance 不變。"; break;
        case 101: extra=!extra; status=L"新增／移除其他欄位，不改名稱欄位的識別。"; break;
        case 102: name.text=L"External "+std::to_wstring(++name.revision); status=L"外部資料已更新；dirty 草稿保留並標示 conflict。"; break;
        case 103:
            if(runtime.nodes.contains("profile.name")) { name.text=runtime.nodes.at("profile.name").draft; ++name.revision; }
            status=L"明確儲存：草稿覆寫應用模型；本原型未做磁碟保存。"; break;
        case 104: if(runtime.nodes.contains("profile.name")) runtime.revert("profile.name"); break;
        case 105: show_name=!show_name; break;
        case 106: {
            auto bad=declare(); bad.push_back(bad.front());
            try { runtime.apply(bad); status=L"錯誤：重複 ID 未拒絕"; }
            catch(const std::invalid_argument&) { status=L"重複 ID 已拒絕；原畫面與資料未改動。"; }
            show_snapshot(); return;
        }
        default:return;
        }
        refresh();
    }
};
static App* app=nullptr;
static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_DESTROY) { PostQuitMessage(0); return 0; }
    if(app && m==WM_COMMAND) { app->command(w,l); return 0; }
    if(app && m==WM_APP+1) { app->drain(); return 0; }
    return DefWindowProcW(h,m,w,l);
}
static void check(bool ok,const char* name) {
    std::cout << (ok?"PASS ":"FAIL ") << name << '\n';
    if(!ok) throw std::runtime_error(name);
}
static void walkthrough(App& a) {
    check(probe::language_probe()==26,"C++26 and independent state library");
    auto original=a.views.at("profile.name").edit;
    const auto instance=a.runtime.nodes.at("profile.name").instance;
    SetFocus(original); SetWindowTextW(original,L"Alice edited"); SendMessageW(original,EM_SETSEL,2,7);
    a.drain();
    a.reverse=true; a.extra=true; a.refresh();
    DWORD begin{},end{}; SendMessageW(original,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
    check(a.views.at("profile.name").edit==original && a.runtime.nodes.at("profile.name").instance==instance,"reorder/insert preserve native handle and instance");
    check(GetFocus()==original && begin==2 && end==7,"reorder/insert preserve native focus and selection");
    check(GetNextDlgTabItem(a.window,a.views.at(a.runtime.order[0]).edit,FALSE)==a.views.at(a.runtime.order[1]).edit,"keyboard tab order follows declared order");
    check(get_text(original)==L"Alice edited","reorder preserves unsaved input");
    a.name={L"Server update",1}; a.refresh();
    check(a.runtime.nodes.at("profile.name").conflict && get_text(original)==L"Alice edited","external update preserves dirty draft and marks conflict");
    a.email={L"new@example.com",1}; a.refresh();
    check(get_text(a.views.at("profile.email").edit)==L"new@example.com","external update refreshes clean field");
    const auto count=a.changes; a.drain(); a.refresh();
    check(a.changes==count && count==1,"programmatic refresh does not recurse into change events");
    auto invalid=a.declare(); invalid.push_back(invalid.front());
    const auto order=a.runtime.order;
    bool rejected=false; try {a.runtime.apply(invalid);} catch(const std::invalid_argument&){rejected=true;}
    check(rejected && a.runtime.order==order && a.runtime.nodes.at("profile.name").draft==L"Alice edited","duplicate identity rejected before mutation");
    a.runtime.edit("profile.name",L"queued old event");
    a.show_name=false; a.refresh();
    check(a.runtime.focus.empty(),"removed field clears logical focus");
    a.show_name=true; a.refresh();
    check(a.runtime.nodes.at("profile.name").instance!=instance,"recreated key receives a new lifetime");
    check(!a.runtime.next_event().has_value(),"stale event cannot target recreated key");
    check(get_text(a.views.at("profile.name").edit)==L"Server update","recreated field uses current model");
    a.status=L"自動情境已通過。可繼續輸入、重排、外部更新與檢視衝突。"; a.show_snapshot();
    auto text=utf8(a.snapshot()); std::erase(text,'\r');
    std::ofstream("state-walkthrough.txt",std::ios::binary) << text << '\n';
}
static void capture_window(HWND window) {
    RECT rect{}; GetWindowRect(window,&rect); int w=rect.right-rect.left,h=rect.bottom-rect.top;
    HDC screen=GetDC(window), memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);
    auto old=SelectObject(memory,bitmap);
    UpdateWindow(window);
    const BOOL printed=PrintWindow(window,memory,0);
    SelectObject(memory,old);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);
    const int rows=GetDIBits(memory,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window,screen);
    if(!printed || rows!=h) throw std::runtime_error("Window capture failed");
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    std::ofstream out("state-window.bmp",std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file),sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
}
int main(int argc,char** argv) {
    try {
        std::cout << std::unitbuf;
        const bool interactive=argc>1 && std::string(argv[1])=="--interactive";
        SetProcessDPIAware();
        WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"LcarsStateProbe";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1); RegisterClassW(&wc);
        App a; app=&a;
        a.window=CreateWindowW(wc.lpszClassName,L"LcarsUI - Declarative State PROTOTYPE",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,980,630,nullptr,nullptr,wc.hInstance,nullptr);
        if(!a.window) throw std::runtime_error("window creation failed");
        a.init(); ShowWindow(a.window,SW_SHOW); UpdateWindow(a.window);
        walkthrough(a);
        capture_window(a.window);
        if(interactive) {
            MSG msg{};
            while(GetMessageW(&msg,nullptr,0,0)>0) { if(!IsDialogMessageW(a.window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
        } else DestroyWindow(a.window);
        app=nullptr;
        std::cout << "PASS all declarative-state scenarios\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
