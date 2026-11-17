#include "uia_demo_messages.hpp"
#include "desktop_host.hpp"
#include "core_internal.hpp"
#include "settings_demo.hpp"
#include "isoduo/semantics.hpp"
#include <windows.h>
#include <iostream>
namespace {
settingsDemo::Demo* active = nullptr;
WNDPROC originalProc = nullptr;
LRESULT CALLBACK testProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == uiaDemo::messageExpectPage) {
        const char* pages[] = {"general", "items", "appearance", "about", "items", "items", "general", "appearance"};
        if (w > 7 || !active->nav.selected() || active->nav.selected()->id != pages[w]) return 0;
        if (w == 4) return active->itemDialog.isOpen().value.value_or(false);
        if (w == 5) return active->deleteDialog.isOpen().value.value_or(false);
        if (w == 6) return active->closeDialog.isOpen().value.value_or(false);
        if (w == 7) return active->ui.semantics().get({"appearance.font"})->expanded;
        return 1;
    }
    if (message == uiaDemo::messageNavigate) {
        if (w > 7) return 1;
        const char* pages[] = {"general", "items", "appearance", "about", "items", "items", "general", "appearance"};
        if (!active->nav.select({pages[w]})) return 2;
        if (w == 4 && !active->itemsAdd.invoke()) return 3;
        if (w == 5 && (!active->itemsList.select({"T001"}) || !active->itemsDelete.invoke())) return 4;
        if (w == 6 && (!active->notices.toggle() || active->requestClose())) return 5;
        if (w == 7 && !active->font.open()) return 6;
        InvalidateRect(window, nullptr, FALSE); UpdateWindow(window);
        return 0;
    }
    if (message == uiaDemo::messageTextFlow) {  // #80: the Items search box gets a centered, 1.5-spaced presentation (w = 1) or the defaults (w = 0);
        // #84: w = 2 justifies it, w = 3 justifies it with the user's "justified text as left" setting on
        auto& search = active->itemsSearch;
        auto settings = active->ui.presentation(); settings.justifyAsLeft = w == 3;
        const auto align = w == 1 ? isoduo::TextAlign::center : w >= 2 ? isoduo::TextAlign::justify : isoduo::TextAlign::left;
        return static_cast<LRESULT>(search.setTextAlign(align).status) |
               static_cast<LRESULT>(search.setLineSpacing(w == 1 ? 1.5 : 1.0).status) |
               static_cast<LRESULT>(active->ui.setPresentation(settings).status);
    }
    if (message == uiaDemo::messageContextMenu) {
        // Context menu driver (#85). 0: open the Items list menu on row T003; 1: close; 2: open the search field's menu;
        // 3: press Down; the result is the Status as an integer (0 is ok).
        auto& tree = active->ui.semantics();
        switch (w) {
        case 0: return static_cast<LRESULT>(tree.perform({"items.list", isoduo::OptionValue{"T003"}}, isoduo::Verb::contextMenu).status);
        case 1: return static_cast<LRESULT>(active->ui.closeContextMenu().status);
        case 2: return static_cast<LRESULT>(tree.perform({"items.search"}, isoduo::Verb::contextMenu).status);
        case 3: return static_cast<LRESULT>(isoduo::detail::access(active->ui).keyDown({.key = isoduo::Key::down}).status);
        case 4: return static_cast<LRESULT>(isoduo::detail::access(active->ui).keyDown({.key = isoduo::Key::escape}).status);  // Escape closes the menu
        case 5: isoduo::detail::access(active->ui).windowActive(true); return 0;   // the window counts as active (before opening a menu to deactivate)
        case 6: isoduo::detail::access(active->ui).windowActive(false); return 0;  // deactivation closes an open menu
        default: return 99;
        }
    }
    if (message == uiaDemo::messageHover) {  // #74: w=1 simulates a pointer hover on the Items search box, w=0 clears it; the Status as an integer
        if (w == 1) return static_cast<LRESULT>(active->ui.simulateHover(active->itemsSearch).status);
        return static_cast<LRESULT>(active->ui.clearSimulatedHover().status);
    }
    if (message == uiaDemo::messageRichText) {  // #81: w=1 gives the item note a styled value, w=2 switches the theme (l=1 dark, l=0 light)
        if (w == 1) return static_cast<LRESULT>(active->itemNote.setRichValue({u"abcdef", {{.start = 0, .length = 2, .bold = true}, {.start = 2, .length = 2, .italic = true, .underline = true}, {.start = 4, .length = 2, .color = "red"}}}).status);
        if (w == 2) { auto settings = active->ui.presentation(); settings.variant = l ? isoduo::ThemeVariant::dark() : isoduo::ThemeVariant::light(); return static_cast<LRESULT>(active->ui.setPresentation(settings).status); }
        return 1;
    }
    if (message == uiaDemo::messageSelectInSearch) return static_cast<LRESULT>(active->itemsSearch.select(0, 2).status);
    if (message == uiaDemo::messageSetSearchValue) return static_cast<LRESULT>(active->itemsSearch.setValue(w ? u"sample" : u"").status);
    return CallWindowProcW(originalProc, window, message, w, l);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        settingsDemo::AboutInfo info;
        info.licenses.push_back({{"isoduo"}, u"License", std::u16string(3000, u'A') + u"LAST LINE"});
        for (int i = 0; i < 150; ++i) {
            auto number = std::to_wstring(i); std::u16string name = u"Font";
            name.append(number.begin(), number.end()); info.fonts.push_back({name, name});
        }
        settingsDemo::Demo demo(std::move(info));
        int saves = 0; demo.onSave([&](const auto&) { ++saves; return std::u16string{}; });
        const auto& appearance = argc > 1 && std::wstring_view(argv[1]) == L"material" ? isoduo::materialAppearance() : isoduo::webAppearance();
        isoduo::DesktopConfig options; options.title = argc > 1 && std::wstring_view(argv[1]) == L"material" ? u"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Material-inspired" : u"\u8a2d\u5b9a\u7ba1\u7406\u793a\u7bc4 \u2014 Minimal Web";
        isoduo::detail::DesktopHost host(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options, false, true);
        host.onCloseRequest([&] { return demo.requestClose() ? isoduo::CloseDecision::close : isoduo::CloseDecision::keepOpen; });
        demo.onClose([&] { host.onCloseRequest({}); PostMessageW(host.window(), WM_CLOSE, 0, 0); });
        demo.onDirtyChanged([&](bool dirty) { auto title = (dirty ? u"*" : u"") + options.title; SetWindowTextW(host.window(), reinterpret_cast<const wchar_t*>(title.c_str())); });
        active = &demo; originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(testProc)));
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        auto result = host.run();
        return saves == 0 ? result : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
