#include "settings_demo.hpp"
#include "lcars/semantics.hpp"
#include <algorithm>
#include "check.hpp"
#include "core_internal.hpp"
#include "fake_measure.hpp"
#include "component_type.hpp"
using namespace lcars;
struct RoleProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    detail::PaintRole current = detail::PaintRole::content;
    bool selected = false, focused = false;
    void role(detail::PaintRole value) override { current = value; }
    void record() { selected |= current == detail::PaintRole::selection; focused |= current == detail::PaintRole::focus_indicator; }
    void fill(Rect, Color) override { record(); }
    void rounded(Rect, int, Color) override { record(); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color) override { record(); }
};
void semantic_navigation_pages() {
    settings_demo::Demo demo;
    auto& tree = demo.ui.semantics();
    const char* pages[] = {"general", "items", "appearance", "about"};
    const char* members[] = {"general.name.label", "general.name", "general.name.help", "general.name.error",
                             "general.notices", "general.mode", "general.region", "general.volume"};
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        for (const char* page : pages) {
            test::check(bool(demo.nav.select({page})), "select settings page");
            test::FakeMeasure measure; ScrollState scroll;
            arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
            auto roots = tree.children({});
            test::check(bool(roots), "settings semantic roots available");
            auto has_root = [&](const char* id) {
                return std::any_of(roots->begin(), roots->end(), [&](const auto& ref) { return ref.id == id; });
            };
            auto nav = tree.get({"app.nav"});
            test::check(has_root("app.nav") && has_root("app.page_title") && nav && nav->role == Role::list,
                        "navigation and page title are enumerated");
            auto options = tree.children({"app.nav"});
            test::check(options && options->size() == 4, "all four pages are enumerated");
            for (const char* option : pages) {
                auto item = tree.get({"app.nav", OptionValue{option}});
                test::check(item && item->role == Role::list_item && item->selected == (std::string_view(page) == option),
                            "navigation item exposes selection");
            }
            for (const char* id : members) {
                auto node = tree.get({id});
                test::check(has_root(id) && node && node->hidden == (std::string_view(page) != "general"),
                            "general member is enumerated and hidden on other pages");
            }
        }
    }
}
void exercise() {
    settings_demo::Demo demo;
    test::check(demo.nav.selected()->id == "general", "general selected initially");
    test::check(demo.name.draft().value.value() == u"測試使用者", "initial name");
    test::check(demo.volume.draft().value.value() == 40, "initial volume");
    test::check(detail::access(demo.ui).focused() == "app.nav", "navigation initially focused");
    detail::access(demo.ui).key_down({.key=Key::tab});
    test::check(detail::access(demo.ui).focused() == "general.name", "tab enters general page");
    demo.volume.focus();
    detail::access(demo.ui).key_down({.key=Key::tab});
    test::check(detail::access(demo.ui).focused() == "app.nav", "tab wraps past disabled toolbar");
    detail::access(demo.ui).key_down({.key=Key::tab, .shift=true});
    test::check(detail::access(demo.ui).focused() == "general.volume", "shift tab wraps to last page field");
    demo.name.focus();
    demo.name.set_value(u"\u5de5\u4f5c\u7ad9 A");
    detail::access(demo.ui).key_down({.key=Key::tab});
    test::check(!demo.name.describe()->source_conflict, "confirmed name agrees with source");
    test::check(bool(demo.name.focus()), "name can be edited again");
    test::check(bool(demo.name.set_value(u"\u5de5\u4f5c\u7ad9 B")), "second edit succeeds");
    detail::access(demo.ui).key_down({.key=Key::tab});
    test::check(!demo.name.describe()->source_conflict, "second confirmation agrees with source");
    demo.volume.set_value(45);
    demo.volume.focus();
    detail::access(demo.ui).key_down({.key=Key::tab});
    test::check(detail::access(demo.ui).focused() == "app.undo", "editing enables undo in tab order");
    test::check(bool(demo.nav.select({"about"})), "navigate to about");
    for (const auto* appearance : {&web_appearance(), &material_appearance()})
    for (int size : {12, 16, 28})
    for (auto viewport : {std::pair{960,720}, std::pair{640,480}}) {
        test::FakeMeasure measure; ScrollState scroll;
        auto root = layout::row({layout::control(demo.nav, size), layout::column({})});
        auto frame = arrange(root, demo.ui, *appearance, measure, viewport.first, viewport.second, 1, scroll);
        auto* item = frame.find(demo.nav);
        test::check(item && item->options.size() == 4 && item->bounds.w >= 160, "four navigation rows retain natural width");
        demo.nav.focus();
        for (const char* page : {"general", "items", "appearance", "about"}) {
            demo.nav.select({page});
            auto semantic = demo.nav.describe();
            RoleProbe probe(detail::access(demo.ui), *item, *semantic, *appearance, 1, true);
            detail::component_type(Kind::navigation_list).paint(probe);
            test::check(probe.selected && probe.focused, "selected and focused rows carry draw roles");
        }
        for (std::size_t i=0; i<item->options.size(); ++i) {
            const auto& r = item->options[i];
            test::check(frame.option_at(*item, r.x+r.w/2, r.y+r.h/2) == i, "row center hits painted row");
            if (i) test::check(item->options[i-1].y+item->options[i-1].h <= r.y, "rows do not overlap");
        }
    }
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        test::FakeMeasure measure; ScrollState scroll;
        auto settings = demo.ui.presentation(); settings.text_size = 12;
        test::check(bool(demo.ui.set_presentation(settings)), "small presentation accepted");
        auto small = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
        auto small_nav = *small.find(demo.nav);
        settings.text_size = 28;
        test::check(bool(demo.ui.set_presentation(settings)), "large presentation accepted");
        for (const char* page : {"general", "items", "appearance", "about"}) {
            demo.nav.select({page});
            auto large = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
            auto* nav = large.find(demo.nav);
            auto title = std::find_if(large.items.begin(), large.items.end(), [](const auto& item) { return item.id == "app.page_title"; });
            test::check(nav && nav->text_size == 28 && nav->bounds.w > small_nav.bounds.w &&
                        nav->options[0].h > small_nav.options[0].h, "presentation reflows navigation");
            test::check(title != large.items.end() && title->text_size == 42, "presentation reflows demo page title");
            if (page == std::string_view("general"))
                test::check(large.find(demo.name) && large.find(demo.name)->text_size == 28, "presentation reflows demo fields");
        }
    }
    demo.nav.select({"about"});
    test::check(demo.name.set_value(u"X").status == Status::hidden, "general page hidden");
}

int main() { const test::Case cases[] = {{"settings demo", exercise}, {"semantic navigation pages", semantic_navigation_pages}}; return test::run("settings demo", cases); }
