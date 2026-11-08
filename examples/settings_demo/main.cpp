#include "settings_demo.hpp"
#include "lcars/desktop.hpp"
#include "about_desktop.hpp"
#include <iostream>
int wmain() {
    try {
#ifdef LCARS_APPEARANCE_material
        const auto& appearance = lcars::material_appearance();
#else
        const auto& appearance = lcars::web_appearance();
#endif
        auto name = appearance.name();
        settings_demo::Demo demo(settings_demo::desktop_about_info(appearance));
        lcars::DesktopOptions options;
        options.title = u"設定管理示範 — " + std::u16string(name.begin(), name.end());
        auto creation = lcars::Desktop::create(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options);
        if (!creation.desktop) return 1;
        demo.on_dirty_changed([&](bool dirty) { creation.desktop->set_title((dirty ? u"*" : u"") + options.title); });
        auto caps = creation.desktop->capabilities();
        demo.set_renderer(caps.version + u" — " + caps.renderer);
        return creation.desktop->run().status == lcars::Status::ok ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
