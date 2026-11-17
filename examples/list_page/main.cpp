#include "../startup_report.hpp"
#include "list_page.hpp"
#include "isoduo/desktop.hpp"
#include <iostream>

int wmain() {
    try {
        listPage::Page page;
#ifdef ISODUO_APPEARANCE_material
        const auto& appearance = isoduo::materialAppearance();
#else
        const auto& appearance = isoduo::webAppearance();
#endif
        isoduo::DesktopConfig options; options.title = u"Isoduo 範本清單";
        auto creation = isoduo::Desktop::create(page.ui, appearance, [&] { return page.page(); }, page.scroll, options);
        if (!creation.desktop) { reportStartupFailure(creation.failure); return 1; }
        return creation.desktop->run().status == isoduo::Status::ok ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "Isoduo: " << e.what() << '\n'; return 1; }
}
