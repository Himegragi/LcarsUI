// Launches the V01 edit form. The appearance is chosen at link time (web or material);
// the form, its data and behavior are the same source for both executables.
#include "edit_form.hpp"
#include "lcars/desktop.hpp"
#include <iostream>

int wmain() {
    try {
        edit_form::Form form(edit_form::default_ids(), true);
#ifdef LCARS_APPEARANCE_material
        const auto& appearance = lcars::material_appearance();
#else
        const auto& appearance = lcars::web_appearance();
#endif
        auto name = appearance.name();
        lcars::DesktopOptions options;
        options.title = u"LcarsUI 編輯表單 — " + std::u16string(name.begin(), name.end());
        return lcars::run_desktop(form.ui, appearance, [&] { return form.page(); }, form.scroll, options);
    } catch (const std::exception& e) {
        std::cerr << "LcarsUI: " << e.what() << '\n';
        return 1;
    }
}
