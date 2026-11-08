#include "settings_demo.hpp"
#include "lcars/desktop.hpp"
#include <iostream>
int wmain() {
    try {
        settings_demo::Demo demo;
#ifdef LCARS_APPEARANCE_material
        const auto& appearance = lcars::material_appearance();
#else
        const auto& appearance = lcars::web_appearance();
#endif
        auto name = appearance.name();
        lcars::DesktopOptions options;
        options.title = u"設定管理示範 — " + std::u16string(name.begin(), name.end());
        return lcars::run_desktop(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
