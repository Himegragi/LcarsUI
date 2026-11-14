#include "../startup_report.hpp"
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
        lcars::DesktopConfig options; options.title = u"LcarsUI 範本清單";
        auto creation = lcars::Desktop::create(page.ui, appearance, [&] { return page.page(); }, page.scroll, options);
        if (!creation.desktop) { report_startup_failure(creation.failure); return 1; }
        return creation.desktop->run().status == lcars::Status::ok ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "LcarsUI: " << e.what() << '\n'; return 1; }
}
