#include "about_desktop.hpp"
#include "lcars/third_party_notices.hpp"
#include "about_licenses.hpp"
#include "demo_revision.hpp"
#include <cstdlib>
#include <memory>
namespace settings_demo {
AboutInfo desktop_about_info(const lcars::Appearance& appearance) {
    auto name = appearance.name();
    AboutInfo about;
    about.fonts = lcars::system_font_catalog().families();
    about.resolve_font = [](const lcars::PresentationSettings& settings) { return lcars::system_font_catalog().resolve(settings); };
    about.appearance = std::u16string(name.begin(), name.end());
    std::string configuration = LCARS_DEMO_CONFIGURATION;
    about.build = std::u16string(LCARS_DEMO_REVISION) + u"、" + std::u16string(configuration.begin(), configuration.end());
    wchar_t* raw = nullptr; std::size_t length = 0;
    if (_wdupenv_s(&raw, &length, L"APPDATA") == 0 && raw) {
        std::unique_ptr<wchar_t, decltype(&std::free)> appdata(raw, &std::free);
        std::wstring path = std::wstring(appdata.get()) + L"\\LcarsUI\\SettingsDemo\\settings.json";
        about.settings_path.assign(path.begin(), path.end());
    } else about.settings_path = u"未知";
    about.licenses.push_back({{"lcars"}, u"LcarsUI 框架 — MIT", std::u16string(framework_license)});
    const char* ids[] = {"freetype", "harfbuzz", "khronos", "unicode"};
    std::size_t i = 0;
    for (const auto& notice : lcars::third_party_notices()) {
        auto label = std::u16string(notice.name) + u" " + std::u16string(notice.version) + u" — " + std::u16string(notice.license);
        about.licenses.push_back({{ids[i++]}, std::move(label), std::u16string(notice.text)});
    }
    about.licenses.push_back({{"json"}, u"nlohmann/json（僅本示範應用）— MIT", std::u16string(json_license)});
    return about;
}
}
