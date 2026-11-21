#include "../startup_report.hpp"
// Launches the V01 edit form. The appearance is chosen at link time (web or material);
// the form, its data and behavior are the same source for both executables.
#include "edit_form.hpp"
#include "isoduo/desktop.hpp"
#include <iostream>

#ifdef _WIN32
int wmain() {
#else
int main() {
#endif
    try {
        auto resolveFont = [](std::u16string_view family) {
            isoduo::PresentationSettings settings; settings.fontFamily = family;
            return isoduo::systemFontCatalog().resolve(settings);
        };
#ifdef _WIN32
        editForm::Form form(editForm::defaultIds(), true, resolveFont);
#else
        editForm::Form form(editForm::defaultIds(), true, resolveFont, isoduo::systemFontCatalog().families());
#endif
#ifdef ISODUO_APPEARANCE_material
        const auto& appearance = isoduo::materialAppearance();
#else
        const auto& appearance = isoduo::webAppearance();
#endif
        auto name = appearance.name();
        isoduo::DesktopConfig options;
        options.title = u"Isoduo 編輯表單 — " + std::u16string(name.begin(), name.end());
        auto creation = isoduo::Desktop::create(form.ui, appearance, [&] { return form.page(); }, form.scroll, options);
        if (!creation.desktop) { reportStartupFailure(creation.failure); return 1; }
        return creation.desktop->run().status == isoduo::Status::ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Isoduo: " << e.what() << '\n';
        return 1;
    }
}
