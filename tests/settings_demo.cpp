#include "settings_demo.hpp"
#include "lcars/semantics.hpp"
#include "lcars/version.hpp"
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
void semantic_items_and_dialogs() {
    settings_demo::Demo demo; auto& tree = demo.ui.semantics();
    auto contains = [&](const char* dialog, const char* member) {
        auto children = tree.children({dialog});
        return children && std::any_of(children->begin(), children->end(), [&](const auto& ref) { return ref.id == member; });
    };
    test::check(contains("items.dialog", "items.dialog.name") && !contains("items.dialog", "items.delete_dialog.confirm") &&
                contains("items.delete_dialog", "items.delete_dialog.confirm") && !contains("items.delete_dialog", "items.dialog.name"),
                "semantic dialogs contain only their own members");
    test::check(bool(demo.nav.select({"items"})) && bool(demo.items_add.invoke()), "open Items editor");
    test::check(!tree.get({"items.dialog"})->hidden && !tree.get({"items.dialog.name"})->hidden &&
                tree.get({"items.delete_dialog"})->hidden && tree.get({"items.delete_dialog.confirm"})->hidden &&
                tree.get({"items.list"})->blocked_by_modal, "editor semantics expose modal and hidden peer dialog");
    test::check(bool(demo.item_dialog.settle(false)) && bool(demo.items_list.select({"T001"})) && bool(demo.items_delete.invoke()),
                "open Items delete confirmation");
    test::check(tree.get({"items.dialog"})->hidden && tree.get({"items.dialog.name"})->hidden &&
                !tree.get({"items.delete_dialog"})->hidden && !tree.get({"items.delete_dialog.confirm"})->hidden,
                "semantic visibility follows the active Items dialog");
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
            test::check(state.describe("app.status")->value == status, "search keeps status");
            test::check(!state.describe("app.undo")->enabled, "search does not create history");
        }
    }
}

int history_steps(settings_demo::Demo& demo) {
    auto& state=detail::access(demo.ui); int count=0;
    while (state.describe("app.undo")->enabled) {
        test::check(bool(state.invoke("app.undo")), "history step can be undone");
        test::check(++count <= 100, "history is bounded");
    }
    return count;
}

void hs01_step_matrix() {
    using Action=std::function<void(settings_demo::Demo&)>;
    struct Case { const char* name; Action setup, action; int steps; };
    auto none=[](settings_demo::Demo&) {};
    auto items=[](settings_demo::Demo& d) { test::check(bool(d.nav.select({"items"})), "show items"); };
    auto selected=[](settings_demo::Demo& d) { d.nav.select({"items"}); d.items_list.select({"T006"}); };
    const std::vector<Case> cases={
        {"name",none,[](auto& d){ d.name.focus(); d.name.set_value(u"Station"); d.nav.focus(); },1},
        {"notices",none,[](auto& d){ d.notices.toggle(); },1},
        {"mode",none,[](auto& d){ d.mode.select({"focus"}); },1},
        {"region",none,[](auto& d){ d.region.select({"south"}); },1},
        {"volume",none,[](auto& d){ d.volume.focus(); d.volume.set_value(45); d.volume.set_value(50); d.nav.focus(); },1},
        {"add",items,[](auto& d){ d.items_add.invoke(); d.item_name.set_value(u"Station"); d.item_ok.invoke(); },1},
        {"edit",selected,[](auto& d){ d.items_edit.invoke(); d.item_name.set_value(u"Station"); d.item_ok.invoke(); },1},
        {"delete",selected,[](auto& d){ d.items_delete.invoke(); detail::access(d.ui).invoke("items.delete_dialog.confirm"); },1},
        {"font",none,[](auto& d){ auto p=d.ui.presentation(); p.font_family=u"Consolas"; d.set_presentation(p); },1},
        {"text size",none,[](auto& d){ auto p=d.ui.presentation(); p.text_size=28; d.set_presentation(p); },1},
        {"dark",none,[](auto& d){ auto p=d.ui.presentation(); p.variant=ThemeVariant::dark(); d.set_presentation(p); },1},
        {"compact",none,[](auto& d){ auto p=d.ui.presentation(); p.density=Density::compact; d.set_presentation(p); },1},
        {"reset presentation",[](auto& d){ auto p=d.ui.presentation(); p.font_family=u"Consolas"; d.set_presentation(p); p.text_size=28; d.set_presentation(p); p.variant=ThemeVariant::dark(); d.set_presentation(p); p.density=Density::compact; d.set_presentation(p); },[](auto& d){ d.set_presentation(PresentationSettings{}); },5},
        {"revert",[](auto& d){ d.notices.toggle(); },[](auto& d){ detail::access(d.ui).invoke("app.revert"); },2}
    };
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        for (const auto& c : cases) {
            settings_demo::Demo d; c.setup(d); c.action(d);
            test::FakeMeasure measure; ScrollState scroll;
            arrange(d.page(),d.ui,*appearance,measure,960,720,1.f,scroll);
            test::check(history_steps(d)==c.steps, (std::string("HS01 step count: ")+c.name).c_str());
        }
    }
}

void hs01_non_history_matrix() {
    using Action=std::function<void(settings_demo::Demo&)>;
    struct Case { const char* name; Action action; };
    const std::vector<Case> cases={
        {"search",[](auto& d){ d.nav.select({"items"}); d.items_search.set_value(u"ABC"); }},
        {"selection",[](auto& d){ d.nav.select({"items"}); d.items_list.select({"T003"}); }},
        {"page",[](auto& d){ d.nav.select({"about"}); }},
        {"candidate",[](auto& d){ d.region.focus(); d.region.open(); detail::access(d.ui).key_down({.key=Key::down}); test::check(d.region.describe()->candidate == OptionValue{"central"} && *d.region.draft() == OptionValue{"north"}, "candidate moves without confirmation"); d.region.cancel(); }},
        {"save",[](auto& d){ test::check(bool(detail::access(d.ui).invoke("app.save")), "save baseline"); }}
    };
    for (const auto& c : cases) {
        settings_demo::Demo d; d.notices.toggle(); c.action(d);
        test::check(history_steps(d)==1, (std::string("HS01 no step: ")+c.name).c_str());
    }
    settings_demo::AboutInfo info; info.licenses={{{"lcars"},u"License",std::u16string(3000,u'A')}};
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo d(info); d.notices.toggle(); d.nav.select({"about"});
        test::FakeMeasure measure; ScrollState scroll;
        arrange(d.page(),d.ui,*appearance,measure,640,480,1.f,scroll);
        test::check(bool(d.ui.semantics().perform({"about.license_text"},Verb::focus)), "focus overflowing license");
        detail::access(d.ui).key_down({.key=Key::end});
        test::check(d.license_scroll.offset>0, "scroll moves viewport");
        test::check(history_steps(d)==1, "scroll does not add history");
    }
}

void it03_unchanged_edit_has_no_history() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.nav.select({"items"}); demo.items_list.select({"T006"});
    test::check(bool(demo.items_edit.invoke()), "open T006");
    demo.item_name.set_value(u"\u5171\u7528\u7bc4\u672c B");
    test::check(bool(demo.item_ok.invoke()), "rename T006");
    auto items=demo.items_list.describe()->options;
    test::check(items[4].label==u"\u5171\u7528\u7bc4\u672c" && items[5].label==u"\u5171\u7528\u7bc4\u672c B", "only T006 changed");
    test::check(bool(demo.items_edit.invoke()) && bool(demo.item_ok.invoke()), "confirm unchanged edit");
    test::check(bool(state.invoke("app.undo")), "undo changed edit");
    items=demo.items_list.describe()->options;
    test::check(items[4].label==u"\u5171\u7528\u7bc4\u672c" && items[5].label==u"\u5171\u7528\u7bc4\u672c", "one undo restores T006");
    test::check(!state.describe("app.undo")->enabled, "unchanged edit adds no history step");
}

void hs03_value_equality_and_saved_name() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    std::u16string title; demo.on_dirty_changed([&](bool value) { title = value ? u"*Settings Demo" : u"Settings Demo"; });
    demo.volume.focus(); demo.volume.set_value(45); demo.nav.focus();
    demo.volume.focus(); demo.volume.set_value(40); demo.nav.focus();
    test::check(title==u"Settings Demo" && !state.describe("app.save")->enabled, "returned volume equals baseline");
    test::check(bool(state.invoke("app.undo")) && *demo.volume.draft()==45, "undo second focus period");
    test::check(bool(state.invoke("app.undo")) && *demo.volume.draft()==40, "undo first focus period");
    test::check(!state.describe("app.undo")->enabled, "exactly two volume steps");
    demo.notices.toggle(); test::check(bool(state.invoke("app.save")), "save changed baseline");
    demo.name.focus(); demo.name.set_value(u"New name"); demo.nav.focus();
    test::check(title==u"*Settings Demo", "name change marks title dirty");
    test::check(bool(state.invoke("app.undo")), "undo name after save");
    test::check(title==u"Settings Demo" && !state.describe("app.save")->enabled, "undo to saved value clears title marker");
    test::check(state.describe("app.status")->value == u"\u6240\u6709\u4fee\u6539\u5df2\u5132\u5b58", "saved status restored");
}

void hs02_undo_redo_sequence() {
    settings_demo::Demo demo; auto& state = detail::access(demo.ui);
    test::check(bool(demo.notices.toggle()) && bool(demo.mode.select({"focus"})), "change notices and mode");
    test::check(bool(state.invoke("app.undo")), "undo mode");
    test::check(!*demo.notices.draft() && *demo.mode.draft() == OptionValue{"normal"}, "mode undone only");
    test::check(bool(state.invoke("app.undo")), "undo notices");
    test::check(*demo.notices.draft() && *demo.mode.draft() == OptionValue{"normal"}, "notices undone");
    test::check(bool(state.invoke("app.redo")), "redo notices");
    test::check(!*demo.notices.draft() && *demo.mode.draft() == OptionValue{"normal"}, "notices redone");
    test::check(bool(demo.region.select({"south"})), "change region");
    test::check(*demo.region.draft() == OptionValue{"south"} && !state.describe("app.redo")->enabled, "new region clears redo");
}

void history_delete_restore() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.nav.select({"items"}); demo.items_list.select({"T003"}); demo.items_delete.invoke();
    test::check(bool(state.invoke("items.delete_dialog.confirm")), "confirm deletion");
    test::check(demo.items_list.describe()->options.size()==19, "deleted item absent");
    test::check(bool(state.invoke("app.undo")), "undo deletion");
    auto list=demo.items_list.describe();
    test::check(list->options.size()==20 && list->options[2].value.id=="T003", "undo restores original identity and order");
}

void history_key_repeat() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.notices.toggle(); demo.volume.set_value(45); demo.nav.focus();
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "first undo");
    for (int i=0; i<10; ++i) state.key_down({.ctrl=true, .repeat=true, .letter=u'Z'});
    test::check(*demo.volume.draft()==40 && !*demo.notices.draft(), "repeated key does not undo another step");
}

void history_field_shortcuts() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.notices.toggle(); demo.name.focus(); demo.name.set_value(u"A");
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "field undo");
    test::check(*demo.notices.draft()==false, "field undo leaves document history alone");
    demo.nav.focus(); state.key_up(Key::other);
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "document undo");
    test::check(*demo.notices.draft(), "document undo restores previous setting");
}

void history_limit_and_branch() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    for (int i=0; i<101; ++i) test::check(bool(demo.notices.toggle()), "toggle step");
    for (int i=0; i<100; ++i) test::check(bool(state.invoke("app.undo")), "undo retained step");
    test::check(!state.describe("app.undo")->enabled && !*demo.notices.draft(), "oldest step is discarded");
    test::check(bool(state.invoke("app.redo")), "redo retained step");
    demo.volume.set_value(45);
    test::check(!state.describe("app.redo")->enabled, "new step clears redo");
}

void history_presentation() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    auto value=demo.ui.presentation(); value.text_size=28; value.variant=ThemeVariant::dark();
    test::check(bool(demo.set_presentation(value)), "presentation change succeeds");
    test::check(state.describe("app.save")->enabled, "presentation changes mark document dirty");
    test::check(bool(state.invoke("app.undo")), "undo presentation");
    test::check(demo.ui.presentation().text_size==16 && demo.ui.presentation().variant==ThemeVariant::light(), "undo applies presentation");
}

void history_items() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.nav.select({"items"}); demo.items_add.invoke(); demo.item_name.set_value(u"Station");
    test::check(bool(demo.item_ok.invoke()), "add item");
    test::check(demo.items_list.describe()->options.size()==21, "new item visible");
    test::check(bool(state.invoke("app.undo")), "undo item addition");
    test::check(demo.items_list.describe()->options.size()==20, "undo removes item");
    demo.items_add.invoke(); demo.item_name.set_value(u"Next"); demo.item_ok.invoke();
    test::check(demo.items_list.describe()->options.back().value.id=="T021", "undo restores next item id");
}

void history_revert_is_undoable() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.notices.toggle(); demo.volume.set_value(55);
    test::check(bool(state.invoke("app.revert")), "revert restores baseline");
    test::check(*demo.notices.draft() && *demo.volume.draft()==40, "revert restores all values");
    test::check(bool(state.invoke("app.undo")), "revert itself can be undone");
    test::check(!*demo.notices.draft() && *demo.volume.draft()==55, "undo restores changes");
}

void history_save_focused_name() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.name.focus(); demo.name.set_value(u"Station");
    test::check(bool(state.key_down({.ctrl=true, .letter=u'S'})), "save commits focused name");
    test::check(!state.describe("app.save")->enabled, "saved draft is clean");
    demo.nav.focus();
    test::check(!state.describe("app.save")->enabled, "blur after save remains clean");
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "name save keeps history");
    test::check(*demo.name.draft() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "undo returns old name");
}

void history_invalid_save() {
    settings_demo::Demo demo; auto& state=detail::access(demo.ui);
    demo.name.focus(); demo.name.set_value(u""); demo.nav.select({"about"});
    test::check(!state.key_down({.ctrl=true, .letter=u'S'}), "invalid save is refused");
    test::check(demo.nav.selected()->id == "general", "invalid save returns to general");
    test::check(demo.name.describe()->focused && !demo.name.describe()->error.empty(), "invalid field receives focus and error");
}

void history_invalid_draft() {
    settings_demo::Demo demo;
    auto& state=detail::access(demo.ui);
    demo.name.focus(); demo.name.set_value(u""); demo.nav.focus();
    test::check(!demo.name.describe()->error.empty(), "invalid draft retains error");
    test::check(state.describe("app.undo")->enabled, "invalid draft can be discarded with undo");
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "undo discards rejected draft");
    test::check(*demo.name.draft() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "discard restores document value");
    test::check(demo.name.describe()->error.empty(), "discard clears error");
}

void history_title_notification() {
    settings_demo::Demo demo;
    std::vector<bool> states;
    demo.on_dirty_changed([&](bool value) { states.push_back(value); });
    demo.name.focus(); demo.name.set_value(u"Station");
    test::check(!states.empty() && states.back(), "draft notifies dirty title");
    demo.name.undo();
    test::check(!states.back(), "discarding draft clears title marker");
}

void history_slider_group() {
    settings_demo::Demo demo;
    auto& state=detail::access(demo.ui);
    demo.volume.focus(); demo.volume.set_value(45); demo.volume.set_value(50); demo.volume.set_value(55);
    demo.nav.focus();
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "undo slider group");
    test::check(*demo.volume.draft() == 40, "slider focus period is one history step");
}

void history_draft_dirty() {
    settings_demo::Demo demo;
    auto& state = detail::access(demo.ui);
    demo.name.focus(); demo.name.set_value(u"New name");
    test::check(state.describe("app.save")->enabled, "draft enables save");
    test::check(state.describe("app.status")->value == u"\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539", "draft updates status");
}

void history_save_revert() {
    settings_demo::Demo demo;
    auto& state = detail::access(demo.ui);
    demo.notices.toggle();
    test::check(state.describe("app.save")->enabled, "changed document can be saved");
    test::check(bool(state.key_down({.ctrl=true, .letter=u'S'})), "save shortcut");
    test::check(!state.describe("app.save")->enabled, "save clears dirty state");
    state.key_up(Key::other);
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "undo after save");
    test::check(*demo.notices.draft(), "undo restores original value");
    test::check(state.describe("app.save")->enabled, "undo after save is dirty");
}

void history_toggle_undo() {
    settings_demo::Demo demo;
    auto& state = detail::access(demo.ui);
    test::check(bool(demo.notices.toggle()), "change notices");
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})), "undo shortcut");
    test::check(*demo.notices.draft(), "undo restores notices");
}
void about_page_content() {
    settings_demo::AboutInfo info;
    info.appearance = u"Minimal Web"; info.renderer = u"GL version — renderer";
    info.build = u"123456789abc、Release"; info.settings_path = u"C:\\settings.json";
    settings_demo::Demo demo(info);
    test::check(bool(demo.nav.select({"about"})), "open About");
    auto& tree = demo.ui.semantics();
    for (const char* id : {"about.app", "about.framework", "about.build", "about.appearance", "about.renderer", "about.diagnostics", "about.settings_path", "about.licenses", "about.license_text"}) {
        test::check(bool(tree.get({id})), (std::string("About component exists: ") + id).c_str());
    }
    for (const char* id : {"about.app", "about.framework", "about.build", "about.appearance", "about.renderer", "about.diagnostics", "about.settings_path"}) {
        auto node = tree.get({id});
        test::check(node && !node->value.empty(), (std::string("About value: ") + id).c_str());
    }
    test::check(tree.get({"about.renderer"})->value == u"繪製：GL version — renderer", "renderer reports supplied capabilities");
    test::check(tree.get({"about.framework"})->value.find(lcars::framework_version) != std::u16string::npos, "framework version is shown");
}
void about_licenses() {
    settings_demo::AboutInfo info;
    info.licenses = {{{"lcars"}, u"framework", u"framework license"}, {{"freetype"}, u"freetype", u"freetype license"}, {{"harfbuzz"}, u"harfbuzz", u"harfbuzz license"}, {{"khronos"}, u"khronos", u"khronos license"}, {{"json"}, u"json", u"json license"}};
    settings_demo::Demo demo(info);
    test::check(bool(demo.nav.select({"about"})), "open About");
    for (const auto& license : info.licenses) {
        test::check(bool(demo.licenses.select(license.id)), "select license");
        auto text = demo.ui.semantics().get({"about.license_text"});
        test::check(text && text->value == license.text, "selected license text is exposed");
    }
}
void about_keyboard_scroll() {
    settings_demo::AboutInfo info;
    info.licenses = {{{"lcars"}, u"framework", std::u16string(5000, u'x')}};
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo(info); test::FakeMeasure measure;
        demo.nav.select({"about"});
        auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto& state = detail::access(demo.ui);
        demo.licenses.focus();
        state.key_down({.key=Key::tab});
        test::check(state.focused() == "about.license_text", "overflowing license receives Tab focus");
        auto semantic = demo.ui.semantics().get({"about.license_text"});
        test::check(semantic && semantic->role == Role::label && semantic->focusable, "license remains semantic text while focusable");
        demo.licenses.focus();
        test::check(bool(demo.ui.semantics().perform({"about.license_text"}, Verb::focus)), "semantic focus reaches overflowing license");
        auto item = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& p) { return p.id == "about.license_text"; });
        test::check(item != frame.items.end(), "license has placement");
        auto description = state.describe("about.license_text");
        RoleProbe paint(state, *item, *description, *appearance, 1.f, true);
        detail::component_type(Kind::label).paint(paint);
        test::check(paint.focused, "focused license draws a focus indicator");
        state.key_down({.key=Key::down});
        test::check(demo.license_scroll.offset > 0, "Down scrolls license");
        state.key_down({.key=Key::end});
        frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto region = std::find_if(frame.regions.begin(), frame.regions.end(), [&](const auto& r) { return r.state == &demo.license_scroll; });
        test::check(region != frame.regions.end() && region->offset == region->maximum, "End reveals last line");
        state.key_down({.key=Key::home});
        test::check(demo.license_scroll.offset == 0, "Home reveals first line");
        settings_demo::AboutInfo short_info;
        short_info.licenses = {{{"lcars"}, u"framework", u"short text"}};
        settings_demo::Demo short_demo(short_info);
        short_demo.nav.select({"about"});
        arrange(short_demo.page(), short_demo.ui, *appearance, measure, 960, 720, 1.f, short_demo.scroll);
        auto short_label = short_demo.ui.semantics().get({"about.license_text"});
        test::check(short_label && !short_label->focusable, "fitting license stays out of Tab order");
    }
}
void appearance_size_history() {
    settings_demo::Demo demo;
    test::check(bool(demo.nav.select({"appearance"})), "open appearance page");
    auto& tree = demo.ui.semantics();
    test::check(bool(tree.get({"appearance.text_size"})), "text size control exists");
    test::check(bool(tree.perform({"appearance.text_size"}, Verb::focus)), "focus text size");
    test::check(bool(tree.perform({"appearance.text_size"}, Verb::set_value, 28)), "change text size");
    test::check(demo.ui.presentation().text_size == 28, "text size applies immediately");
    test::check(bool(tree.perform({"appearance.text_size"}, Verb::set_value, 26)), "second size in focus period");
    test::check(demo.ui.presentation().text_size == 26, "second size applies immediately");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Z'})), "undo appearance edit");
    test::check(demo.ui.presentation().text_size == 16, "undo restores presentation");
}
void appearance_toggle_reset() {
    settings_demo::Demo demo;
    demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    auto settings=demo.ui.presentation(); settings.font_family=u"Consolas"; settings.text_size=25;
    test::check(bool(demo.set_presentation(settings)), "change font and size");
    for (const char* id : {"appearance.dark", "appearance.compact"})
        test::check(bool(tree.perform({id}, Verb::toggle)), "toggle appearance setting");
    test::check(demo.ui.presentation().variant == ThemeVariant::dark() && demo.ui.presentation().density == Density::compact, "appearance toggles apply");
    test::check(bool(tree.perform({"appearance.reset"}, Verb::invoke)), "reset appearance");
    test::check(demo.ui.presentation() == PresentationSettings{}, "reset restores defaults");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true,.letter=u'Z'})), "undo reset");
    test::check(demo.ui.presentation().variant == ThemeVariant::dark() && demo.ui.presentation().density == Density::compact &&
                demo.ui.presentation().font_family == u"Consolas" && demo.ui.presentation().text_size == 25, "reset is one history step");
}
void appearance_font_history() {
    settings_demo::AboutInfo info;
    info.fonts = {{u"Consolas", u"Consolas"}};
    for (int i=0; i<149; ++i) { auto n=std::to_string(i); auto key=u"Font" + std::u16string(n.begin(),n.end()); info.fonts.push_back({key,key}); }
    settings_demo::Demo demo(info); demo.nav.select({"appearance"});
    auto& tree=demo.ui.semantics();
    auto options=tree.children({"appearance.font"});
    test::check(options && options->size()==151, "default and all 150 families remain semantic options");
    test::check(bool(tree.perform({"appearance.font", OptionValue{"family:Consolas"}}, Verb::select)), "select font");
    test::check(demo.ui.presentation().font_family == u"Consolas", "font applies");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true,.letter=u'Z'})), "undo font");
    test::check(demo.ui.presentation().font_family.empty(), "font undo restores default");
}
void appearance_missing_font() {
    settings_demo::Demo demo;
    auto settings=demo.ui.presentation(); settings.font_family=u"NoSuchFont";
    test::check(bool(demo.set_presentation(settings)), "restore unavailable font");
    demo.nav.select({"appearance"});
    auto node=demo.ui.semantics().get({"appearance.font", OptionValue{"family:NoSuchFont"}});
    test::check(node && node->selected && node->name == u"NoSuchFont\uff08\u672a\u5b89\u88dd\uff09", "unavailable font remains selected");
    auto status=demo.ui.semantics().get({"appearance.font.status"});
    test::check(status && status->value.find(u"NoSuchFont") != std::u16string::npos, "unavailable font is explained");
}
void appearance_interleaved_history() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree=demo.ui.semantics();
    tree.perform({"appearance.text_size"}, Verb::focus);
    tree.perform({"appearance.text_size"}, Verb::set_value, 20);
    tree.perform({"appearance.dark"}, Verb::toggle);
    tree.perform({"appearance.text_size"}, Verb::set_value, 22);
    auto& state=detail::access(demo.ui);
    state.key_down({.ctrl=true,.letter=u'Z'});
    test::check(demo.ui.presentation().text_size==20 && demo.ui.presentation().variant==ThemeVariant::dark(), "undo latest size independently");
    state.key_down({.ctrl=true,.letter=u'Z'});
    test::check(demo.ui.presentation().variant==ThemeVariant::light(), "undo intervening toggle independently");
}
int main() { const test::Case cases[] = {{"appearance interleaved history", appearance_interleaved_history}, {"appearance missing font", appearance_missing_font}, {"appearance font", appearance_font_history}, {"appearance toggle and reset", appearance_toggle_reset}, {"HS08 appearance size", appearance_size_history}, {"HS01 counted steps", hs01_step_matrix}, {"HS01 non-history", hs01_non_history_matrix}, {"IT03 no-op edit history", it03_unchanged_edit_has_no_history}, {"HS03 value and saved state", hs03_value_equality_and_saved_name}, {"HS02 full sequence", hs02_undo_redo_sequence}, {"history toggle undo", history_toggle_undo}, {"history save", history_save_revert}, {"history draft", history_draft_dirty}, {"history slider", history_slider_group}, {"history title", history_title_notification}, {"history invalid", history_invalid_draft}, {"history invalid save", history_invalid_save}, {"history save name", history_save_focused_name}, {"history revert", history_revert_is_undoable}, {"history items", history_items}, {"history presentation", history_presentation}, {"history limit", history_limit_and_branch}, {"history field", history_field_shortcuts}, {"history delete", history_delete_restore}, {"history repeat", history_key_repeat}, {"About keyboard scroll", about_keyboard_scroll}, {"About licenses", about_licenses}, {"About page content", about_page_content}, {"settings demo", exercise}, {"IT01 search count and empty state", item_search_count_and_empty_state}, {"semantic navigation pages", semantic_navigation_pages}, {"semantic Items dialogs", semantic_items_and_dialogs}, {"dark newer components", dark_new_component_sweep}, {"compact newer components", compact_new_component_layout}}; return test::run("settings demo", cases); }
