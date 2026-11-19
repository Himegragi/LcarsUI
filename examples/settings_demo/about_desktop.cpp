#include "about_desktop.hpp"
#include "isoduo/third_party_notices.hpp"
#include "about_licenses.hpp"
#include "demo_revision.hpp"
#include <cstdlib>
#include <memory>
namespace settingsDemo {
AboutInfo desktopAboutInfo(const isoduo::Appearance& appearance) {
    auto name = appearance.name();
    AboutInfo about;
    about.fonts = isoduo::systemFontCatalog().families();
    about.resolveFont = [](const isoduo::PresentationSettings& settings) { return isoduo::systemFontCatalog().resolve(settings); };
#ifdef __linux__
    const auto primary = about.resolveFont({}).primary;
    about.defaultFontLabel = primary.empty() ? u"\u9810\u8a2d" : u"\u9810\u8a2d\uff08" + primary + u"\uff09";
#endif
    about.appearance = std::u16string(name.begin(), name.end());
    std::string configuration = ISODUO_DEMO_CONFIGURATION;
    about.build = std::u16string(ISODUO_DEMO_REVISION) + u"、" + std::u16string(configuration.begin(), configuration.end());
    about.settingsPath = u"未知";
#ifdef _WIN32
    wchar_t* raw = nullptr; std::size_t length = 0;
    if (_wdupenv_s(&raw, &length, L"APPDATA") == 0 && raw) {
        std::unique_ptr<wchar_t, decltype(&std::free)> appdata(raw, &std::free);
        std::wstring path = std::wstring(appdata.get()) + L"\\Isoduo\\SettingsDemo\\settings.json";
        about.settingsPath.assign(path.begin(), path.end());
    }
#endif
    about.licenses.push_back({{"isoduo"}, u"Isoduo 框架 — MIT", std::u16string(frameworkLicense)});
    const char* ids[] = {"freetype", "harfbuzz", "khronos", "unicode"};
    std::size_t i = 0;
    for (const auto& notice : isoduo::thirdPartyNotices()) {
        auto label = std::u16string(notice.name) + u" " + std::u16string(notice.version) + u" — " + std::u16string(notice.license);
        about.licenses.push_back({{ids[i++]}, std::move(label), std::u16string(notice.text)});
    }
    about.licenses.push_back({{"json"}, u"nlohmann/json（僅本示範應用）— MIT", std::u16string(jsonLicense)});
    return about;
}
}
