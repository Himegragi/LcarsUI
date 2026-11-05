#include "list_page.hpp"
#include "lcars/desktop.hpp"
#include <iostream>

int wmain() {
    try {
        list_page::Page page;
#ifdef LCARS_APPEARANCE_material
        const auto& appearance = lcars::material_appearance();
#else
        const auto& appearance = lcars::web_appearance();
#endif
        lcars::DesktopOptions options; options.title = u"LcarsUI 範本清單";
        return lcars::run_desktop(page.ui, appearance, [&] { return page.page(); }, page.scroll, options);
    } catch (const std::exception& e) { std::cerr << "LcarsUI: " << e.what() << '\n'; return 1; }
}
