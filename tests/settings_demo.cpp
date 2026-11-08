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
struct TextProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<std::u16string> drawn;
    void role(detail::PaintRole) override {}
    void fill(Rect, Color) override {}
    void rounded(Rect, int, Color) override {}
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color) override { drawn.emplace_back(value); }
};
// Sweep the actual newer demo placements using the type's declared state applicability.
struct DarkProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    detail::PaintRole current = detail::PaintRole::content;
    std::vector<std::pair<detail::PaintRole, Color>> colors;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect, Color c) override { colors.emplace_back(current, c); }
    void rounded(Rect, int, Color c) override { colors.emplace_back(current, c); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color c) override { colors.emplace_back(current, c); }
    bool has(Color c, detail::PaintRole role = detail::PaintRole::content) const {
        return std::any_of(colors.begin(), colors.end(), [&](const auto& entry) {
            return entry.second.r == c.r && entry.second.g == c.g && entry.second.b == c.b &&
                (role == detail::PaintRole::content || (unsigned(entry.first) & unsigned(role)) != 0);
        });
    }
};
void dark_new_component_sweep() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo;
        auto settings = demo.ui.presentation(); settings.variant = ThemeVariant::dark();
        test::check(bool(demo.ui.set_presentation(settings)), "dark presentation accepted");
        test::check(bool(demo.nav.select({"items"})), "open items page");
        auto& state = detail::access(demo.ui);
        test::FakeMeasure measure; ScrollState scroll;
        auto palette = appearance->palette(ThemeVariant::dark());
        std::vector<std::string> visited;
        auto sweep = [&] {
            auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
            auto placements = frame.items;
            placements.insert(placements.end(), frame.overlays.begin(), frame.overlays.end());
            for (const auto& item : placements) {
                if (item.tooltip) continue;
                auto semantic = state.describe(item.id);
                if (!semantic || !semantic->visible) continue;
                visited.push_back(item.id);
                const auto& type = detail::component_type(semantic->kind);
                auto probe = [&](const Semantics& value) {
                    DarkProbe paint(state, item, value, *appearance, 1.f, true);
                    type.paint(paint); return paint;
                };
                if (type.supports_state(detail::ComponentState::disabled)) {
                    auto value = *semantic; value.enabled = false; value.focused = false;
                    auto paint = probe(value);
                    test::check(paint.has(palette.muted), ("dark disabled " + item.id).c_str());
                }
                if (type.can_focus()) {
                    auto value = *semantic; value.enabled = true; value.focused = true; value.invalid = false;
                    if (!value.options.empty()) { value.options[0].selected = true; value.candidate = value.options[0].value; }
                    auto paint = probe(value);
                    test::check(paint.has(palette.accent, detail::PaintRole::focus_indicator), ("dark focus " + item.id).c_str());
                }
                if (type.supports_state(detail::ComponentState::selected)) {
                    auto value = *semantic; value.enabled = true; value.focused = false;
                    if (!value.options.empty()) value.options[0].selected = true;
                    if (value.checked) value.checked = true;
                    auto paint = probe(value);
                    auto cue = value.kind == Kind::checkbox || value.kind == Kind::radio_group ? palette.accent : palette.selection;
                    test::check(paint.has(cue), ("dark selection " + item.id).c_str());
                }
                if (type.supports_state(detail::ComponentState::error)) {
                    auto value = *semantic; value.invalid = true;
                    auto paint = probe(value);
                    test::check(paint.has(palette.error), ("dark error " + item.id).c_str());
                }
            }
        };
        sweep();
        test::check(bool(demo.items_add.invoke()), "open item dialog");
        test::check(bool(demo.item_name.set_error(u"required")), "set dialog error");
        sweep();
        test::check(bool(demo.item_dialog.settle(false)), "close item dialog");
        test::check(bool(demo.items_list.select({"T001"})) && bool(demo.items_delete.invoke()), "open delete dialog");
        sweep();
        for (const char* id : {"app.nav", "items.search", "items.list", "items.dialog.name", "items.dialog.ok", "items.delete_dialog.confirm"})
            test::check(std::find(visited.begin(), visited.end(), id) != visited.end(), (std::string("dark sweep visited ") + id).c_str());
    }
}
void compact_new_component_layout() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo; test::FakeMeasure measure; ScrollState scroll;
        test::check(bool(demo.nav.select({"items"})), "open items for density sweep");
        auto frame = [&] { return arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll); };
        auto standard = frame();
        auto settings = demo.ui.presentation(); settings.density = Density::compact;
        test::check(bool(demo.ui.set_presentation(settings)), "compact accepted");
        auto compact = frame();
        auto* nav = compact.find(demo.nav);
        test::check(nav && nav->options[0].h >= 24 && nav->options[0].h < standard.find(demo.nav)->options[0].h, "compact navigation rows shrink and remain clickable");
        test::check(compact.find(demo.items_search)->bounds.h < standard.find(demo.items_search)->bounds.h, "compact items field shrinks");
        test::check(bool(demo.items_add.invoke()), "open compact item dialog");
        auto dialog = frame();
        test::check(dialog.modal && dialog.find(demo.item_name) && dialog.find(demo.item_ok), "compact item dialog is arranged");
        test::check(dialog.find(demo.item_ok)->bounds.h >= 24, "compact dialog action is clickable");
        test::check(bool(demo.item_dialog.settle(false)), "close compact item dialog");
        test::check(bool(demo.items_list.select({"T001"})) && bool(demo.items_delete.invoke()), "open compact delete dialog");
        auto deletion = frame();
        auto confirm = std::find_if(deletion.overlays.begin(), deletion.overlays.end(), [](const auto& p) { return p.id == "items.delete_dialog.confirm"; });
        test::check(deletion.modal && confirm != deletion.overlays.end() && confirm->bounds.h >= 24, "compact delete action is clickable");
    }
}
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
    auto navigation = demo.ui.semantics().get({"app.nav"});
    auto pages = demo.ui.semantics().children({"app.nav"});
    test::check(navigation && navigation->role == Role::list && pages && pages->size() == 4,
                "semantic navigation exposes the four demo pages");
    for (const auto& page : *pages) {
        auto node = demo.ui.semantics().get(page);
        test::check(node && node->role == Role::list_item && node->selected == (page.item->id == "general"),
                    "semantic page items identify the selected page");
    }
    auto items_probe = demo.ui.add_label({.id="probe.items", .kind=Kind::label}).value.value();
    auto appearance_probe = demo.ui.add_label({.id="probe.appearance", .kind=Kind::label}).value.value();
    auto about_probe = demo.ui.add_label({.id="probe.about", .kind=Kind::label}).value.value();
    test::check(bool(demo.nav.add_member({"items"}, items_probe)) && bool(demo.nav.add_member({"appearance"}, appearance_probe)) &&
                bool(demo.nav.add_member({"about"}, about_probe)), "attach page-specific semantic probes");
    for (const char* page : {"general", "items", "appearance", "about"}) {
        test::check(bool(demo.nav.select({page})), "semantic page test switches page");
        auto field = demo.ui.semantics().get({"general.name"});
        auto items_field = demo.ui.semantics().get({"items.search"});
        auto item = demo.ui.semantics().get({"app.nav", OptionValue{page}});
        test::check(field && field->hidden == (std::string_view(page) != "general") && item && item->selected,
                    "semantic tree reflects navigation and hidden general members");
        test::check(items_field && items_field->hidden == (std::string_view(page) != "items"),
                    "semantic tree hides the Items field outside its page");
        for (const auto& [id, member_page] : {std::pair{"probe.items", "items"}, std::pair{"probe.appearance", "appearance"}, std::pair{"probe.about", "about"}}) {
            auto member = demo.ui.semantics().get({id});
            test::check(member && member->hidden == (std::string_view(page) != member_page), "non-current page member is hidden");
        }
    }
    test::check(bool(demo.nav.select({"general"})), "restore general page");
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
    demo.nav.select({"items"});
    test::check(demo.items_list.describe()->options.size() == 20, "items page starts with twenty entries");
    demo.items_search.set_value(u"\u7bc4\u672c 0");
    test::check(demo.items_list.describe()->options.size()==7, "search matches seven numbered templates");
    demo.items_search.set_value(u"XYZ");
    test::check(demo.items_list.describe()->options.empty(), "unmatched search is empty");
    demo.items_search.set_value(u"\u5171\u7528");
    test::check(demo.items_list.describe()->options.size() == 2, "search filters duplicate names");
    demo.items_search.set_value(u"");
    test::check(bool(demo.items_add.invoke()), "open add item");
    test::check(bool(*demo.item_dialog.is_open()), "item dialog opens");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        test::FakeMeasure measure; ScrollState scroll;
        auto frame=arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
        test::check(frame.modal && frame.find(demo.item_name), "both appearances arrange the same item dialog");
    }
    test::check(!demo.item_ok.invoke() && *demo.item_dialog.is_open(), "empty name keeps dialog open");
    test::check(detail::access(demo.ui).focused()=="items.dialog.name" && !demo.item_name.describe()->error.empty(), "invalid name is focused with an error");
    demo.item_name.set_value(u"Workstation A");
    test::check(bool(demo.item_ok.invoke()), "valid item is added");
    test::check(!*demo.item_dialog.is_open() && demo.items_list.describe()->options.size()==21, "new item visible");
    test::check(bool(demo.items_list.select({"T006"})), "select item for edit");
    demo.items_list.focus();
    test::check(bool(detail::access(demo.ui).key_down({.key=Key::enter})) && *demo.item_dialog.is_open(), "Enter opens edit");
    test::check(demo.item_name.draft().value.value()==u"\u5171\u7528\u7bc4\u672c", "edit loads selected name");
    demo.item_name.set_value(u"Changed");
    test::check(bool(demo.item_ok.invoke()), "edit item");
    demo.items_list.focus();
    test::check(bool(detail::access(demo.ui).key_down({.key=Key::del})) && *demo.delete_dialog.is_open(), "Delete opens confirmation");
    detail::access(demo.ui).key_down({.key=Key::escape});
    test::check(!*demo.delete_dialog.is_open() && demo.items_list.describe()->options.size()==21, "Escape cancels deletion");
    detail::access(demo.ui).key_down({.key=Key::del});
    detail::access(demo.ui).key_down({.key=Key::tab});
    detail::access(demo.ui).key_down({.key=Key::space});
    detail::access(demo.ui).key_up(Key::space);
    test::check(!*demo.delete_dialog.is_open() && demo.items_list.describe()->options.size()==20, "keyboard confirms deletion");
    test::check(!demo.items_edit.describe()->enabled && !demo.items_delete.describe()->enabled, "actions disabled with no selection");
    demo.items_search.set_value(u"\u5171\u7528");
    test::check(bool(demo.items_add.invoke()), "add from filtered list");
    demo.item_name.set_value(u"Outside filter");
    test::check(bool(demo.item_ok.invoke()), "filtered add succeeds");
    test::check(demo.items_search.draft().value.value().empty() && demo.items_list.describe()->options.size()==21, "filtered add clears search and reveals item");
    demo.nav.select({"about"});
    test::check(demo.name.set_value(u"X").status == Status::hidden, "general page hidden");
}

void item_search_count_and_empty_state() {
    struct SearchCase { std::u16string query, count; std::vector<std::string> ids; };
    const std::vector<SearchCase> cases = {
        {u"\u5171\u7528", u"\u5171 20 \u7b46\uff0c\u986f\u793a 2 \u7b46", {"T005", "T006"}},
        {u"\u7bc4\u672c 0", u"\u5171 20 \u7b46\uff0c\u986f\u793a 7 \u7b46", {"T001", "T002", "T003", "T004", "T007", "T008", "T009"}},
        {u"XYZ", u"\u5171 20 \u7b46\uff0c\u986f\u793a 0 \u7b46", {}},
        {u"", u"\u5171 20 \u7b46\uff0c\u986f\u793a 20 \u7b46", {}}
    };
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo;
        test::check(bool(demo.nav.select({"items"})), "open items page");
        auto& state = detail::access(demo.ui);
        const auto status = state.describe("app.status")->value;
        for (const auto& c : cases) {
            std::vector<std::u16string> redraw_counts;
            state.on_invalidate([&] { redraw_counts.push_back(state.describe("items.count")->value); });
            test::check(bool(demo.items_search.set_value(c.query)), "search succeeds");
            state.on_invalidate({});
            test::check(!redraw_counts.empty() && std::all_of(redraw_counts.begin(), redraw_counts.end(), [&](const auto& value) { return value == c.count; }), "every requested redraw observes the updated count");
            auto list = demo.items_list.describe();
            test::check(bool(list) && list->options.size() == (c.query.empty() ? 20 : c.ids.size()), "search returns the expected number of items");
            for (std::size_t i = 0; i < c.ids.size(); ++i)
                test::check(list->options[i].value.id == c.ids[i], "search preserves the expected item identities");
            test::FakeMeasure measure; ScrollState scroll;
            auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
            auto count = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& item) { return item.id == "items.count"; });
            std::u16string rendered_count;
            if (count != frame.items.end()) for (const auto& line : count->lines) rendered_count += line.text;
            test::check(count != frame.items.end() && rendered_count == c.count, "item count text matches the search result");
            if (c.query == u"XYZ") {
                auto* empty = frame.find(demo.items_list);
                test::check(empty && empty->options.empty() && empty->lines.size() == 1 && empty->lines[0].text == u"\u6c92\u6709\u9805\u76ee", "empty search displays the empty-list message");
                TextProbe paint(state, *empty, *list, *appearance, 1, false);
                detail::component_type(Kind::list).paint(paint);
                test::check(std::find(paint.drawn.begin(), paint.drawn.end(), u"\u6c92\u6709\u9805\u76ee") != paint.drawn.end(), "empty-list message is painted");
            }
            test::check(state.describe("app.status")->value == status && !state.describe("app.undo")->enabled, "search does not mark the document changed");
        }
    }
}

int main() { const test::Case cases[] = {{"settings demo", exercise}, {"IT01 search count and empty state", item_search_count_and_empty_state}, {"semantic navigation pages", semantic_navigation_pages}, {"dark newer components", dark_new_component_sweep}, {"compact newer components", compact_new_component_layout}}; return test::run("settings demo", cases); }

