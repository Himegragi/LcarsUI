#include "desktop_host.hpp"
#include "settings_demo.hpp"
#include <windows.h>
#include <iostream>
namespace {
settings_demo::Demo* active = nullptr;
WNDPROC original_proc = nullptr;
LRESULT CALLBACK test_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
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
        const auto& appearance = argc > 1 && std::wstring_view(argv[1]) == L"material" ? lcars::material_appearance() : lcars::web_appearance();
        lcars::DesktopOptions options; options.title = u"LcarsUI UIA demo fixture";
        lcars::detail::DesktopHost host(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options, false, true);
        active = &demo; original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(test_proc)));
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        return host.run();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
