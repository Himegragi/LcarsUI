#include "settings_demo.hpp"
#include "failure_flow.hpp"
#include "failure_diagnostic.hpp"
#include "lcars/semantics.hpp"
#include "lcars/version.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
#include "check.hpp"
#include "core_internal.hpp"
#include "fake_measure.hpp"
#include "component_type.hpp"
#include "vertical.hpp"
#include "vertical_expectations.hpp"
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
        settings.focus_indicator = FocusIndicatorMode::always;  // indicators draw only after keys unless forced
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

// #78: accent choice on the appearance page, backed by Ui palette overrides.
bool accent_has(const settings_demo::Demo& demo, const Appearance& appearance, const ThemeVariant& variant, Color expected_accent) {
    auto palette = demo.ui.effective_palette(appearance, variant);
    return palette && palette->accent == expected_accent;
}
void accent_choices_pass_contrast_everywhere() {
    const char* ids[] = {"default", "green", "orange"};
    for (const char* id : ids) {
        settings_demo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics();
        auto options = tree.children({"appearance.accent"});
        test::check(options && options->size() == 3, "accent dropdown has default, green and orange");
        test::check(bool(tree.perform({"appearance.accent", OptionValue{id}}, Verb::select)), "select accent");
        test::check(demo.accent_id() == id && tree.get({"appearance.accent", OptionValue{id}})->selected, "selection is reflected");
        for (const auto* appearance : {&web_appearance(), &material_appearance()})
            for (const auto& variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
                auto palette = demo.ui.effective_palette(*appearance, variant);
                test::check(palette && check_palette_contrast(*palette).empty(), (std::string("accent ") + id + " passes contrast for " + std::string(appearance->name()) + " " + variant.name).c_str());
                auto built_in = appearance->palette(variant); built_in.focus = built_in.focus_color();  // the effective palette has its ring color settled
                bool unchanged = palette && *palette == built_in;
                test::check(unchanged == (std::string(id) == "default"), "default leaves built-in colors, other choices change them");
            }
    }
}
void accent_choice_repaints_the_page_in_the_chosen_color() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics();
        test::check(bool(tree.perform({"appearance.dark"}, Verb::toggle)), "dark on so the checked box paints the accent");
        auto painted = [&] {
            test::FakeMeasure measure; ScrollState scroll; auto& state = detail::access(demo.ui);
            auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
            std::vector<std::pair<detail::PaintRole, Color>> colors;
            for (const auto& item : frame.items) {
                auto semantic = state.describe(item.id); if (!semantic || !semantic->visible) continue;
                DarkProbe paint(state, item, *semantic, *appearance, 1.f, true);
                detail::component_type(semantic->kind).paint(paint);
                colors.insert(colors.end(), paint.colors.begin(), paint.colors.end());
            }
            return colors;
        };
        auto has = [](const auto& colors, Color c) { return std::any_of(colors.begin(), colors.end(), [&](const auto& e) { return e.second == c; }); };
        auto builtin = appearance->palette(ThemeVariant::dark());
        test::check(has(painted(), builtin.accent), "built-in accent paints before any choice");
        test::check(bool(tree.perform({"appearance.accent", OptionValue{"green"}}, Verb::select)), "choose green");
        auto chosen = demo.ui.effective_palette(*appearance, ThemeVariant::dark());
        auto colors = painted();
        test::check(chosen && chosen->accent != builtin.accent && has(colors, chosen->accent) && !has(colors, builtin.accent), "next frame paints the green accent instead of the built-in one");
        test::check(bool(tree.perform({"appearance.accent", OptionValue{"default"}}, Verb::select)) && has(painted(), builtin.accent), "default restores the built-in accent");
    }
}
void accent_is_history_saved_state_and_reset() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(!demo.is_dirty(), "clean before the choice");
    test::check(bool(tree.perform({"appearance.accent", OptionValue{"orange"}}, Verb::select)) && demo.is_dirty() && demo.document().accent == "orange", "choice dirties the document");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Z'})) && demo.accent_id() == "default" && demo.ui.palette_overrides().empty(), "undo restores the default accent and clears the overrides");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Y'})) && demo.accent_id() == "orange" && !demo.ui.palette_overrides().empty(), "redo reapplies the accent");
    test::check(bool(tree.perform({"appearance.reset"}, Verb::invoke)) && demo.accent_id() == "default" && demo.ui.palette_overrides().empty(), "reset appearance also resets the accent");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Z'})) && demo.accent_id() == "orange", "reset is one history step");
}
void loading_a_document_applies_its_accent() {
    settings_demo::Demo demo; auto document = demo.document();
    test::check(document.accent == "default", "a new document uses the default accent");
    document.accent = "green"; demo.load_document(document);
    test::check(demo.accent_id() == "green" && accent_has(demo, web_appearance(), ThemeVariant::light(), Color{20, 110, 50}), "loaded accent overrides the palette");
    document.accent = "default"; demo.load_document(document);
    test::check(demo.ui.palette_overrides().empty(), "loading the default clears the overrides");
}


// #90: the focus ring controls on the appearance page, through the semantic layer (the same operations UIA performs).
const Color focus_green_light_web{20, 110, 50}, focus_orange_light_web{170, 70, 0};
void focus_ring_controls_are_on_the_appearance_page_with_their_defaults() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    auto mode = tree.get({"appearance.focus_mode"}); auto modes = tree.children({"appearance.focus_mode"});
    test::check(mode && mode->role == Role::dropdown && mode->name == u"焦點標示" && modes && modes->size() == 3, "the mode dropdown offers automatic, always and never");
    test::check(tree.get({"appearance.focus_mode", OptionValue{"automatic"}})->selected, "automatic is selected by default");
    auto width = tree.get({"appearance.focus_width"});
    test::check(width && width->role == Role::slider && width->name == u"焦點外框粗細" && width->numeric_value == 3 && width->range && width->range->minimum == 2 && width->range->maximum == 6, "the thickness slider is 2-6 and shows the appearance's 3 DIP");
    auto same = tree.get({"appearance.focus_radius_same"});
    test::check(same && same->role == Role::checkbox && same->checked == std::optional<bool>(true), "the radius is 'same as the component' by default");
    auto color = tree.get({"appearance.focus_color"});
    test::check(color && color->name == u"焦點外框顏色" && tree.children({"appearance.focus_color"})->size() == 3 && tree.get({"appearance.focus_color", OptionValue{"default"}})->selected, "the color dropdown offers default, green and orange");
    const auto& presentation = demo.ui.presentation();
    test::check(presentation.focus_indicator == FocusIndicatorMode::automatic && !presentation.focus_ring_width && !presentation.focus_ring_radius && demo.focus_color_id() == "default", "nothing is set by default: the settings are empty");
    auto order = detail::access(demo.ui).focus_order();
    auto at = [&](const char* id) { return std::find(order.begin(), order.end(), std::string(id)) - order.begin(); };
    test::check(at("appearance.accent") < at("appearance.focus_mode") && at("appearance.focus_mode") < at("appearance.focus_width") && at("appearance.focus_width") < at("appearance.focus_radius_same") &&
                at("appearance.focus_radius_same") < at("appearance.focus_color") && at("appearance.focus_color") < at("appearance.reset"), "they follow the existing controls in tab order, before the reset button");
}
void the_focus_ring_controls_apply_to_the_presentation() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select)) && demo.ui.presentation().focus_indicator == FocusIndicatorMode::always, "the mode dropdown sets always");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"never"}}, Verb::select)) && demo.ui.presentation().focus_indicator == FocusIndicatorMode::never, "and never");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"automatic"}}, Verb::select)) && demo.ui.presentation().focus_indicator == FocusIndicatorMode::automatic, "and automatic");
    test::check(bool(tree.perform({"appearance.focus_width"}, Verb::set_value, 5)) && demo.ui.presentation().focus_ring_width == 5 && tree.get({"appearance.focus_width"})->numeric_value == 5, "the slider sets the thickness");
    test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && demo.ui.presentation().focus_ring_radius.has_value() && *tree.get({"appearance.focus_radius_same"})->checked == false, "unchecking 'same as the component' sets a radius");
    test::check(bool(tree.perform({"appearance.focus_radius"}, Verb::set_value, 20)) && demo.ui.presentation().focus_ring_radius == 20, "and the radius slider sets it");
    test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && !demo.ui.presentation().focus_ring_radius, "checking it again clears the radius");
    test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && demo.ui.presentation().focus_ring_radius == 20, "and unchecking again brings the last radius back");
    test::check(!tree.perform({"appearance.focus_width"}, Verb::set_value, 7) && demo.ui.presentation().focus_ring_width == 5, "a thickness out of range is refused and changes nothing");
    // The next frame is laid out and painted for the new settings.
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(demo.page(), demo.ui, web_appearance(), measure, 960, 720, 1.f, scroll);
    test::check(frame.find(demo.focus_radius) != nullptr, "the radius slider is on the page while a radius is set");
    tree.perform({"appearance.focus_radius_same"}, Verb::toggle);
    frame = arrange(demo.page(), demo.ui, web_appearance(), measure, 960, 720, 1.f, scroll);
    test::check(frame.find(demo.focus_radius) == nullptr && frame.find(demo.focus_radius_same) != nullptr, "it is not on the page while the radius is 'same as the component'");
}
// Review item 9: a palette or presentation that cannot be applied is reported to the caller, never dropped. Off the UI thread the
// Ui refuses every call, which is the failure the demo has to surface.
void a_document_that_cannot_be_applied_reports_the_failure() {
    settings_demo::Demo demo; auto document = demo.document(); document.focus_color = "green";
    Result loaded; std::thread([&] { loaded = demo.load_document(document); }).join();
    test::check(!loaded && loaded.status == Status::wrong_thread, "load_document returns the failure instead of dropping it");
    test::check(demo.focus_color_id() == "default", "and the choice it could not apply is not recorded as applied");
    test::check(bool(demo.load_document(document)) && demo.focus_color_id() == "green", "on the UI thread the same document loads");
}
// Review item 3: unchecking 'same as the component' the first time starts from the appearance's own radius, so nothing drawn changes.
void unchecking_same_as_the_component_starts_from_the_appearances_own_radius() {
    for (const Appearance* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo; demo.nav.select({"appearance"}); demo.ui.bind_appearance(appearance);
        auto& tree = demo.ui.semantics();
        const auto where = std::string(appearance->name());
        test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && demo.ui.presentation().focus_ring_radius == appearance->radius(), (where + ": the first unchecking starts at the appearance's own radius").c_str());
        test::check(tree.get({"appearance.focus_radius"})->numeric_value == appearance->radius(), (where + ": and the slider shows it").c_str());
        tree.perform({"appearance.focus_radius"}, Verb::set_value, 11);
        tree.perform({"appearance.focus_radius_same"}, Verb::toggle); tree.perform({"appearance.focus_radius_same"}, Verb::toggle);
        test::check(demo.ui.presentation().focus_ring_radius == 11, (where + ": after that the last chosen value is kept").c_str());
    }
}
void the_indicator_off_disables_the_other_three_and_keeps_their_values() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    tree.perform({"appearance.focus_width"}, Verb::set_value, 5);
    tree.perform({"appearance.focus_radius_same"}, Verb::toggle); tree.perform({"appearance.focus_radius"}, Verb::set_value, 12);
    tree.perform({"appearance.focus_color", OptionValue{"green"}}, Verb::select);
    for (const char* id : {"appearance.focus_width", "appearance.focus_radius_same", "appearance.focus_radius", "appearance.focus_color"}) test::check(tree.get({id})->enabled, (std::string(id) + " is enabled while the indicator shows").c_str());
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"never"}}, Verb::select)), "choose never");
    for (const char* id : {"appearance.focus_width", "appearance.focus_radius_same", "appearance.focus_radius", "appearance.focus_color"}) test::check(!tree.get({id})->enabled, (std::string(id) + " is disabled with the indicator off").c_str());
    test::check(!tree.perform({"appearance.focus_width"}, Verb::set_value, 4) && demo.ui.presentation().focus_ring_width == 5, "a disabled control cannot be driven");
    test::check(tree.get({"appearance.focus_width"})->numeric_value == 5 && tree.get({"appearance.focus_radius"})->numeric_value == 12 && tree.get({"appearance.focus_color", OptionValue{"green"}})->selected &&
                *tree.get({"appearance.focus_radius_same"})->checked == false, "their values stay");
    test::check(demo.ui.presentation().focus_ring_width == 5 && demo.ui.presentation().focus_ring_radius == 12 && demo.focus_color_id() == "green", "so do the presentation settings and the palette");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select)), "choose always");
    for (const char* id : {"appearance.focus_width", "appearance.focus_radius_same", "appearance.focus_radius", "appearance.focus_color"}) test::check(tree.get({id})->enabled, (std::string(id) + " is back").c_str());
    test::check(tree.get({"appearance.focus_width"})->numeric_value == 5 && tree.get({"appearance.focus_radius"})->numeric_value == 12 && tree.get({"appearance.focus_color", OptionValue{"green"}})->selected, "with the values they had");
}
void ring_colors_pass_three_to_one_everywhere_and_follow_the_accent_by_default() {
    for (const char* id : {"default", "green", "orange"}) {
        settings_demo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics();
        test::check(bool(tree.perform({"appearance.focus_color", OptionValue{id}}, Verb::select)) && demo.focus_color_id() == id && tree.get({"appearance.focus_color", OptionValue{id}})->selected, "select the ring color");
        for (const auto* appearance : {&web_appearance(), &material_appearance()})
            for (const auto& variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
                auto palette = demo.ui.effective_palette(*appearance, variant);
                const std::string where = std::string(id) + " " + std::string(appearance->name()) + " " + variant.name;
                test::check(palette && check_palette_contrast(*palette).empty(), (where + " passes every contrast rule").c_str());
                for (auto surface : {palette->background, palette->panel, palette->surface})
                    test::check(contrast_ratio(palette->focus_color(), surface) >= 3.0, (where + " ring is 3:1 on every surface").c_str());
                test::check((palette->focus_color() == appearance->palette(variant).focus_color()) == (std::string(id) == "default"), (where + ": default leaves the ring on the accent, the others change it").c_str());
            }
    }
    settings_demo::Demo demo;
    test::check(bool(demo.set_accent("green")), "choose a green accent");
    auto palette = demo.ui.effective_palette(web_appearance(), ThemeVariant::light());
    test::check(palette->focus == focus_green_light_web && palette->focus == palette->accent, "with the default ring color the ring follows the accent");
    test::check(bool(demo.set_focus_color("orange")), "choose an orange ring");
    palette = demo.ui.effective_palette(web_appearance(), ThemeVariant::light());
    test::check(palette->focus == focus_orange_light_web && palette->accent == focus_green_light_web, "an own ring color is not dragged along by the accent");
    test::check(bool(demo.set_focus_color("default")), "back to the default ring color");
    palette = demo.ui.effective_palette(web_appearance(), ThemeVariant::light());
    test::check(palette->focus == focus_green_light_web, "and the ring follows the accent again");
}
void ring_colors_paint_the_focus_indicator() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics(); auto& state = detail::access(demo.ui);
        tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select);
        tree.perform({"appearance.focus_color", OptionValue{"orange"}}, Verb::select);
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
        test::check(bool(state.focus("appearance.reset")), "focus the reset button");
        auto placed = std::find_if(frame.items.begin(), frame.items.end(), [](const Placement& p) { return p.id == "appearance.reset"; });
        test::check(placed != frame.items.end(), "reset button is placed");
        auto semantic = state.describe("appearance.reset");
        DarkProbe paint(state, *placed, *semantic, *appearance, 1.f, true);
        detail::component_type(semantic->kind).paint(paint);
        const auto orange = demo.ui.effective_palette(*appearance, demo.ui.presentation().variant)->focus_color();
        test::check(paint.has(orange, detail::PaintRole::focus_indicator) && orange != appearance->palette(ThemeVariant::light()).accent, "the ring is painted in the chosen color");
    }
}
void the_focus_ring_settings_are_history_saved_state_and_reset() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(!demo.is_dirty(), "clean before the choices");
    test::check(bool(tree.perform({"appearance.focus_color", OptionValue{"green"}}, Verb::select)) && demo.is_dirty() && demo.document().focus_color == "green", "the color dirties the document and is part of it");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"never"}}, Verb::select)) && demo.document().presentation.focus_indicator == FocusIndicatorMode::never, "so does the mode");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Z'})) && demo.ui.presentation().focus_indicator == FocusIndicatorMode::automatic && tree.get({"appearance.focus_width"})->enabled, "undo restores the mode, and the controls follow");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Z'})) && demo.focus_color_id() == "default" && demo.ui.palette_overrides().empty(), "undo restores the ring color");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Y'})) && demo.focus_color_id() == "green" && bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Y'})) &&
                demo.ui.presentation().focus_indicator == FocusIndicatorMode::never && !tree.get({"appearance.focus_width"})->enabled, "redo reapplies both");
    tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select); tree.perform({"appearance.focus_width"}, Verb::set_value, 6);
    tree.perform({"appearance.focus_color", OptionValue{"orange"}}, Verb::select);
    test::check(bool(tree.perform({"appearance.reset"}, Verb::invoke)) && demo.ui.presentation() == PresentationSettings{} && demo.focus_color_id() == "default" && demo.accent_id() == "default" && demo.ui.palette_overrides().empty(), "reset appearance resets the focus settings and the ring color too");
    test::check(!demo.ui.presentation().focus_ring_width && *tree.get({"appearance.focus_radius_same"})->checked && tree.get({"appearance.focus_width"})->numeric_value == 3, "the controls show the defaults again");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true, .letter=u'Z'})) && demo.focus_color_id() == "orange" && demo.ui.presentation().focus_ring_width == 6, "reset is one history step");
}
// Review item 1: a slider drag is one undo step, and undoing while the slider keeps the focus ends that step for every slider
// (the two ring sliders used to be forgotten by dirty(false) and apply()).
void undo_while_a_ring_slider_keeps_the_focus_starts_a_new_history_step() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics(); auto& state = detail::access(demo.ui);
    tree.perform({"appearance.focus_width"}, Verb::focus);
    tree.perform({"appearance.focus_width"}, Verb::set_value, 5); tree.perform({"appearance.focus_width"}, Verb::set_value, 4);
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})) && !demo.ui.presentation().focus_ring_width, "one undo takes the whole drag of the thickness slider back");
    tree.perform({"appearance.focus_width"}, Verb::set_value, 6);
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})) && !demo.ui.presentation().focus_ring_width, "a change after that undo is a history step of its own");
    tree.perform({"appearance.focus_radius_same"}, Verb::toggle);
    tree.perform({"appearance.focus_radius"}, Verb::focus);
    tree.perform({"appearance.focus_radius"}, Verb::set_value, 10); tree.perform({"appearance.focus_radius"}, Verb::set_value, 12);
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})) && demo.ui.presentation().focus_ring_radius == 6, "one undo takes the whole drag of the radius slider back");
    tree.perform({"appearance.focus_radius"}, Verb::set_value, 20);
    test::check(bool(state.key_down({.ctrl=true, .letter=u'Z'})) && demo.ui.presentation().focus_ring_radius == 6, "and the radius change after that undo is a step of its own");
}
void loading_a_document_applies_the_focus_ring_settings() {
    settings_demo::Demo demo; auto document = demo.document();
    test::check(document.focus_color == "default" && document.presentation.focus_indicator == FocusIndicatorMode::automatic && !document.presentation.focus_ring_width && !document.presentation.focus_ring_radius, "a new document has the defaults");
    document.presentation.focus_indicator = FocusIndicatorMode::never; document.presentation.focus_ring_width = 4; document.presentation.focus_ring_radius = 9; document.focus_color = "orange";
    demo.load_document(document); demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(demo.ui.presentation().focus_indicator == FocusIndicatorMode::never && demo.ui.presentation().focus_ring_width == 4 && demo.ui.presentation().focus_ring_radius == 9, "the presentation settings are applied");
    test::check(demo.focus_color_id() == "orange" && demo.ui.effective_palette(web_appearance(), ThemeVariant::light())->focus == focus_orange_light_web, "the ring color is applied");
    test::check(tree.get({"appearance.focus_mode", OptionValue{"never"}})->selected && tree.get({"appearance.focus_width"})->numeric_value == 4 && tree.get({"appearance.focus_radius"})->numeric_value == 9 &&
                *tree.get({"appearance.focus_radius_same"})->checked == false && tree.get({"appearance.focus_color", OptionValue{"orange"}})->selected, "the controls show them");
    test::check(!tree.get({"appearance.focus_width"})->enabled, "and the indicator being off disables the three");
}

// #84: the appearance page's checkbox is the user's way to turn justified text off (WCAG 1.4.8).
void the_justify_left_checkbox_switches_the_presentation_setting() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    auto box = tree.get({"appearance.justify_left"});
    test::check(box && box->role == Role::checkbox && box->name == u"左右對齊文字改為靠左" && box->checked == std::optional<bool>(false), "the checkbox is there and off");
    auto order = detail::access(demo.ui).focus_order();
    auto at = [&](const char* id) { return std::find(order.begin(), order.end(), std::string(id)) - order.begin(); };
    test::check(at("appearance.compact") < at("appearance.justify_left") && at("appearance.justify_left") < at("appearance.accent"), "it follows the compact checkbox in tab order");
    test::check(!demo.ui.presentation().justify_as_left, "off by default");
    test::check(bool(tree.perform({"appearance.justify_left"}, Verb::toggle)) && demo.ui.presentation().justify_as_left, "toggling turns the presentation setting on");
    test::check(*tree.get({"appearance.justify_left"})->checked, "and the checkbox shows it");
    test::check(bool(detail::access(demo.ui).key_down({.ctrl=true,.letter=u'Z'})) && !demo.ui.presentation().justify_as_left, "it is one undo step");
    test::check(bool(tree.perform({"appearance.justify_left"}, Verb::toggle)) && bool(tree.perform({"appearance.reset"}, Verb::invoke)) && !demo.ui.presentation().justify_as_left, "reset appearance turns it off");
    // The License text is long English and stays left-aligned, whatever the setting.
    test::check(tree.get({"about.license_text"})->text_align == TextAlign::left, "the About license text stays left-aligned");
}
void accent_follows_the_existing_appearance_controls_in_tab_order() {
    settings_demo::Demo demo; demo.nav.select({"appearance"});
    auto order = detail::access(demo.ui).focus_order();
    auto at = [&](const char* id) { return std::find(order.begin(), order.end(), std::string(id)) - order.begin(); };
    test::check(at("appearance.text_size") < at("appearance.dark") && at("appearance.dark") < at("appearance.compact") && at("appearance.compact") < at("appearance.accent") && at("appearance.accent") < at("appearance.reset"), "accent dropdown comes after the existing controls so their keyboard order is unchanged");
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
    test::check(tree.get({"about.diagnostics"})->value == u"診斷：未選用", "diagnostics absent by default");
    info.diagnostics = true;
    settings_demo::Demo with_diagnostics(info);
    test::check(with_diagnostics.ui.semantics().get({"about.diagnostics"})->value == u"診斷：基本診斷（只存記憶體）", "diagnostics present when selected");
    test::check(tree.get({"about.framework"})->value.find(lcars::framework_version) != std::u16string::npos, "framework version is shown");
}
void about_vertical_label() {
    settings_demo::AboutInfo info;
    settings_demo::Demo demo(info);
    test::check(bool(demo.nav.select({"about"})), "open About");
    auto node = demo.ui.semantics().get({"about.vertical"});
    test::check(node && node->role == Role::label && node->writing_mode == WritingMode::vertical, "the About page has a vertical label");
    test::check(node->value == test::vertical::about_sample, "the vertical label carries the sample sentence");
    // It shows every kind of piece the rules cover: a sideways word, a bracket, combined digits, a comma and a period.
    std::vector<VerticalForm> forms;
    for (const auto& segment : detail::segment_vertical(node->value, 3)) forms.push_back(segment.form);
    for (auto form : {VerticalForm::sideways, VerticalForm::bracket, VerticalForm::combined, VerticalForm::point, VerticalForm::upright})
        test::check(std::find(forms.begin(), forms.end(), form) != forms.end(), "the sample covers every piece form");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        test::FakeMeasure measure;
        auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto item = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& p) { return p.id == "about.vertical"; });
        test::check(item != frame.items.end() && item->vertical && item->column_layout.columns.size() >= 2 && item->bounds.h > 0, "the page lays the label out in columns");
        auto semantic = demo.ui.semantics().get({"about.vertical"});
        test::check(semantic && semantic->bounds && semantic->name == test::vertical::about_sample &&
                    test::vertical::drawn_box(semantic->bounds->x, semantic->bounds->y, semantic->bounds->x + semantic->bounds->w, semantic->bounds->y + semantic->bounds->h,
                                              0, 0, 960, 720, 200) && *semantic->bounds == intersect(item->bounds, item->clip),
                    "the semantic node reports the text in reading order and the box that is drawn"); 
    }
}
void td01_failure_diagnostic() {
    auto reset = settings_demo::failure_diagnostic({lcars::RendererFailureKind::device_reset, 0x8253, u"Graphics reset"});
    test::check(reset.category == lcars::DiagnosticCategory::device_reset && reset.severity == lcars::DiagnosticSeverity::error &&
                reset.code == "renderer_failed.device_reset" && reset.native_code == 0x8253 && reset.native_message == "Graphics reset" &&
                reset.backend == u"OpenGL", "TD01 a device reset becomes a device_reset diagnostic");
    auto present = settings_demo::failure_diagnostic({lcars::RendererFailureKind::present_failed, 5, u"SwapBuffers 失敗 \U0001F600"});
    test::check(present.category == lcars::DiagnosticCategory::other && present.code == "renderer_failed.present_failed" &&
                present.native_message == "SwapBuffers \xE5\xA4\xB1\xE6\x95\x97 \xF0\x9F\x98\x80", "TD01 other failures keep kind and UTF-8 message");
    test::check(settings_demo::failure_diagnostic({lcars::RendererFailureKind::frame_error, 0, u""}).code == "renderer_failed.frame_error" &&
                settings_demo::failure_diagnostic({lcars::RendererFailureKind::resource_failed, 0, u""}).code == "renderer_failed.resource_failed",
                "TD01 every failure kind is named");
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
// #80: the licence text breathes (line spacing 1.4, 8 DIP between paragraphs) and the appearance preview is centered;
// the settings pages offer no controls for either (ADR 0014: code switches and demo switches stay apart).
void demo_text_flow() {
    settings_demo::AboutInfo info;
    info.licenses = {{{"lcars"}, u"framework", u"alpha\nbeta"}};
    settings_demo::Demo demo(info);
    auto& tree = demo.ui.semantics();
    test::check(tree.get({"about.license_text"})->line_spacing == 1.4 && tree.get({"about.license_text"})->paragraph_spacing == 8, "licence text: 1.4 spacing, 8 DIP paragraphs");
    test::check(tree.get({"about.license_text"})->text_align == TextAlign::left && tree.get({"appearance.preview"})->text_align == TextAlign::center, "the preview is centered, the licence text stays left");
    test::check(tree.get({"appearance.preview"})->line_spacing == 1.0 && tree.get({"appearance.preview"})->paragraph_spacing == 0, "the preview keeps the default spacing");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        test::FakeMeasure measure;
        demo.nav.select({"about"});
        auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto item = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& p) { return p.id == "about.license_text"; });
        test::check(item != frame.items.end() && item->lines.size() == 2, "the licence text has two paragraphs");
        // 16 px text: a 24 px line, a 34 px row (24 * 1.4 rounded), and 8 px after the paragraph.
        test::check(item->lines[1].bounds.y - item->lines[0].bounds.y == 34 + 8, "one row plus the paragraph gap between paragraphs");
        demo.nav.select({"appearance"});
        frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        item = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& p) { return p.id == "appearance.preview"; });
        test::check(item != frame.items.end() && !item->lines.empty() && item->lines[0].bounds.x > item->bounds.x &&
                    std::abs((item->lines[0].bounds.x - item->bounds.x) * 2 + item->lines[0].bounds.w - item->bounds.w) <= 1,
                    "the preview text is centered in its label");
    }
    auto top = tree.children({});
    test::check(top && std::none_of(top->begin(), top->end(), [](const NodeRef& ref) { return ref.id.find("align") != std::string::npos || ref.id.find("spacing") != std::string::npos; }),
                "the settings pages have no alignment or spacing options");
}
void failure_retry_flow() {
    struct Prompt : settings_demo::NativePrompt {
        int attempts = 0, saves = 0;
        bool retry(std::u16string_view) override { return ++attempts < 3; }
        settings_demo::SaveChoice save(bool) override { ++saves; return settings_demo::SaveChoice::discard; }
        void error(std::u16string_view) override {}
    } prompt;
    int rebuilds=0, closes=0;
    settings_demo::failure_flow(prompt, u"failed", [] { return true; }, [&] { ++rebuilds; return false; }, [] { return std::u16string{}; }, [&] { ++closes; });
    test::check(rebuilds==2 && closes==1 && prompt.saves==1, "retry only on choice then discard");
}
void failure_choices() {
    struct Prompt : settings_demo::NativePrompt {
        settings_demo::SaveChoice choice = settings_demo::SaveChoice::cancel;
        bool saw_blank=false; int errors=0;
        bool retry(std::u16string_view) override { return false; }
        settings_demo::SaveChoice save(bool blank) override { saw_blank=blank; return choice; }
        void error(std::u16string_view) override { ++errors; }
    } prompt;
    int writes=0;
    auto save=[&] { ++writes; return std::u16string(u"failed"); };
    test::check(!settings_demo::failure_close(prompt,true,true,save) && prompt.saw_blank && writes==0, "cancel returns to retry");
    prompt.choice=settings_demo::SaveChoice::save;
    test::check(!settings_demo::failure_close(prompt,true,false,save) && writes==1 && prompt.errors==1, "save failure returns to retry");
    prompt.choice=settings_demo::SaveChoice::discard;
    test::check(settings_demo::failure_close(prompt,true,false,save) && writes==1, "discard does not save");
}
void failure_draft_save() {
    settings_demo::Demo demo;
    settings_demo::SettingsDocument saved=demo.document();
    demo.on_save([&](const auto& document) { saved=document; return std::u16string{}; });
    test::check(bool(demo.name.set_value(u"Draft")), "prepare draft");
    detail::access(demo.ui).renderer_failed(true);
    test::check(demo.is_dirty() && demo.save_failed_renderer().empty() && saved.name==u"Draft", "failure saves valid draft");
    detail::access(demo.ui).renderer_failed(false);
    test::check(bool(demo.name.set_value(u"")), "prepare blank draft");
    detail::access(demo.ui).renderer_failed(true);
    test::check(demo.blank_name_draft() && demo.save_failed_renderer().empty() && saved.name==demo.document().name, "failure preserves committed name when draft blank");
}
void close_invalid_name() {
    settings_demo::Demo demo; int closed=0;
    demo.on_close([&] { ++closed; });
    test::check(bool(demo.name.focus()) && bool(demo.name.set_value(u"")), "empty name draft");
    test::check(!demo.request_close(), "invalid draft prompts");
    test::check(!demo.close_save.invoke() && closed==0 && demo.name.describe()->focused && !demo.name.describe()->error.empty(), "invalid draft remains open and focused");
}
void close_save_and_layout() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo;
        int writes=0, closes=0;
        demo.on_close([&] { ++closes; });
        demo.on_save([&](const settings_demo::SettingsDocument& document) { ++writes; test::check(document.volume==65, "saved changed volume"); return std::u16string{}; });
        test::check(bool(demo.volume.set_value(65)), "change before save");
        test::check(!demo.request_close(), "prompt before save");
        test::FakeMeasure measure; ScrollState scroll;
        auto frame=arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
        auto has=[&](std::string_view id) { return std::any_of(frame.overlays.begin(), frame.overlays.end(), [&](const auto& item) { return item.id==id; }); };
        test::check(has("app.close_dialog.save") && !has("items.dialog.ok"), "MD04 active close dialog only");
        test::check(bool(demo.close_save.invoke()) && writes==1 && closes==1, "save closes after successful write");
    }
    settings_demo::Demo failed; int closed=0;
    failed.on_close([&] { ++closed; }); failed.on_save([](const auto&) { return std::u16string(u"disk full"); });
    test::check(bool(failed.volume.set_value(65)) && !failed.request_close(), "prepare failed save");
    test::check(!failed.close_save.invoke() && closed==0 && failed.is_dirty(), "failed save keeps changes and window");
    test::check(failed.ui.semantics().get({"app.error_dialog.ok"})->focused, "error dialog available");
}
void close_item_draft() {
    settings_demo::Demo demo;
    test::check(bool(demo.volume.set_value(65)), "dirty document");
    test::check(bool(demo.nav.select({"items"})), "items page");
    auto before=demo.document();
    test::check(bool(demo.items_add.invoke()), "open item dialog");
    test::check(bool(demo.item_name.set_value(u"discard me")), "type item draft");
    test::check(!demo.request_close(), "dirty close vetoed");
    test::check(!demo.item_dialog.is_open().value.value() && demo.close_dialog.is_open().value.value(), "item canceled before close prompt");
    test::check(demo.document()==before, "item draft discarded");
    test::check(!demo.request_close() && !demo.close_dialog.is_open().value.value(), "second close cancels close dialog");
}
void close_dialog_flow() {
    settings_demo::Demo demo;
    int closed = 0; demo.on_close([&] { ++closed; });
    test::check(bool(demo.volume.set_value(65)), "dirty volume");
    test::check(!demo.request_close(), "dirty close is vetoed");
    test::check(demo.close_dialog.is_open().value == true, "close dialog opened");
    test::check(demo.ui.semantics().get({"app.close_dialog.title"})->value == u"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f", "close title text");
    test::check(demo.ui.semantics().get({"app.close_dialog.save"})->name == u"\u5132\u5b58", "close save label");
    test::check(demo.ui.semantics().get({"app.close_dialog.discard"})->name == u"\u4e0d\u5132\u5b58", "close discard label");
    test::check(demo.ui.semantics().get({"app.close_dialog.cancel"})->name == u"\u53d6\u6d88", "close cancel label");
    test::check(bool(demo.close_cancel.invoke()), "cancel close");
    test::check(!demo.close_dialog.is_open().value.value() && closed == 0, "cancel keeps application open");
    test::check(!demo.request_close(), "second request vetoed");
    test::check(bool(demo.close_discard.invoke()) && closed == 1, "discard closes");
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
// #79: the item dialog's note is a three-line multi-line field whose breaks survive saving and editing.
void item_note_is_a_multiline_field() {
    settings_demo::Demo demo; demo.nav.select({"items"});
    test::check(bool(demo.items_add.invoke()), "open the add item dialog");
    auto note = demo.ui.semantics().get({"items.dialog.note"});
    test::check(note && note->role == Role::text && note->scroll && note->scroll->visible_lines == 3, "the note is a three-line multi-line text field");
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(demo.page(), demo.ui, web_appearance(), measure, 960, 720, 1, scroll);
    const auto* placed = frame.find(demo.item_note);
    test::check(placed && placed->multiline && placed->view.visible == 3 && placed->field.editor.h == 3 * measure.line_height(placed->text_size), "it is arranged three lines tall");
    test::check(demo.item_note.describe()->visible_lines == 3 && !demo.item_name.describe()->multiline, "the name beside it stays single-line");
    demo.item_note.focus();
    test::check(bool(detail::access(demo.ui).key_down({.key = Key::enter})) && *demo.item_dialog.is_open(), "Enter in the note breaks the line and the dialog stays open");
    test::check(demo.item_note.draft().value.value() == u"\n", "the break is in the draft");
    const std::u16string text = u"first line\nsecond line\n\nfourth";
    test::check(bool(demo.item_note.set_value(text)) && bool(demo.item_name.set_value(u"Multi")), "a note may hold line breaks");
    test::check(bool(demo.item_ok.invoke()) && !*demo.item_dialog.is_open(), "the item is saved");
    auto document = demo.document();
    test::check(document.items.back().name == u"Multi" && document.items.back().note == text, "the saved note keeps its line breaks");
    test::check(bool(demo.items_list.select(document.items.back().id)), "select it again");
    demo.items_list.focus();
    test::check(bool(detail::access(demo.ui).key_down({.key = Key::enter})) && *demo.item_dialog.is_open(), "Enter on the list still opens the edit dialog");
    test::check(demo.item_note.draft().value.value() == text, "editing loads the note with its breaks");
    test::check(demo.item_note.set_value(std::u16string(200, u'x') + u"\ny").status == Status::invalid_value, "the 200 character limit still counts a line break");
}
// #81: the item dialog's note is a rich text field with a style bar above it; the buttons and the color list act on
// the note's selection, the selection stays and the note takes the focus back; the styles are saved with the item.
void item_note_is_rich_with_a_style_bar() {
    settings_demo::Demo demo; demo.nav.select({"items"});
    test::check(bool(demo.items_add.invoke()), "open the add item dialog");
    auto& tree = demo.ui.semantics();
    test::check(tree.get({"items.dialog.note"})->rich && !tree.get({"items.dialog.name"})->rich, "the note is rich, the name is not");
    for (const char* id : {"items.dialog.note.bold", "items.dialog.note.italic", "items.dialog.note.underline"}) test::check(tree.get({id})->role == Role::button, "a style button above the note");
    test::check(tree.get({"items.dialog.note.color"})->role == Role::dropdown && demo.item_note_color.describe()->options.size() == 4, "a color list with default, red, green and blue");
    test::check(bool(demo.item_note.set_value(u"重要通知內容")) && bool(demo.item_name.set_value(u"Styled")), "write the note");
    auto& host = detail::access(demo.ui);
    demo.item_note.focus(); demo.item_note.select(0, 2);
    // A button press moves the focus to the button; invoking it applies the style and returns the focus.
    host.focus("items.dialog.note.bold");
    test::check(bool(demo.item_note_bold.invoke()), "press the bold button");
    auto spans = [&] { return demo.item_note.rich_value()->spans; };
    test::check(spans() == std::vector<StyleSpan>{{.start = 0, .length = 2, .bold = true}}, "the selection is bold");
    test::check(host.focused() == "items.dialog.note" && host.edit("items.dialog.note")->selection.anchor == 0 && host.edit("items.dialog.note")->selection.caret == 2,
                "the focus is back in the note and the selection is unchanged");
    host.focus("items.dialog.note.italic"); demo.item_note_italic.invoke();
    host.focus("items.dialog.note.underline"); demo.item_note_underline.invoke();
    test::check(spans() == std::vector<StyleSpan>{{.start = 0, .length = 2, .bold = true, .italic = true, .underline = true}}, "italic and underline combine with bold");
    host.focus("items.dialog.note.bold"); demo.item_note_bold.invoke();
    test::check(!spans().empty() && !spans()[0].bold && spans()[0].italic, "a second press removes bold");
    demo.item_note.select(2, 4);
    host.focus("items.dialog.note.color");
    test::check(bool(demo.item_note_color.select({"red"})), "choose red");
    test::check(spans().size() == 2 && spans()[1].start == 2 && spans()[1].length == 2 && spans()[1].color == "red" && host.focused() == "items.dialog.note",
                "the selection is red and the focus is back in the note");
    test::check(bool(demo.item_note_color.select({"default"})) && spans().size() == 1, "default returns the selection to the normal color");
    demo.item_note_color.select({"green"});
    test::check(bool(demo.item_ok.invoke()) && !*demo.item_dialog.is_open(), "the item is saved");
    auto document = demo.document();
    test::check(document.items.back().note == u"重要通知內容" && document.items.back().note_spans.size() == 2 && document.items.back().note_spans[1].color == "green",
                "the saved item has the note's text and styles");
    test::check(bool(demo.items_list.select(document.items.back().id)), "select it again");
    demo.items_list.focus();
    test::check(bool(host.key_down({.key = Key::enter})) && *demo.item_dialog.is_open(), "Enter on the list opens the edit dialog");
    test::check(demo.item_note.rich_value()->spans == document.items.back().note_spans, "editing loads the styles back");
    test::check(!demo.item_note_color.describe()->options.empty() && demo.item_note_color.draft()->has_value() && (*demo.item_note_color.draft())->id == "default", "the color list starts at default for each opening");
}
// The note's named colors stay readable (WCAG AA 4.5:1) on the field's surface in both appearances and both themes.
void note_colors_are_readable_in_every_appearance_and_theme() {
    settings_demo::Demo demo; demo.nav.select({"items"}); demo.items_add.invoke();
    auto colors = demo.item_note.colors();
    test::check(colors && colors->size() == 3, "red, green and blue");
    for (const auto* appearance : {&web_appearance(), &material_appearance()})
        for (const auto& variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
            if (!appearance->supports(variant)) continue;
            const auto palette = appearance->palette(variant);
            for (const auto& color : *colors) {
                const auto value = variant == ThemeVariant::dark() ? color.dark : color.light;
                test::check(contrast_ratio(value, palette.surface) >= 4.5, "a named note color is readable on the surface");
                test::check(contrast_ratio(value, palette.selection) >= 3.0, "...and still distinguishable inside a selection");
            }
        }
}
// #73: the pointer-to-value basis is frozen at press, so a layout that moves under the pointer
// (text size widens the navigation list) cannot make the value wander while the mouse is still.
void slider_drag_basis_is_frozen_when_layout_moves() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo; demo.nav.select({"appearance"});
        test::FakeMeasure measure; ScrollState scroll;
        auto& state = detail::access(demo.ui);
        auto frame = [&] { return arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll); };
        auto first = frame(); const auto* item = first.find(demo.text_size);
        test::check(item && item->slider_track.w > 0, "text size track is arranged");
        const int mouse_x = item->slider_track.x + item->slider_track.w * 9 / 10;
        test::check(bool(state.pointer_down(item->id)), "press text size");
        state.slider_pointer_down(item->id, mouse_x, item->slider_track);
        const int pressed = *demo.text_size.draft();
        std::vector<int> seen{pressed}; std::vector<int> track_x{item->slider_track.x};
        for (int i = 0; i < 6; ++i) {
            auto moved = frame(); const auto* now = moved.find(demo.text_size);
            track_x.push_back(now->slider_track.x);
            state.slider_pointer(now->id, mouse_x);
            seen.push_back(*demo.text_size.draft());
        }
        test::check(std::any_of(track_x.begin(), track_x.end(), [&](int x) { return x != track_x.front(); }), "the layout does move while dragging");
        for (int v : seen) test::check(v == pressed, "a still mouse keeps the value while the layout moves");
        state.pointer_up(item->id);
    }
}
// What the slider paints, through the same entry the host uses.
struct SliderProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<Rect> thumbs; std::vector<std::u16string> texts;  // thumbs: content-role discs only (the #72 focus dot is not the thumb)
    detail::PaintRole current = detail::PaintRole::content;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect, Color) override {}
    void rounded(Rect rect, int, Color) override { if (current != detail::PaintRole::focus_indicator) thumbs.push_back(rect); }
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color) override { texts.emplace_back(value); }
    int thumb_center() const { return thumbs.empty() ? -1 : thumbs.back().x + thumbs.back().w / 2; }
    bool titled(std::u16string_view title) const { return std::find(texts.begin(), texts.end(), std::u16string(title)) != texts.end(); }
};
SliderProbe paint_slider(settings_demo::Demo& demo, const Appearance& appearance, const lcars::SliderHandle& slider, Rect& track) {
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(demo.page(), demo.ui, appearance, measure, 960, 720, 1.f, scroll);
    const auto* item = frame.find(slider); test::check(item != nullptr, "slider is arranged");
    track = item->slider_track;
    auto semantic = detail::access(demo.ui).describe(item->id);
    SliderProbe probe(detail::access(demo.ui), *item, *semantic, appearance, 1.f, true);
    detail::component_type(Kind::slider).paint(probe);
    return probe;
}
bool near_px(int a, int b) { return std::abs(a - b) <= 1; }
// #73: while pressed, the thumb is under the pointer and the value is the snapped one.
void slider_thumb_follows_pointer_value_snaps() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        settings_demo::Demo demo; auto& state = detail::access(demo.ui);
        Rect track{};
        paint_slider(demo, *appearance, demo.volume, track);
        const int x = track.x + static_cast<int>(track.w * 0.47);
        test::check(bool(state.pointer_down("general.volume")) && bool(state.slider_pointer_down("general.volume", x, track)), "press volume at 47%");
        auto held = paint_slider(demo, *appearance, demo.volume, track);
        test::check(near_px(held.thumb_center(), x), "thumb is under the pointer, not on the 45 tick");
        test::check(held.titled(u"音量: 45"), "title shows the snapped value");
        test::check(*demo.volume.draft() == 45 && demo.ui.semantics().get({"general.volume"})->value == u"45", "semantic value is 45 while held");
        test::check(bool(state.slider_pointer("general.volume", x - 40)) && near_px(paint_slider(demo, *appearance, demo.volume, track).thumb_center(), x - 40), "thumb keeps following");
        state.slider_pointer("general.volume", x); state.pointer_up("general.volume");
        auto released = paint_slider(demo, *appearance, demo.volume, track);
        test::check(near_px(released.thumb_center(), track.x + static_cast<int>(std::lround(track.w * 0.45))), "release snaps the thumb to the 45 tick");
        test::check(!state.slider_drag_x("general.volume"), "no drag display position after release");
    }
}
void slider_thumb_stops_at_track_ends() {
    settings_demo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    paint_slider(demo, web_appearance(), demo.volume, track);
    state.pointer_down("general.volume"); state.slider_pointer_down("general.volume", track.x + 10, track);
    state.slider_pointer("general.volume", track.x - 80);
    auto left = paint_slider(demo, web_appearance(), demo.volume, track);
    test::check(*demo.volume.draft() == 0 && near_px(left.thumb_center(), track.x), "beyond the left end: minimum, thumb at the end");
    state.slider_pointer("general.volume", track.x + track.w + 80);
    auto right = paint_slider(demo, web_appearance(), demo.volume, track);
    test::check(*demo.volume.draft() == 100 && near_px(right.thumb_center(), track.x + track.w), "beyond the right end: maximum, thumb at the end");
    state.pointer_up("general.volume");
}
void slider_interrupted_drag_returns_to_tick() {
    settings_demo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    paint_slider(demo, web_appearance(), demo.volume, track);
    const int x = track.x + static_cast<int>(track.w * 0.47);
    state.pointer_down("general.volume"); state.slider_pointer_down("general.volume", x, track);
    state.cancel_input();
    auto after = paint_slider(demo, web_appearance(), demo.volume, track);
    test::check(*demo.volume.draft() == 45, "the last snapped value is kept");
    test::check(near_px(after.thumb_center(), track.x + static_cast<int>(std::lround(track.w * 0.45))), "an interrupted drag puts the thumb on the value's tick");
    test::check(!bool(state.slider_pointer("general.volume", x + 100)), "an interrupted drag takes no more pointer moves");
}
void slider_drag_is_one_write_per_value_and_one_undo() {
    settings_demo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    paint_slider(demo, web_appearance(), demo.volume, track);
    int writes = 0; std::vector<int> values;
    Ui ui; auto bare = ui.add_slider({.id = "v", .kind = Kind::slider, .label = u"V"}, {0, 100, 5}).value.value();
    int model = 40; bare.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    bare.connect_immediate([&](const NumericChange& c) { ++writes; values.push_back(c.value); model = c.value; return Result{}; });
    auto& bare_state = detail::access(ui); const Rect bare_track{100, 0, 200, 20};
    bare_state.pointer_down("v"); bare_state.slider_pointer_down("v", 100 + 94, bare_track);
    for (int x : {93, 94, 92, 91}) bare_state.slider_pointer("v", 100 + x);
    bare_state.pointer_up("v");
    test::check(writes == 1 && values == std::vector<int>{45}, "repeated moves inside one step write once");
    const int before = *demo.volume.draft();
    state.pointer_down("general.volume"); state.slider_pointer_down("general.volume", track.x, track);
    for (double r : {0.2, 0.5, 0.7, 0.9, 0.3}) state.slider_pointer("general.volume", track.x + static_cast<int>(track.w * r));
    state.slider_pointer("general.volume", track.x + static_cast<int>(track.w * 0.47)); state.pointer_up("general.volume");
    test::check(*demo.volume.draft() == 45, "drag ended on 45");
    test::check(bool(state.key_down({.ctrl = true, .letter = u'Z'})) && *demo.volume.draft() == before, "one undo restores the value before the whole drag");
}
// #75: Ctrl+Z / Ctrl+Y restore the value through the application, so the thumb lands at once,
// while the user's own arrow key on the same slider slides.
void slider_undo_redo_land_at_once() {
    settings_demo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    std::uint64_t now = 0; state.animation_time([&] { return now; });
    paint_slider(demo, web_appearance(), demo.volume, track);
    auto tick = [&](int value) { return track.x + static_cast<int>(std::lround(track.w * value / 100.0)); };
    auto thumb = [&] { return paint_slider(demo, web_appearance(), demo.volume, track).thumb_center(); };
    test::check(bool(demo.volume.focus()) && bool(state.key_down({.key = Key::right})), "arrow right on the focused volume");
    test::check(*demo.volume.draft() == 45 && near_px(thumb(), tick(40)), "the arrow key slides: 45 now, thumb still on 40");
    now = 150;
    test::check(near_px(thumb(), tick(45)) && !demo.ui.animating().value.value(), "the slide lands on 45");
    test::check(bool(state.key_down({.ctrl = true, .letter = u'Z'})) && *demo.volume.draft() == 40, "undo restores 40");
    test::check(near_px(thumb(), tick(40)) && !demo.ui.animating().value.value(), "undo: the thumb is on 40 at once");
    test::check(bool(state.key_down({.ctrl = true, .letter = u'Y'})) && *demo.volume.draft() == 45, "redo restores 45");
    test::check(near_px(thumb(), tick(45)) && !demo.ui.animating().value.value(), "redo: the thumb is on 45 at once");
}

// Right-clicking a row of the Items page: the row is selected, and the menu's Edit and Delete are the
// buttons' own actions, so they match the Enter / Del shortcuts exactly.
void items_context_menu_acts_on_the_clicked_row() {
    settings_demo::Demo demo;
    auto& core = detail::access(demo.ui);
    test::check(bool(demo.nav.select({"items"})), "open items page");
    auto row_of = [&](const char* id) {
        auto options = demo.items_list.describe()->options;
        auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.value.id == id; });
        return static_cast<std::size_t>(it - options.begin());
    };
    auto has_menu_verb = [&](const char* id) {
        auto verbs = demo.ui.semantics().get({id})->verbs;
        return std::find(verbs.begin(), verbs.end(), Verb::context_menu) != verbs.end();
    };
    test::check(has_menu_verb("items.search") && has_menu_verb("items.list") && !has_menu_verb("items.add"),
                "the search field has the built-in menu, the list has the demo's, buttons have none");
    test::check(bool(core.menu_pointer_open("items.list", std::nullopt, std::nullopt, {10, 10})), "right button on empty space opens the list menu");
    auto menu = demo.ui.context_menu();
    test::check(menu && menu->items.size() == 2 && menu->items[0].id == "items.edit" && menu->items[0].label == u"編輯(E)" && menu->items[0].shortcut_hint == u"Enter" &&
                menu->items[1].id == "items.delete" && menu->items[1].label == u"刪除(D)" && menu->items[1].shortcut_hint == u"Del" &&
                menu->items[0].access_key == u'E' && menu->items[1].access_key == u'D', "Edit(E) and Delete(D), with their shortcut hints");
    test::check(!menu->items[0].enabled && !menu->items[1].enabled, "both are off while nothing is selected, like the buttons");
    demo.ui.close_context_menu();
    // Delete the clicked row, not the previous selection.
    test::check(bool(demo.items_list.select({"T001"})), "a different row is selected first");
    test::check(bool(core.menu_pointer_open("items.list", row_of("T003"), std::nullopt, {10, 10})), "right button on row T003");
    test::check(demo.items_list.describe()->options[row_of("T003")].selected && !demo.items_list.describe()->options[row_of("T001")].selected, "the clicked row is selected");
    menu = demo.ui.context_menu();
    test::check(menu->items[0].enabled && menu->items[1].enabled, "Edit and Delete are on");
    test::check(bool(demo.ui.invoke_menu_item("items.delete")) && *demo.delete_dialog.is_open(), "Delete opens the confirmation");
    const auto by_menu = demo.ui.semantics().get({"items.delete_dialog.message"})->value;
    test::check(by_menu.find(u"範本 03") != std::u16string::npos, "it names the clicked row");
    core.key_down({.key = Key::escape});
    test::check(!*demo.delete_dialog.is_open() && core.focused() == "items.list", "cancelling returns focus to the list");
    test::check(bool(core.key_down({.key = Key::del})) && *demo.delete_dialog.is_open() && demo.ui.semantics().get({"items.delete_dialog.message"})->value == by_menu,
                "the Del key opens the very same confirmation");
    core.key_down({.key = Key::tab}); core.key_down({.key = Key::space}); core.key_up(Key::space);
    test::check(!*demo.delete_dialog.is_open() && row_of("T003") == demo.items_list.describe()->options.size() && demo.items_list.describe()->options.size() == 19,
                "confirming deletes row T003");
    // Edit through the menu with the keyboard: the menu key, then the quick key.
    test::check(bool(core.menu_pointer_open("items.list", row_of("T005"), std::nullopt, {10, 10})), "right button on row T005");
    core.text_input(u"e");
    test::check(*demo.item_dialog.is_open() && demo.item_name.draft().value.value() == u"共用範本", "the quick key E opens Edit on that row");
    demo.item_dialog.settle(false);
    // The menu key on the focused list hangs the menu from the current row.
    demo.items_list.focus();
    test::check(bool(core.key_down({.key = Key::menu})) && *demo.ui.context_menu_open(), "the menu key opens the list menu");
    core.key_down({.key = Key::down}); core.key_down({.key = Key::down});
    test::check(bool(core.key_down({.key = Key::enter})) && *demo.delete_dialog.is_open(), "Down, Down, Enter runs Delete(D)");
}

int main() { const test::Case cases[] = {{"items context menu", items_context_menu_acts_on_the_clicked_row}, {"focus ring controls are on the appearance page with their de", focus_ring_controls_are_on_the_appearance_page_with_their_defaults}, {"the focus ring controls apply to the presentation", the_focus_ring_controls_apply_to_the_presentation}, {"a document that cannot be applied reports it", a_document_that_cannot_be_applied_reports_the_failure}, {"unchecking same-as-component starts at the appearance radius", unchecking_same_as_the_component_starts_from_the_appearances_own_radius}, {"the indicator off disables the other three and keeps their v", the_indicator_off_disables_the_other_three_and_keeps_their_values}, {"ring colors pass three to one everywhere and follow the acce", ring_colors_pass_three_to_one_everywhere_and_follow_the_accent_by_default}, {"ring colors paint the focus indicator", ring_colors_paint_the_focus_indicator}, {"the focus ring settings are history saved state and reset", the_focus_ring_settings_are_history_saved_state_and_reset}, {"undo while a ring slider has focus starts a new step", undo_while_a_ring_slider_keeps_the_focus_starts_a_new_history_step}, {"loading a document applies the focus ring settings", loading_a_document_applies_the_focus_ring_settings}, {"slider undo redo land at once", slider_undo_redo_land_at_once}, {"item note multi-line", item_note_is_a_multiline_field}, {"item note rich", item_note_is_rich_with_a_style_bar}, {"note colors readable", note_colors_are_readable_in_every_appearance_and_theme}, {"slider thumb follows pointer", slider_thumb_follows_pointer_value_snaps}, {"slider thumb track ends", slider_thumb_stops_at_track_ends}, {"slider interrupted drag", slider_interrupted_drag_returns_to_tick}, {"slider one write and one undo", slider_drag_is_one_write_per_value_and_one_undo}, {"slider drag frozen basis", slider_drag_basis_is_frozen_when_layout_moves}, {"failure draft save", failure_draft_save}, {"failure choices", failure_choices}, {"close invalid name", close_invalid_name}, {"failure retry flow", failure_retry_flow}, {"close save and layout", close_save_and_layout}, {"close item draft", close_item_draft}, {"close dialog flow", close_dialog_flow}, {"appearance interleaved history", appearance_interleaved_history}, {"appearance missing font", appearance_missing_font}, {"appearance font", appearance_font_history}, {"appearance toggle and reset", appearance_toggle_reset}, {"HS08 appearance size", appearance_size_history}, {"HS01 counted steps", hs01_step_matrix}, {"HS01 non-history", hs01_non_history_matrix}, {"IT03 no-op edit history", it03_unchanged_edit_has_no_history}, {"HS03 value and saved state", hs03_value_equality_and_saved_name}, {"HS02 full sequence", hs02_undo_redo_sequence}, {"history toggle undo", history_toggle_undo}, {"history save", history_save_revert}, {"history draft", history_draft_dirty}, {"history slider", history_slider_group}, {"history title", history_title_notification}, {"history invalid", history_invalid_draft}, {"history invalid save", history_invalid_save}, {"history save name", history_save_focused_name}, {"history revert", history_revert_is_undoable}, {"history items", history_items}, {"history presentation", history_presentation}, {"history limit", history_limit_and_branch}, {"history field", history_field_shortcuts}, {"history delete", history_delete_restore}, {"history repeat", history_key_repeat}, {"About keyboard scroll", about_keyboard_scroll}, {"TD01 failure diagnostic", td01_failure_diagnostic}, {"About licenses", about_licenses}, {"demo text flow", demo_text_flow}, {"About page content", about_page_content}, {"About vertical label", about_vertical_label}, {"settings demo", exercise}, {"IT01 search count and empty state", item_search_count_and_empty_state}, {"semantic navigation pages", semantic_navigation_pages}, {"semantic Items dialogs", semantic_items_and_dialogs}, {"accent choices pass contrast", accent_choices_pass_contrast_everywhere}, {"accent repaints page", accent_choice_repaints_the_page_in_the_chosen_color}, {"accent history and reset", accent_is_history_saved_state_and_reset}, {"accent loaded from document", loading_a_document_applies_its_accent}, {"accent tab order", accent_follows_the_existing_appearance_controls_in_tab_order}, {"justify-left checkbox", the_justify_left_checkbox_switches_the_presentation_setting}, {"dark newer components", dark_new_component_sweep}, {"compact newer components", compact_new_component_layout}}; return test::run("settings demo", cases); }
