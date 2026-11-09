#include "settings_demo.hpp"
#include "settings_store.hpp"
#include "desktop_host.hpp"
#include "core_internal.hpp"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace lcars;
using namespace settings_demo;
static void check(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
struct Walk {
    SettingsStore store;
    Demo demo;
    detail::DesktopHost host;
    HWND window;
    bool closed = false;
    Walk(const Appearance& appearance, const std::filesystem::path& path)
        : store(path), demo(info()), host(demo.ui,appearance,[this]{return demo.page();},demo.scroll,DesktopOptions{},false,true), window(host.window()) {
        auto loaded=store.load(); if(loaded.document) demo.load_document(*loaded.document);
        demo.on_save([this](const auto& document) { return store.save(document); });
        demo.on_dirty_changed([this](bool dirty) { SetWindowTextW(window,dirty ? L"*Demo" : L"Demo"); });
        host.on_close_request([this] { if (demo.request_close()) { closed=true; return CloseDecision::close; } return CloseDecision::keep_open; });
        demo.on_close([this] { closed=true; host.request_quit(); });
        ShowWindow(window,SW_SHOWNOACTIVATE); SendMessageW(window,WM_SETFOCUS,0,0); host.pump(); host.paint();
    }
    static AboutInfo info() {
        AboutInfo i; i.licenses={{{"lcars"},u"First",u"License"},{{"freetype"},u"Second",u"Other"}};
        for(int n=0;n<35;++n) { auto x=std::to_string(n); std::u16string v=u"Font "; v.append(x.begin(),x.end()); i.fonts.push_back({v,v}); }
        return i;
    }
    void tap(WPARAM key, bool ctrl=false, bool shift=false) {
        BYTE original[256]{}, state[256]{}; GetKeyboardState(original); std::copy(original,original+256,state);
        state[VK_CONTROL]=ctrl?0x80:0; state[VK_SHIFT]=shift?0x80:0; SetKeyboardState(state);
        PostMessageW(window,WM_KEYDOWN,key,1); PostMessageW(window,WM_KEYUP,key,0xc0000001); host.pump(); SetKeyboardState(original);
    }
    void text(std::u16string_view value) { for(auto c:value) { PostMessageW(window,WM_CHAR,c,1); host.pump(); } }
    void alt_f4() { PostMessageW(window,WM_SYSKEYDOWN,VK_F4,0x20000001); PostMessageW(window,WM_SYSKEYUP,VK_F4,0xe0000001); host.pump(); }
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
int main() { try { auto base=std::filesystem::temp_directory_path()/("lcars-keyboard-"+std::to_string(GetCurrentProcessId())); for(auto* appearance:{&web_appearance(),&material_appearance()}) { auto path=base/appearance->name(); std::filesystem::create_directories(path); run(*appearance,path); } std::filesystem::remove_all(base); return 0; } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }

