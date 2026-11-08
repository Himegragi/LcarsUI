#include "desktop_host.hpp"
#include "edit_form.hpp"
#include <windows.h>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    try {
        const bool material = argc > 1 && std::wstring_view(argv[1]) == L"material";
        edit_form::Form form(edit_form::default_ids(), true, [](std::u16string_view family) {
            lcars::PresentationSettings settings; settings.font_family = family;
            return lcars::system_font_catalog().resolve(settings);
        });
        const auto& appearance = material ? lcars::material_appearance() : lcars::web_appearance();
        lcars::DesktopOptions options; options.title = u"LcarsUI UIA test host";
        lcars::detail::DesktopHost host(form.ui, appearance, [&] { return form.page(); }, form.scroll, options, false, true);
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        return host.run();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
