#include "settings_demo.hpp"
#include "settings_store.hpp"
#include "desktop_host.hpp"
#include "core_internal.hpp"
#include <windows.h>
#include <imm.h>
#include "real_input_profiles.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace lcars;
using namespace settings_demo;
static std::ofstream* event_log = nullptr;
static void check(bool value, const char* message) { if(event_log) *event_log << (value ? "PASS\t" : "FAIL\t") << message << '\n'; if(!value) throw std::runtime_error(message); }
static bool real_input = false;
static Profiles* profiles = nullptr;
struct Walk {
    SettingsStore store;
    Demo demo;
    detail::DesktopHost host;
    HWND window;
    bool closed = false;
    bool observe_close = false, callback_composing = true;
    std::u16string callback_draft;
    Walk(const Appearance& appearance, const std::filesystem::path& path)
        : store(path), demo(info()), host(demo.ui,appearance,[this]{return demo.page();},demo.scroll,DesktopOptions{},real_input,!real_input), window(host.window()) {
        auto loaded=store.load(); if(loaded.document) demo.load_document(*loaded.document);
        demo.on_save([this](const auto& document) { return store.save(document); });
        demo.on_dirty_changed([this](bool dirty) { SetWindowTextW(window,dirty ? L"*Demo" : L"Demo"); });
        host.on_close_request([this] { if (observe_close) { auto* edit=detail::access(demo.ui).edit("general.name"); callback_composing=edit && edit->composing; callback_draft=demo.name.draft().value.value_or(u""); } if (demo.request_close()) { closed=true; return CloseDecision::close; } return CloseDecision::keep_open; });
        demo.on_close([this] { closed=true; host.request_quit(); });
        if (real_input) {
            ShowWindow(window,SW_SHOW); SetForegroundWindow(window); wait(300);
            if (GetForegroundWindow()!=window) { ShowWindow(window,SW_MINIMIZE); ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); wait(300); }
            check(GetForegroundWindow()==window,"could not bring test window to foreground");
        } else { ShowWindow(window,SW_SHOWNOACTIVATE); SendMessageW(window,WM_SETFOCUS,0,0); host.pump(); }
        host.paint();
    }
    ~Walk() {
        if (event_log) for (const auto& line : host.ime_log()) {
            int n=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
            std::string utf8(n,'\0'); WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),utf8.data(),n,nullptr,nullptr);
            *event_log << "IME\t" << utf8 << '\n';
        }
    }
    static AboutInfo info() {
        AboutInfo i; i.licenses={{{"lcars"},u"First",u"License"},{{"freetype"},u"Second",u"Other"}};
        for(int n=0;n<35;++n) { auto x=std::to_string(n); std::u16string v=u"Font "; v.append(x.begin(),x.end()); i.fonts.push_back({v,v}); }
        return i;
    }
    void wait(int ms) { auto end=GetTickCount64()+ms; while(GetTickCount64()<end) { host.pump(); Sleep(5); } }
    void send(WORD key, DWORD flags=0) {
        check(GetForegroundWindow()==window,"foreground moved away; input stopped");
        INPUT in{}; in.type=INPUT_KEYBOARD; in.ki.wVk=(flags & KEYEVENTF_UNICODE)?0:key; in.ki.wScan=(flags & KEYEVENTF_UNICODE)?key:static_cast<WORD>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)); in.ki.dwFlags=flags;
        check(SendInput(1,&in,sizeof(in))==1,"SendInput failed"); wait(60);
    }
    void tap(WPARAM key, bool ctrl=false, bool shift=false) {
        if (real_input) { if(ctrl) send(VK_CONTROL); if(shift) send(VK_SHIFT); send(WORD(key)); send(WORD(key),KEYEVENTF_KEYUP); if(shift) send(VK_SHIFT,KEYEVENTF_KEYUP); if(ctrl) send(VK_CONTROL,KEYEVENTF_KEYUP); return; }
        BYTE original[256]{}, state[256]{}; GetKeyboardState(original); std::copy(original,original+256,state);
        state[VK_CONTROL]=ctrl?0x80:0; state[VK_SHIFT]=shift?0x80:0; SetKeyboardState(state);
        PostMessageW(window,WM_KEYDOWN,key,1); PostMessageW(window,WM_KEYUP,key,0xc0000001); host.pump(); SetKeyboardState(original);
    }
    void text(std::u16string_view value) {
        if (real_input && value==u"共用範本 A") {
            check(profiles->chinese(),"Microsoft Bopomofo unavailable"); wait(300);
            if (auto imc=ImmGetContext(window)) { DWORD conversion=0,sentence=0; ImmGetConversionStatus(imc,&conversion,&sentence); ImmSetConversionStatus(imc,conversion|IME_CMODE_NATIVE,sentence); ImmReleaseContext(window,imc); }
            // ㄍㄨㄥˋ ㄩㄥˋ ㄈㄢˋ ㄅㄣˇ; Enter commits the composition (the same commit key as the showcase PR01 run).
            for (auto key : std::string("EJ/4M/4Z041P3")) { SHORT vk=VkKeyScanA(key); check(vk!=-1,"Bopomofo key unavailable"); tap(LOBYTE(vk)); }
            tap(VK_RETURN); wait(250);
            profiles->us(); wait(200); send(' ',KEYEVENTF_UNICODE); send(' ',KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); send('A',KEYEVENTF_UNICODE); send('A',KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); return;
        }
        for(auto c:value) { if(real_input) { send(c,KEYEVENTF_UNICODE); send(c,KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); } else { PostMessageW(window,WM_CHAR,c,1); host.pump(); } }
    }
    void alt_f4() { if(real_input) { send(VK_MENU); tap(VK_F4); send(VK_MENU,KEYEVENTF_KEYUP); return; } PostMessageW(window,WM_SYSKEYDOWN,VK_F4,0x20000001); PostMessageW(window,WM_SYSKEYUP,VK_F4,0xe0000001); host.pump(); }
    void tabs(int count, bool shift=false) { while(count--) tap(VK_TAB,false,shift); }
    bool focused(const char* id) { return detail::access(demo.ui).focused()==id; }
};
static void run(const Appearance& appearance, const std::filesystem::path& path) {
    {
        Walk w(appearance,path); auto& d=w.demo;
        check(w.focused("app.nav") && d.nav.selected()->id=="general","step 1: navigation");
        w.tap(VK_TAB); w.tap('A',true); w.text(u"工作站 A"); w.tap(VK_TAB);
        wchar_t title[32]{}; GetWindowTextW(w.window,title,32);
        check(d.document().name==u"工作站 A" && d.is_dirty() && title[0]==L'*',"step 2: name and dirty title");
        w.tap(VK_SPACE); w.tap(VK_TAB); w.tap(VK_DOWN); w.tap(VK_TAB); w.tap(VK_RETURN); w.tap(VK_DOWN); w.tap(VK_DOWN); w.tap(VK_RETURN); w.tap(VK_TAB); for(int i=0;i<5;i++) w.tap(VK_RIGHT);
        check(!d.document().notices && d.document().mode==OptionValue{"focus"} && d.document().region==OptionValue{"south"} && d.document().volume==65,"step 3: general values");
        w.tap('Z',true); check(d.document().volume==40,"step 4: undo"); w.tap('Y',true); check(d.document().volume==65,"step 4: redo");
        w.tap('S',true); GetWindowTextW(w.window,title,32); check(!d.is_dirty() && title[0]!=L'*' && w.store.load().document->volume==65,"step 5: save");
        w.tabs(5,true); w.tap(VK_DOWN); w.tap(VK_TAB); w.text(u"共用"); w.tap(VK_TAB); w.tap(VK_SPACE); w.tap(VK_RETURN); w.tap('A',true); w.text(u"共用範本 A"); w.tabs(3); w.tap(VK_SPACE);
        check(!d.item_dialog.is_open().value.value_or(true) && w.focused("items.list") && d.document().items[4].id==OptionValue{"T005"} && d.document().items[4].name==u"共用範本 A","step 6: edit and focus");
        w.tap(VK_DELETE); w.tap(VK_ESCAPE); check(!d.delete_dialog.is_open().value.value_or(true) && d.document().items.size()==20,"step 7: cancel deletion"); w.tap(VK_DELETE); w.tap(VK_TAB); w.tap(VK_SPACE);
        check(d.document().items.size()==19,"step 7: delete"); w.tap('Z',true);
        check(d.document().items.size()==20 && d.document().items[4].name==u"共用範本 A","step 7: undo deletion");
        w.tabs(2,true); w.tap(VK_DOWN); w.tap(VK_TAB); w.tap(VK_RETURN); w.tap(VK_END); w.tap(VK_RETURN); w.tap(VK_TAB); w.tap(VK_END); w.tap(VK_TAB); w.tap(VK_SPACE);
        check(!d.ui.presentation().font_family.empty() && d.ui.presentation().text_size==28 && d.ui.presentation().variant==ThemeVariant::dark(),"step 8: appearance");
        w.tabs(3,true); w.tap(VK_DOWN); w.tap(VK_TAB); w.tap(VK_DOWN); check(d.nav.selected()->id=="about" && d.licenses.describe()->options[1].selected,"step 9: about");
        w.tap('S',true); check(!d.is_dirty(),"step 10: save"); w.alt_f4(); check(w.closed,"step 10: close");
    }
    {
        Walk w(appearance,path); auto& d=w.demo; check(d.document().name==u"工作站 A" && !d.document().notices && d.document().mode==OptionValue{"focus"} && d.document().region==OptionValue{"south"} && d.document().volume==65 && d.document().items[4].name==u"共用範本 A" && !d.ui.presentation().font_family.empty() && d.ui.presentation().text_size==28 && d.ui.presentation().variant==ThemeVariant::dark(),"step 11: reload");
        w.tabs(5); w.tap(VK_RIGHT); check(d.document().volume==70,"step 12: edit"); w.alt_f4(); check(d.close_dialog.is_open().value.value_or(false),"step 12: prompt"); w.tap(VK_ESCAPE); check(!w.closed,"step 12: cancel"); w.alt_f4(); w.tap(VK_TAB); w.tap(VK_SPACE); check(w.closed,"step 12: discard");
    }
    { Walk w(appearance,path); check(w.demo.document().volume==65,"step 12: reload saved volume"); }
}
int wmain(int argc,wchar_t** argv) {
    std::filesystem::path evidence; bool com=false;
    try {
        if(argc>1) { check(argc==4 && std::wstring_view(argv[1])==L"--real" && std::wstring_view(argv[2])==L"--evidence","Usage: lcars_demo_keyboard [--real --evidence DIR]"); real_input=true; evidence=argv[3]; std::filesystem::create_directories(evidence); check(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=0,"DPI awareness unavailable"); check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization failed"); com=true; profiles=new Profiles; profiles->us(); }
        auto base=std::filesystem::temp_directory_path()/("lcars-keyboard-"+std::to_string(GetCurrentProcessId()));
        std::ofstream kw,cr,events;
        if(real_input) { kw.open(evidence/"KW02.tsv"); cr.open(evidence/"CR09-real.tsv"); events.open(evidence/"demo-events.log"); event_log=&events; kw<<"case\tappearance\tstatus\tdetail\n"; cr<<"case\tappearance\tstatus\tdetail\n"; wchar_t path[]=L"C:\\Windows\\System32\\IME\\IMETC\\IMTCTIP.DLL"; DWORD ignored=0; DWORD size=GetFileVersionInfoSizeW(path,&ignored); std::vector<char> data(size); VS_FIXEDFILEINFO* info=nullptr; UINT len=0; if(size && GetFileVersionInfoW(path,0,size,data.data()) && VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&len)) events<<"IMTCTIP.DLL "<<HIWORD(info->dwFileVersionMS)<<'.'<<LOWORD(info->dwFileVersionMS)<<'.'<<HIWORD(info->dwFileVersionLS)<<'.'<<LOWORD(info->dwFileVersionLS)<<'\n'; else events<<"IMTCTIP.DLL version unknown\n"; }
        for(auto* appearance:{&web_appearance(),&material_appearance()}) {
            auto path=base/appearance->name(); std::filesystem::create_directories(path);
            try { run(*appearance,path); if(real_input) kw<<"KW02\t"<<appearance->name()<<"\tPASS\t12 steps\n"; }
            catch(const std::exception& e) { if(real_input) { kw<<"KW02\t"<<appearance->name()<<"\tFAIL\t"<<e.what()<<"\n"; events<<e.what()<<'\n'; } throw; }
            if(real_input) try {
                Walk w(*appearance,base/(std::string(appearance->name())+"-close")); w.demo.name.focus(); w.demo.name.set_value(u"Before"); check(profiles->chinese(),"Microsoft Bopomofo unavailable"); w.wait(300); if(auto imc=ImmGetContext(w.window)) { DWORD c=0,s=0; ImmGetConversionStatus(imc,&c,&s); ImmSetConversionStatus(imc,c|IME_CMODE_NATIVE,s); ImmReleaseContext(w.window,imc); }
                w.tap('J'); w.tap('I'); w.tap('3'); auto* edit=detail::access(w.demo.ui).edit("general.name"); check(edit && edit->composing && !edit->preedit.empty(),"composition did not start"); w.observe_close=true; w.alt_f4(); // E01: the host asks the IME to complete; Microsoft Bopomofo delivers 我 synchronously, so the handler reads the committed draft.
                check(!w.callback_composing && w.callback_draft==u"Before我","CR09 E01 result mismatch"); cr<<"CR09-real\t"<<appearance->name()<<"\tPASS\tcomposition completed before callback; draft=Before我\n"; profiles->us();
            } catch (const std::exception& e) { cr << "CR09-real\t" << appearance->name() << "\tFAIL\t" << e.what() << "\n"; throw; }
        }
        std::filesystem::remove_all(base); event_log=nullptr; delete profiles; if(com) CoUninitialize(); return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; delete profiles; if(com) CoUninitialize(); return 1; }
}

