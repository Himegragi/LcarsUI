#include "desktop_host.hpp"
#include "core_internal.hpp"
#include "settings_demo.hpp"
#include "lcars/semantics.hpp"
#include <windows.h>
#include <iostream>
namespace {
settings_demo::Demo* active = nullptr;
WNDPROC original_proc = nullptr;
LRESULT CALLBACK test_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 84) {
        const char* pages[] = {"general", "items", "appearance", "about", "items", "items", "general", "appearance"};
        if (w > 7 || !active->nav.selected() || active->nav.selected()->id != pages[w]) return 0;
        if (w == 4) return active->item_dialog.is_open().value.value_or(false);
        if (w == 5) return active->delete_dialog.is_open().value.value_or(false);
        if (w == 6) return active->close_dialog.is_open().value.value_or(false);
        if (w == 7) return active->ui.semantics().get({"appearance.font"})->expanded;
        return 1;
    }
    if (message == WM_APP + 83) {
        if (w > 7) return 1;
        const char* pages[] = {"general", "items", "appearance", "about", "items", "items", "general", "appearance"};
        if (!active->nav.select({pages[w]})) return 2;
        if (w == 4 && !active->items_add.invoke()) return 3;
        if (w == 5 && (!active->items_list.select({"T001"}) || !active->items_delete.invoke())) return 4;
        if (w == 6 && (!active->notices.toggle() || active->request_close())) return 5;
        if (w == 7 && !active->font.open()) return 6;
        InvalidateRect(window, nullptr, FALSE); UpdateWindow(window);
        return 0;
    }
    if (message == WM_APP + 85) {  // #80: the Items search box gets a centered, 1.5-spaced presentation (w = 1) or the defaults (w = 0);
        // #84: w = 2 justifies it, w = 3 justifies it with the user's "justified text as left" setting on
        auto& search = active->items_search;
        auto settings = active->ui.presentation(); settings.justify_as_left = w == 3;
        const auto align = w == 1 ? lcars::TextAlign::center : w >= 2 ? lcars::TextAlign::justify : lcars::TextAlign::left;
        return static_cast<LRESULT>(search.set_text_align(align).status) |
               static_cast<LRESULT>(search.set_line_spacing(w == 1 ? 1.5 : 1.0).status) |
               static_cast<LRESULT>(active->ui.set_presentation(settings).status);
    }
    if (message == WM_APP + 86) {
        // Context menu driver (#85). 0: open the Items list menu on row T003; 1: close; 2: open the search field's menu;
        // 3: press Down; the result is the Status as an integer (0 is ok).
        auto& tree = active->ui.semantics();
        switch (w) {
        case 0: return static_cast<LRESULT>(tree.perform({"items.list", lcars::OptionValue{"T003"}}, lcars::Verb::context_menu).status);
        case 1: return static_cast<LRESULT>(active->ui.close_context_menu().status);
        case 2: return static_cast<LRESULT>(tree.perform({"items.search"}, lcars::Verb::context_menu).status);
        case 3: return static_cast<LRESULT>(lcars::detail::access(active->ui).key_down({.key = lcars::Key::down}).status);
        default: return 99;
        }
    }
    if (message == WM_APP + 82) return static_cast<LRESULT>(active->items_search.select(0, 2).status);
    if (message == WM_APP + 81) return static_cast<LRESULT>(active->items_search.set_value(w ? u"sample" : u"").status);
    return CallWindowProcW(original_proc, window, message, w, l);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        settings_demo::AboutInfo info;
        info.licenses.push_back({{"lcars"}, u"License", std::u16string(3000, u'A') + u"LAST LINE"});
        for (int i = 0; i < 150; ++i) {
            auto number = std::to_wstring(i); std::u16string name = u"Font";
            name.append(number.begin(), number.end()); info.fonts.push_back({name, name});
        }
        settings_demo::Demo demo(std::move(info));
        int saves = 0; demo.on_save([&](const auto&) { ++saves; return std::u16string{}; });
        const auto& appearance = argc > 1 && std::wstring_view(argv[1]) == L"material" ? lcars::material_appearance() : lcars::web_appearance();
        lcars::DesktopOptions options; options.title = argc > 1 && std::wstring_view(argv[1]) == L"material" ? u"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Material-inspired" : u"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Minimal Web";
        lcars::detail::DesktopHost host(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options, false, true);
        host.on_close_request([&] { return demo.request_close() ? lcars::CloseDecision::close : lcars::CloseDecision::keep_open; });
        demo.on_close([&] { host.on_close_request({}); PostMessageW(host.window(), WM_CLOSE, 0, 0); });
        demo.on_dirty_changed([&](bool dirty) { auto title = (dirty ? u"*" : u"") + options.title; SetWindowTextW(host.window(), reinterpret_cast<const wchar_t*>(title.c_str())); });
        active = &demo; original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(test_proc)));
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        auto result = host.run();
        return saves == 0 ? result : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
