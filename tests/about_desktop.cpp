#include "about_desktop.hpp"
#include "lcars/desktop.hpp"
#include "lcars/semantics.hpp"
#include "lcars/third_party_notices.hpp"
#include "lcars/version.hpp"
#include "check.hpp"
#include <fstream>
#include <iterator>

std::u16string license_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    test::check(bool(file), "license file is readable");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    std::u16string result;
    std::size_t start = 0;
    if (bytes.starts_with("\xef\xbb\xbf")) { result.push_back(u'\ufeff'); start = 3; }
    for (std::size_t i = start; i < bytes.size(); ++i) if (bytes[i] != '\r') result.push_back(static_cast<unsigned char>(bytes[i]));
    return result;
}
std::u16string normalized(std::u16string value) {
    std::erase(value, u'\r');
    return value;
}

void verify_about() {
    for (const auto* appearance : {&lcars::web_appearance(), &lcars::material_appearance()}) {
        auto info = settings_demo::desktop_about_info(*appearance);
        settings_demo::Demo demo(info);
        auto desktop = lcars::Desktop::create(demo.ui, *appearance, [&] { return demo.page(); }, demo.scroll);
        test::check(bool(desktop.desktop), "desktop created");
        auto caps = desktop.desktop->capabilities();
        demo.set_renderer(caps.version + u" — " + caps.renderer);
        demo.nav.select({"about"});
        auto& tree = demo.ui.semantics();
        for (const char* id : {"about.app", "about.framework", "about.build", "about.appearance", "about.renderer", "about.diagnostics", "about.settings_path"}) {
            auto node = tree.get({id});
            test::check(node && !node->value.empty(), "About label is populated");
        }
        test::check(tree.get({"about.framework"})->value.find(lcars::framework_version) != std::u16string::npos, "framework version");
        test::check(tree.get({"about.renderer"})->value == u"繪製：" + caps.version + u" — " + caps.renderer, "renderer capabilities");
        auto notices = lcars::third_party_notices();
        test::check(info.licenses.size() == 6 && notices.size() == 4, "six license entries");
        test::check(normalized(info.licenses[0].text) == license_file(LCARS_SOURCE_DIR "/LICENSE"), "framework license matches source");
        test::check(normalized(info.licenses[5].text) == license_file(LCARS_SOURCE_DIR "/examples/settings_demo/about_licenses/nlohmann-json-3.12.0.LICENSE.MIT"), "JSON license matches source");
        for (std::size_t i = 0; i < info.licenses.size(); ++i) {
            test::check(bool(demo.licenses.select(info.licenses[i].id)), "select license");
            auto text = tree.get({"about.license_text"});
            test::check(text && text->value == info.licenses[i].text, "selected full text");
            if (i >= 1 && i <= 4) test::check(text->value == notices[i-1].text, "framework notice text");
        }
    }
}
int main() { const test::Case cases[] = {{"AB01-AB02 desktop", verify_about}}; return test::run("About desktop", cases); }
