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
    void record() { selected |= current == detail::PaintRole::selection; focused |= current == detail::PaintRole::focusIndicator; }
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
void darkNewComponentSweep() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo;
        auto settings = demo.ui.presentation(); settings.variant = ThemeVariant::dark();
        settings.focusIndicator = FocusIndicatorMode::always;  // indicators draw only after keys unless forced
        test::check(bool(demo.ui.setPresentation(settings)), "dark presentation accepted");
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
                const auto& type = detail::componentType(semantic->kind);
                auto probe = [&](const ComponentSnapshot& value) {
                    DarkProbe paint(state, item, value, *appearance, 1.f, true);
                    type.paint(paint); return paint;
                };
                if (type.supportsState(detail::ComponentState::disabled)) {
                    auto value = *semantic; value.enabled = false; value.focused = false;
                    auto paint = probe(value);
                    test::check(paint.has(palette.muted), ("dark disabled " + item.id).c_str());
                }
                if (type.canFocus()) {
                    auto value = *semantic; value.enabled = true; value.focused = true; value.invalid = false;
                    if (!value.items.empty()) { value.items[0].selected = true; value.candidate = value.items[0].value; }
                    auto paint = probe(value);
                    test::check(paint.has(palette.accent, detail::PaintRole::focusIndicator), ("dark focus " + item.id).c_str());
                }
                if (type.supportsState(detail::ComponentState::selected)) {
                    auto value = *semantic; value.enabled = true; value.focused = false;
                    if (!value.items.empty()) value.items[0].selected = true;
                    if (value.checked) value.checked = true;
                    auto paint = probe(value);
                    auto cue = value.kind == ComponentKind::checkbox || value.kind == ComponentKind::radioGroup ? palette.accent : palette.selection;
                    test::check(paint.has(cue), ("dark selection " + item.id).c_str());
                }
                if (type.supportsState(detail::ComponentState::error)) {
                    auto value = *semantic; value.invalid = true;
                    auto paint = probe(value);
                    test::check(paint.has(palette.error), ("dark error " + item.id).c_str());
                }
            }
        };
        sweep();
        test::check(bool(demo.itemsAdd.invoke()), "open item dialog");
        test::check(bool(demo.itemName.setError(u"required")), "set dialog error");
        sweep();
        test::check(bool(demo.itemDialog.settle(false)), "close item dialog");
        test::check(bool(demo.itemsList.select({"T001"})) && bool(demo.itemsDelete.invoke()), "open delete dialog");
        sweep();
        for (const char* id : {"app.nav", "items.search", "items.list", "items.dialog.name", "items.dialog.ok", "items.delete_dialog.confirm"})
            test::check(std::find(visited.begin(), visited.end(), id) != visited.end(), (std::string("dark sweep visited ") + id).c_str());
    }
}
void compactNewComponentLayout() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo; test::FakeMeasure measure; ScrollState scroll;
        test::check(bool(demo.nav.select({"items"})), "open items for density sweep");
        auto frame = [&] { return arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll); };
        auto standard = frame();
        auto settings = demo.ui.presentation(); settings.density = Density::compact;
        test::check(bool(demo.ui.setPresentation(settings)), "compact accepted");
        auto compact = frame();
        auto* nav = compact.find(demo.nav);
        test::check(nav && nav->options[0].h >= 24 && nav->options[0].h < standard.find(demo.nav)->options[0].h, "compact navigation rows shrink and remain clickable");
        test::check(compact.find(demo.itemsSearch)->bounds.h < standard.find(demo.itemsSearch)->bounds.h, "compact items field shrinks");
        test::check(bool(demo.itemsAdd.invoke()), "open compact item dialog");
        auto dialog = frame();
        test::check(dialog.modal && dialog.find(demo.itemName) && dialog.find(demo.itemOk), "compact item dialog is arranged");
        test::check(dialog.find(demo.itemOk)->bounds.h >= 24, "compact dialog action is clickable");
        test::check(bool(demo.itemDialog.settle(false)), "close compact item dialog");
        test::check(bool(demo.itemsList.select({"T001"})) && bool(demo.itemsDelete.invoke()), "open compact delete dialog");
        auto deletion = frame();
        auto confirm = std::find_if(deletion.overlays.begin(), deletion.overlays.end(), [](const auto& p) { return p.id == "items.delete_dialog.confirm"; });
        test::check(deletion.modal && confirm != deletion.overlays.end() && confirm->bounds.h >= 24, "compact delete action is clickable");
    }
}
void semanticNavigationPages() {
    settingsDemo::Demo demo;
    auto& tree = demo.ui.semantics();
    const char* pages[] = {"general", "items", "appearance", "about"};
    const char* members[] = {"general.name.label", "general.name", "general.name.help", "general.name.error",
                             "general.notices", "general.mode", "general.region", "general.volume"};
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        for (const char* page : pages) {
            test::check(bool(demo.nav.select({page})), "select settings page");
            test::FakeMeasure measure; ScrollState scroll;
            arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
            auto roots = tree.children({});
            test::check(bool(roots), "settings semantic roots available");
            auto hasRoot = [&](const char* id) {
                return std::any_of(roots->begin(), roots->end(), [&](const auto& ref) { return ref.id == id; });
            };
            auto nav = tree.get({"app.nav"});
            test::check(hasRoot("app.nav") && hasRoot("app.page_title") && nav && nav->role == Role::list,
                        "navigation and page title are enumerated");
            auto options = tree.children({"app.nav"});
            test::check(options && options->size() == 4, "all four pages are enumerated");
            for (const char* option : pages) {
                auto item = tree.get({"app.nav", OptionValue{option}});
                test::check(item && item->role == Role::listItem && item->selected == (std::string_view(page) == option),
                            "navigation item exposes selection");
            }
            for (const char* id : members) {
                auto node = tree.get({id});
                test::check(hasRoot(id) && node && node->hidden == (std::string_view(page) != "general"),
                            "general member is enumerated and hidden on other pages");
            }
        }
    }
}
void semanticItemsAndDialogs() {
    settingsDemo::Demo demo; auto& tree = demo.ui.semantics();
    auto contains = [&](const char* dialog, const char* member) {
        auto children = tree.children({dialog});
        return children && std::any_of(children->begin(), children->end(), [&](const auto& ref) { return ref.id == member; });
    };
    test::check(contains("items.dialog", "items.dialog.name") && !contains("items.dialog", "items.delete_dialog.confirm") &&
                contains("items.delete_dialog", "items.delete_dialog.confirm") && !contains("items.delete_dialog", "items.dialog.name"),
                "semantic dialogs contain only their own members");
    test::check(bool(demo.nav.select({"items"})) && bool(demo.itemsAdd.invoke()), "open Items editor");
    test::check(!tree.get({"items.dialog"})->hidden && !tree.get({"items.dialog.name"})->hidden &&
                tree.get({"items.delete_dialog"})->hidden && tree.get({"items.delete_dialog.confirm"})->hidden &&
                tree.get({"items.list"})->blockedByModal, "editor semantics expose modal and hidden peer dialog");
    test::check(bool(demo.itemDialog.settle(false)) && bool(demo.itemsList.select({"T001"})) && bool(demo.itemsDelete.invoke()),
                "open Items delete confirmation");
    test::check(tree.get({"items.dialog"})->hidden && tree.get({"items.dialog.name"})->hidden &&
                !tree.get({"items.delete_dialog"})->hidden && !tree.get({"items.delete_dialog.confirm"})->hidden,
                "semantic visibility follows the active Items dialog");
}

// #78: accent choice on the appearance page, backed by Ui palette overrides.
bool accentHas(const settingsDemo::Demo& demo, const Appearance& appearance, const ThemeVariant& variant, Color expectedAccent) {
    auto palette = demo.ui.effectivePalette(appearance, variant);
    return palette && palette->accent == expectedAccent;
}
void accentChoicesPassContrastEverywhere() {
    const char* ids[] = {"default", "green", "orange"};
    for (const char* id : ids) {
        settingsDemo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics();
        auto options = tree.children({"appearance.accent"});
        test::check(options && options->size() == 3, "accent dropdown has default, green and orange");
        test::check(bool(tree.perform({"appearance.accent", OptionValue{id}}, Verb::select)), "select accent");
        test::check(demo.accentId() == id && tree.get({"appearance.accent", OptionValue{id}})->selected, "selection is reflected");
        for (const auto* appearance : {&webAppearance(), &materialAppearance()})
            for (const auto& variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
                auto palette = demo.ui.effectivePalette(*appearance, variant);
                test::check(palette && checkPaletteContrast(*palette).empty(), (std::string("accent ") + id + " passes contrast for " + std::string(appearance->name()) + " " + variant.name).c_str());
                auto builtIn = appearance->palette(variant); builtIn.focus = builtIn.focusColor();  // the effective palette has its ring color settled
                bool unchanged = palette && *palette == builtIn;
                test::check(unchanged == (std::string(id) == "default"), "default leaves built-in colors, other choices change them");
            }
    }
}
void accentChoiceRepaintsThePageInTheChosenColor() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics();
        test::check(bool(tree.perform({"appearance.dark"}, Verb::toggle)), "dark on so the checked box paints the accent");
        auto painted = [&] {
            test::FakeMeasure measure; ScrollState scroll; auto& state = detail::access(demo.ui);
            auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
            std::vector<std::pair<detail::PaintRole, Color>> colors;
            for (const auto& item : frame.items) {
                auto semantic = state.describe(item.id); if (!semantic || !semantic->visible) continue;
                DarkProbe paint(state, item, *semantic, *appearance, 1.f, true);
                detail::componentType(semantic->kind).paint(paint);
                colors.insert(colors.end(), paint.colors.begin(), paint.colors.end());
            }
            return colors;
        };
        auto has = [](const auto& colors, Color c) { return std::any_of(colors.begin(), colors.end(), [&](const auto& e) { return e.second == c; }); };
        auto builtin = appearance->palette(ThemeVariant::dark());
        test::check(has(painted(), builtin.accent), "built-in accent paints before any choice");
        test::check(bool(tree.perform({"appearance.accent", OptionValue{"green"}}, Verb::select)), "choose green");
        auto chosen = demo.ui.effectivePalette(*appearance, ThemeVariant::dark());
        auto colors = painted();
        test::check(chosen && chosen->accent != builtin.accent && has(colors, chosen->accent) && !has(colors, builtin.accent), "next frame paints the green accent instead of the built-in one");
        test::check(bool(tree.perform({"appearance.accent", OptionValue{"default"}}, Verb::select)) && has(painted(), builtin.accent), "default restores the built-in accent");
    }
}
void accentIsHistorySavedStateAndReset() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(!demo.isDirty(), "clean before the choice");
    test::check(bool(tree.perform({"appearance.accent", OptionValue{"orange"}}, Verb::select)) && demo.isDirty() && demo.document().accent == "orange", "choice dirties the document");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Z'})) && demo.accentId() == "default" && demo.ui.paletteOverrides().empty(), "undo restores the default accent and clears the overrides");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Y'})) && demo.accentId() == "orange" && !demo.ui.paletteOverrides().empty(), "redo reapplies the accent");
    test::check(bool(tree.perform({"appearance.reset"}, Verb::invoke)) && demo.accentId() == "default" && demo.ui.paletteOverrides().empty(), "reset appearance also resets the accent");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Z'})) && demo.accentId() == "orange", "reset is one history step");
}
void loadingADocumentAppliesItsAccent() {
    settingsDemo::Demo demo; auto document = demo.document();
    test::check(document.accent == "default", "a new document uses the default accent");
    document.accent = "green"; demo.loadDocument(document);
    test::check(demo.accentId() == "green" && accentHas(demo, webAppearance(), ThemeVariant::light(), Color{20, 110, 50}), "loaded accent overrides the palette");
    document.accent = "default"; demo.loadDocument(document);
    test::check(demo.ui.paletteOverrides().empty(), "loading the default clears the overrides");
}


// #90: the focus ring controls on the appearance page, through the semantic layer (the same operations UIA performs).
const Color focusGreenLightWeb{20, 110, 50}, focusOrangeLightWeb{170, 70, 0};
void focusRingControlsAreOnTheAppearancePageWithTheirDefaults() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    auto mode = tree.get({"appearance.focus_mode"}); auto modes = tree.children({"appearance.focus_mode"});
    test::check(mode && mode->role == Role::dropdown && mode->name == u"焦點標示" && modes && modes->size() == 3, "the mode dropdown offers automatic, always and never");
    test::check(tree.get({"appearance.focus_mode", OptionValue{"automatic"}})->selected, "automatic is selected by default");
    auto width = tree.get({"appearance.focus_width"});
    test::check(width && width->role == Role::slider && width->name == u"焦點外框粗細" && width->numericValue == 3 && width->range && width->range->minimum == 2 && width->range->maximum == 6, "the thickness slider is 2-6 and shows the appearance's 3 DIP");
    auto same = tree.get({"appearance.focus_radius_same"});
    test::check(same && same->role == Role::checkbox && same->checked == std::optional<bool>(true), "the radius is 'same as the component' by default");
    auto color = tree.get({"appearance.focus_color"});
    test::check(color && color->name == u"焦點外框顏色" && tree.children({"appearance.focus_color"})->size() == 3 && tree.get({"appearance.focus_color", OptionValue{"default"}})->selected, "the color dropdown offers default, green and orange");
    const auto& presentation = demo.ui.presentation();
    test::check(presentation.focusIndicator == FocusIndicatorMode::automatic && !presentation.focusRingWidth && !presentation.focusRingRadius && demo.focusColorId() == "default", "nothing is set by default: the settings are empty");
    auto order = detail::access(demo.ui).focusOrder();
    auto at = [&](const char* id) { return std::find(order.begin(), order.end(), std::string(id)) - order.begin(); };
    test::check(at("appearance.accent") < at("appearance.focus_mode") && at("appearance.focus_mode") < at("appearance.focus_width") && at("appearance.focus_width") < at("appearance.focus_radius_same") &&
                at("appearance.focus_radius_same") < at("appearance.focus_color") && at("appearance.focus_color") < at("appearance.reset"), "they follow the existing controls in tab order, before the reset button");
}
void theFocusRingControlsApplyToThePresentation() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select)) && demo.ui.presentation().focusIndicator == FocusIndicatorMode::always, "the mode dropdown sets always");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"never"}}, Verb::select)) && demo.ui.presentation().focusIndicator == FocusIndicatorMode::never, "and never");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"automatic"}}, Verb::select)) && demo.ui.presentation().focusIndicator == FocusIndicatorMode::automatic, "and automatic");
    test::check(bool(tree.perform({"appearance.focus_width"}, Verb::setValue, 5)) && demo.ui.presentation().focusRingWidth == 5 && tree.get({"appearance.focus_width"})->numericValue == 5, "the slider sets the thickness");
    test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && demo.ui.presentation().focusRingRadius.has_value() && *tree.get({"appearance.focus_radius_same"})->checked == false, "unchecking 'same as the component' sets a radius");
    test::check(bool(tree.perform({"appearance.focus_radius"}, Verb::setValue, 20)) && demo.ui.presentation().focusRingRadius == 20, "and the radius slider sets it");
    test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && !demo.ui.presentation().focusRingRadius, "checking it again clears the radius");
    test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && demo.ui.presentation().focusRingRadius == 20, "and unchecking again brings the last radius back");
    test::check(!tree.perform({"appearance.focus_width"}, Verb::setValue, 7) && demo.ui.presentation().focusRingWidth == 5, "a thickness out of range is refused and changes nothing");
    // The next frame is laid out and painted for the new settings.
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(demo.page(), demo.ui, webAppearance(), measure, 960, 720, 1.f, scroll);
    test::check(frame.find(demo.focusRadius) != nullptr, "the radius slider is on the page while a radius is set");
    tree.perform({"appearance.focus_radius_same"}, Verb::toggle);
    frame = arrange(demo.page(), demo.ui, webAppearance(), measure, 960, 720, 1.f, scroll);
    test::check(frame.find(demo.focusRadius) == nullptr && frame.find(demo.focusRadiusSame) != nullptr, "it is not on the page while the radius is 'same as the component'");
}
// Review item 9: a palette or presentation that cannot be applied is reported to the caller, never dropped. Off the UI thread the
// Ui refuses every call, which is the failure the demo has to surface.
void aDocumentThatCannotBeAppliedReportsTheFailure() {
    settingsDemo::Demo demo; auto document = demo.document(); document.focusColor = "green";
    Result loaded; std::thread([&] { loaded = demo.loadDocument(document); }).join();
    test::check(!loaded && loaded.status == Status::wrongThread, "load_document returns the failure instead of dropping it");
    test::check(demo.focusColorId() == "default", "and the choice it could not apply is not recorded as applied");
    test::check(bool(demo.loadDocument(document)) && demo.focusColorId() == "green", "on the UI thread the same document loads");
}
// Review item 3: unchecking 'same as the component' the first time starts from the appearance's own radius, so nothing drawn changes.
void uncheckingSameAsTheComponentStartsFromTheAppearancesOwnRadius() {
    for (const Appearance* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo; demo.nav.select({"appearance"}); demo.ui.bindAppearance(appearance);
        auto& tree = demo.ui.semantics();
        const auto where = std::string(appearance->name());
        test::check(bool(tree.perform({"appearance.focus_radius_same"}, Verb::toggle)) && demo.ui.presentation().focusRingRadius == appearance->radius(), (where + ": the first unchecking starts at the appearance's own radius").c_str());
        test::check(tree.get({"appearance.focus_radius"})->numericValue == appearance->radius(), (where + ": and the slider shows it").c_str());
        tree.perform({"appearance.focus_radius"}, Verb::setValue, 11);
        tree.perform({"appearance.focus_radius_same"}, Verb::toggle); tree.perform({"appearance.focus_radius_same"}, Verb::toggle);
        test::check(demo.ui.presentation().focusRingRadius == 11, (where + ": after that the last chosen value is kept").c_str());
    }
}
void theIndicatorOffDisablesTheOtherThreeAndKeepsTheirValues() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    tree.perform({"appearance.focus_width"}, Verb::setValue, 5);
    tree.perform({"appearance.focus_radius_same"}, Verb::toggle); tree.perform({"appearance.focus_radius"}, Verb::setValue, 12);
    tree.perform({"appearance.focus_color", OptionValue{"green"}}, Verb::select);
    for (const char* id : {"appearance.focus_width", "appearance.focus_radius_same", "appearance.focus_radius", "appearance.focus_color"}) test::check(tree.get({id})->enabled, (std::string(id) + " is enabled while the indicator shows").c_str());
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"never"}}, Verb::select)), "choose never");
    for (const char* id : {"appearance.focus_width", "appearance.focus_radius_same", "appearance.focus_radius", "appearance.focus_color"}) test::check(!tree.get({id})->enabled, (std::string(id) + " is disabled with the indicator off").c_str());
    test::check(!tree.perform({"appearance.focus_width"}, Verb::setValue, 4) && demo.ui.presentation().focusRingWidth == 5, "a disabled control cannot be driven");
    test::check(tree.get({"appearance.focus_width"})->numericValue == 5 && tree.get({"appearance.focus_radius"})->numericValue == 12 && tree.get({"appearance.focus_color", OptionValue{"green"}})->selected &&
                *tree.get({"appearance.focus_radius_same"})->checked == false, "their values stay");
    test::check(demo.ui.presentation().focusRingWidth == 5 && demo.ui.presentation().focusRingRadius == 12 && demo.focusColorId() == "green", "so do the presentation settings and the palette");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select)), "choose always");
    for (const char* id : {"appearance.focus_width", "appearance.focus_radius_same", "appearance.focus_radius", "appearance.focus_color"}) test::check(tree.get({id})->enabled, (std::string(id) + " is back").c_str());
    test::check(tree.get({"appearance.focus_width"})->numericValue == 5 && tree.get({"appearance.focus_radius"})->numericValue == 12 && tree.get({"appearance.focus_color", OptionValue{"green"}})->selected, "with the values they had");
}
void ringColorsPassThreeToOneEverywhereAndFollowTheAccentByDefault() {
    for (const char* id : {"default", "green", "orange"}) {
        settingsDemo::Demo demo; demo.nav.select({"appearance"});
        auto& tree = demo.ui.semantics();
        test::check(bool(tree.perform({"appearance.focus_color", OptionValue{id}}, Verb::select)) && demo.focusColorId() == id && tree.get({"appearance.focus_color", OptionValue{id}})->selected, "select the ring color");
        for (const auto* appearance : {&webAppearance(), &materialAppearance()})
            for (const auto& variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
                auto palette = demo.ui.effectivePalette(*appearance, variant);
                const std::string where = std::string(id) + " " + std::string(appearance->name()) + " " + variant.name;
                test::check(palette && checkPaletteContrast(*palette).empty(), (where + " passes every contrast rule").c_str());
                for (auto surface : {palette->background, palette->panel, palette->surface})
                    test::check(contrastRatio(palette->focusColor(), surface) >= 3.0, (where + " ring is 3:1 on every surface").c_str());
                test::check((palette->focusColor() == appearance->palette(variant).focusColor()) == (std::string(id) == "default"), (where + ": default leaves the ring on the accent, the others change it").c_str());
            }
    }
    settingsDemo::Demo demo;
    test::check(bool(demo.setAccent("green")), "choose a green accent");
    auto palette = demo.ui.effectivePalette(webAppearance(), ThemeVariant::light());
    test::check(palette->focus == focusGreenLightWeb && palette->focus == palette->accent, "with the default ring color the ring follows the accent");
    test::check(bool(demo.setFocusColor("orange")), "choose an orange ring");
    palette = demo.ui.effectivePalette(webAppearance(), ThemeVariant::light());
    test::check(palette->focus == focusOrangeLightWeb && palette->accent == focusGreenLightWeb, "an own ring color is not dragged along by the accent");
    test::check(bool(demo.setFocusColor("default")), "back to the default ring color");
    palette = demo.ui.effectivePalette(webAppearance(), ThemeVariant::light());
    test::check(palette->focus == focusGreenLightWeb, "and the ring follows the accent again");
}
void ringColorsPaintTheFocusIndicator() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo; demo.nav.select({"appearance"});
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
        detail::componentType(semantic->kind).paint(paint);
        const auto orange = demo.ui.effectivePalette(*appearance, demo.ui.presentation().variant)->focusColor();
        test::check(paint.has(orange, detail::PaintRole::focusIndicator) && orange != appearance->palette(ThemeVariant::light()).accent, "the ring is painted in the chosen color");
    }
}
void theFocusRingSettingsAreHistorySavedStateAndReset() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(!demo.isDirty(), "clean before the choices");
    test::check(bool(tree.perform({"appearance.focus_color", OptionValue{"green"}}, Verb::select)) && demo.isDirty() && demo.document().focusColor == "green", "the color dirties the document and is part of it");
    test::check(bool(tree.perform({"appearance.focus_mode", OptionValue{"never"}}, Verb::select)) && demo.document().presentation.focusIndicator == FocusIndicatorMode::never, "so does the mode");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Z'})) && demo.ui.presentation().focusIndicator == FocusIndicatorMode::automatic && tree.get({"appearance.focus_width"})->enabled, "undo restores the mode, and the controls follow");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Z'})) && demo.focusColorId() == "default" && demo.ui.paletteOverrides().empty(), "undo restores the ring color");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Y'})) && demo.focusColorId() == "green" && bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Y'})) &&
                demo.ui.presentation().focusIndicator == FocusIndicatorMode::never && !tree.get({"appearance.focus_width"})->enabled, "redo reapplies both");
    tree.perform({"appearance.focus_mode", OptionValue{"always"}}, Verb::select); tree.perform({"appearance.focus_width"}, Verb::setValue, 6);
    tree.perform({"appearance.focus_color", OptionValue{"orange"}}, Verb::select);
    test::check(bool(tree.perform({"appearance.reset"}, Verb::invoke)) && demo.ui.presentation() == PresentationSettings{} && demo.focusColorId() == "default" && demo.accentId() == "default" && demo.ui.paletteOverrides().empty(), "reset appearance resets the focus settings and the ring color too");
    test::check(!demo.ui.presentation().focusRingWidth && *tree.get({"appearance.focus_radius_same"})->checked && tree.get({"appearance.focus_width"})->numericValue == 3, "the controls show the defaults again");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Z'})) && demo.focusColorId() == "orange" && demo.ui.presentation().focusRingWidth == 6, "reset is one history step");
}
// Review item 1: a slider drag is one undo step, and undoing while the slider keeps the focus ends that step for every slider
// (the two ring sliders used to be forgotten by dirty(false) and apply()).
void undoWhileARingSliderKeepsTheFocusStartsANewHistoryStep() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics(); auto& state = detail::access(demo.ui);
    tree.perform({"appearance.focus_width"}, Verb::focus);
    tree.perform({"appearance.focus_width"}, Verb::setValue, 5); tree.perform({"appearance.focus_width"}, Verb::setValue, 4);
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})) && !demo.ui.presentation().focusRingWidth, "one undo takes the whole drag of the thickness slider back");
    tree.perform({"appearance.focus_width"}, Verb::setValue, 6);
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})) && !demo.ui.presentation().focusRingWidth, "a change after that undo is a history step of its own");
    tree.perform({"appearance.focus_radius_same"}, Verb::toggle);
    tree.perform({"appearance.focus_radius"}, Verb::focus);
    tree.perform({"appearance.focus_radius"}, Verb::setValue, 10); tree.perform({"appearance.focus_radius"}, Verb::setValue, 12);
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})) && demo.ui.presentation().focusRingRadius == 6, "one undo takes the whole drag of the radius slider back");
    tree.perform({"appearance.focus_radius"}, Verb::setValue, 20);
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})) && demo.ui.presentation().focusRingRadius == 6, "and the radius change after that undo is a step of its own");
}
void loadingADocumentAppliesTheFocusRingSettings() {
    settingsDemo::Demo demo; auto document = demo.document();
    test::check(document.focusColor == "default" && document.presentation.focusIndicator == FocusIndicatorMode::automatic && !document.presentation.focusRingWidth && !document.presentation.focusRingRadius, "a new document has the defaults");
    document.presentation.focusIndicator = FocusIndicatorMode::never; document.presentation.focusRingWidth = 4; document.presentation.focusRingRadius = 9; document.focusColor = "orange";
    demo.loadDocument(document); demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    test::check(demo.ui.presentation().focusIndicator == FocusIndicatorMode::never && demo.ui.presentation().focusRingWidth == 4 && demo.ui.presentation().focusRingRadius == 9, "the presentation settings are applied");
    test::check(demo.focusColorId() == "orange" && demo.ui.effectivePalette(webAppearance(), ThemeVariant::light())->focus == focusOrangeLightWeb, "the ring color is applied");
    test::check(tree.get({"appearance.focus_mode", OptionValue{"never"}})->selected && tree.get({"appearance.focus_width"})->numericValue == 4 && tree.get({"appearance.focus_radius"})->numericValue == 9 &&
                *tree.get({"appearance.focus_radius_same"})->checked == false && tree.get({"appearance.focus_color", OptionValue{"orange"}})->selected, "the controls show them");
    test::check(!tree.get({"appearance.focus_width"})->enabled, "and the indicator being off disables the three");
}

// #84: the appearance page's checkbox is the user's way to turn justified text off (WCAG 1.4.8).
void theJustifyLeftCheckboxSwitchesThePresentationSetting() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    auto box = tree.get({"appearance.justify_left"});
    test::check(box && box->role == Role::checkbox && box->name == u"左右對齊文字改為靠左" && box->checked == std::optional<bool>(false), "the checkbox is there and off");
    auto order = detail::access(demo.ui).focusOrder();
    auto at = [&](const char* id) { return std::find(order.begin(), order.end(), std::string(id)) - order.begin(); };
    test::check(at("appearance.compact") < at("appearance.justify_left") && at("appearance.justify_left") < at("appearance.accent"), "it follows the compact checkbox in tab order");
    test::check(!demo.ui.presentation().justifyAsLeft, "off by default");
    test::check(bool(tree.perform({"appearance.justify_left"}, Verb::toggle)) && demo.ui.presentation().justifyAsLeft, "toggling turns the presentation setting on");
    test::check(*tree.get({"appearance.justify_left"})->checked, "and the checkbox shows it");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true,.letter=u'Z'})) && !demo.ui.presentation().justifyAsLeft, "it is one undo step");
    test::check(bool(tree.perform({"appearance.justify_left"}, Verb::toggle)) && bool(tree.perform({"appearance.reset"}, Verb::invoke)) && !demo.ui.presentation().justifyAsLeft, "reset appearance turns it off");
    // The License text is long English and stays left-aligned, whatever the setting.
    test::check(tree.get({"about.license_text"})->textAlign == TextAlign::left, "the About license text stays left-aligned");
}
void accentFollowsTheExistingAppearanceControlsInTabOrder() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto order = detail::access(demo.ui).focusOrder();
    auto at = [&](const char* id) { return std::find(order.begin(), order.end(), std::string(id)) - order.begin(); };
    test::check(at("appearance.text_size") < at("appearance.dark") && at("appearance.dark") < at("appearance.compact") && at("appearance.compact") < at("appearance.accent") && at("appearance.accent") < at("appearance.reset"), "accent dropdown comes after the existing controls so their keyboard order is unchanged");
}
void exercise() {
    settingsDemo::Demo demo;
    test::check(demo.nav.selected()->id == "general", "general selected initially");
    auto navigation = demo.ui.semantics().get({"app.nav"});
    auto pages = demo.ui.semantics().children({"app.nav"});
    test::check(navigation && navigation->role == Role::list && pages && pages->size() == 4,
                "semantic navigation exposes the four demo pages");
    for (const auto& page : *pages) {
        auto node = demo.ui.semantics().get(page);
        test::check(node && node->role == Role::listItem && node->selected == (page.item->id == "general"),
                    "semantic page items identify the selected page");
    }
    auto itemsProbe = demo.ui.addLabel({.id="probe.items"}).value.value();
    auto appearanceProbe = demo.ui.addLabel({.id="probe.appearance"}).value.value();
    auto aboutProbe = demo.ui.addLabel({.id="probe.about"}).value.value();
    test::check(bool(demo.nav.addMember({"items"}, itemsProbe)) && bool(demo.nav.addMember({"appearance"}, appearanceProbe)) &&
                bool(demo.nav.addMember({"about"}, aboutProbe)), "attach page-specific semantic probes");
    for (const char* page : {"general", "items", "appearance", "about"}) {
        test::check(bool(demo.nav.select({page})), "semantic page test switches page");
        auto field = demo.ui.semantics().get({"general.name"});
        auto itemsField = demo.ui.semantics().get({"items.search"});
        auto item = demo.ui.semantics().get({"app.nav", OptionValue{page}});
        test::check(field && field->hidden == (std::string_view(page) != "general") && item && item->selected,
                    "semantic tree reflects navigation and hidden general members");
        test::check(itemsField && itemsField->hidden == (std::string_view(page) != "items"),
                    "semantic tree hides the Items field outside its page");
        for (const auto& [id, memberPage] : {std::pair{"probe.items", "items"}, std::pair{"probe.appearance", "appearance"}, std::pair{"probe.about", "about"}}) {
            auto member = demo.ui.semantics().get({id});
            test::check(member && member->hidden == (std::string_view(page) != memberPage), "non-current page member is hidden");
        }
    }
    test::check(bool(demo.nav.select({"general"})), "restore general page");
    test::check(demo.name.draft().value.value() == u"測試使用者", "initial name");
    test::check(demo.volume.draft().value.value() == 40, "initial volume");
    test::check(detail::access(demo.ui).focused() == "app.nav", "navigation initially focused");
    detail::access(demo.ui).keyDown({.key=Key::tab});
    test::check(detail::access(demo.ui).focused() == "general.name", "tab enters general page");
    demo.volume.focus();
    detail::access(demo.ui).keyDown({.key=Key::tab});
    test::check(detail::access(demo.ui).focused() == "app.nav", "tab wraps past disabled toolbar");
    detail::access(demo.ui).keyDown({.key=Key::tab, .shift=true});
    test::check(detail::access(demo.ui).focused() == "general.volume", "shift tab wraps to last page field");
    demo.name.focus();
    demo.name.setValue(u"\u5de5\u4f5c\u7ad9 A");
    detail::access(demo.ui).keyDown({.key=Key::tab});
    test::check(!demo.name.describe()->sourceConflict, "confirmed name agrees with source");
    test::check(bool(demo.name.focus()), "name can be edited again");
    test::check(bool(demo.name.setValue(u"\u5de5\u4f5c\u7ad9 B")), "second edit succeeds");
    detail::access(demo.ui).keyDown({.key=Key::tab});
    test::check(!demo.name.describe()->sourceConflict, "second confirmation agrees with source");
    demo.volume.setValue(45);
    demo.volume.focus();
    detail::access(demo.ui).keyDown({.key=Key::tab});
    test::check(detail::access(demo.ui).focused() == "app.undo", "editing enables undo in tab order");
    test::check(bool(demo.nav.select({"about"})), "navigate to about");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()})
    for (int size : {12, 16, 28})
    for (auto viewport : {std::pair{960,720}, std::pair{640,480}}) {
        test::FakeMeasure measure; ScrollState scroll;
        auto root = layout::row({layout::component(demo.nav, size), layout::column({})});
        auto frame = arrange(root, demo.ui, *appearance, measure, viewport.first, viewport.second, 1, scroll);
        auto* item = frame.find(demo.nav);
        test::check(item && item->options.size() == 4 && item->bounds.w >= 160, "four navigation rows retain natural width");
        demo.nav.focus();
        for (const char* page : {"general", "items", "appearance", "about"}) {
            demo.nav.select({page});
            auto semantic = demo.nav.describe();
            RoleProbe probe(detail::access(demo.ui), *item, *semantic, *appearance, 1, true);
            detail::componentType(ComponentKind::navigationList).paint(probe);
            test::check(probe.selected && probe.focused, "selected and focused rows carry draw roles");
        }
        for (std::size_t i=0; i<item->options.size(); ++i) {
            const auto& r = item->options[i];
            test::check(frame.optionAt(*item, r.x+r.w/2, r.y+r.h/2) == i, "row center hits painted row");
            if (i) test::check(item->options[i-1].y+item->options[i-1].h <= r.y, "rows do not overlap");
        }
    }
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        test::FakeMeasure measure; ScrollState scroll;
        auto settings = demo.ui.presentation(); settings.textSize = 12;
        test::check(bool(demo.ui.setPresentation(settings)), "small presentation accepted");
        auto small = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
        auto smallNav = *small.find(demo.nav);
        settings.textSize = 28;
        test::check(bool(demo.ui.setPresentation(settings)), "large presentation accepted");
        for (const char* page : {"general", "items", "appearance", "about"}) {
            demo.nav.select({page});
            auto large = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
            auto* nav = large.find(demo.nav);
            auto title = std::find_if(large.items.begin(), large.items.end(), [](const auto& item) { return item.id == "app.page_title"; });
            test::check(nav && nav->textSize == 28 && nav->bounds.w > smallNav.bounds.w &&
                        nav->options[0].h > smallNav.options[0].h, "presentation reflows navigation");
            test::check(title != large.items.end() && title->textSize == 42, "presentation reflows demo page title");
            if (page == std::string_view("general"))
                test::check(large.find(demo.name) && large.find(demo.name)->textSize == 28, "presentation reflows demo fields");
        }
    }
    demo.nav.select({"about"});
    demo.nav.select({"items"});
    test::check(demo.itemsList.describe()->items.size() == 20, "items page starts with twenty entries");
    demo.itemsSearch.setValue(u"\u7bc4\u672c 0");
    test::check(demo.itemsList.describe()->items.size()==7, "search matches seven numbered templates");
    demo.itemsSearch.setValue(u"XYZ");
    test::check(demo.itemsList.describe()->items.empty(), "unmatched search is empty");
    demo.itemsSearch.setValue(u"\u5171\u7528");
    test::check(demo.itemsList.describe()->items.size() == 2, "search filters duplicate names");
    demo.itemsSearch.setValue(u"");
    test::check(bool(demo.itemsAdd.invoke()), "open add item");
    test::check(bool(*demo.itemDialog.isOpen()), "item dialog opens");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        test::FakeMeasure measure; ScrollState scroll;
        auto frame=arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
        test::check(frame.modal && frame.find(demo.itemName), "both appearances arrange the same item dialog");
    }
    test::check(!demo.itemOk.invoke() && *demo.itemDialog.isOpen(), "empty name keeps dialog open");
    test::check(detail::access(demo.ui).focused()=="items.dialog.name" && !demo.itemName.describe()->error.empty(), "invalid name is focused with an error");
    demo.itemName.setValue(u"Workstation A");
    test::check(bool(demo.itemOk.invoke()), "valid item is added");
    test::check(!*demo.itemDialog.isOpen() && demo.itemsList.describe()->items.size()==21, "new item visible");
    test::check(bool(demo.itemsList.select({"T006"})), "select item for edit");
    demo.itemsList.focus();
    test::check(bool(detail::access(demo.ui).keyDown({.key=Key::enter})) && *demo.itemDialog.isOpen(), "Enter opens edit");
    test::check(demo.itemName.draft().value.value()==u"\u5171\u7528\u7bc4\u672c", "edit loads selected name");
    demo.itemName.setValue(u"Changed");
    test::check(bool(demo.itemOk.invoke()), "edit item");
    demo.itemsList.focus();
    test::check(bool(detail::access(demo.ui).keyDown({.key=Key::del})) && *demo.deleteDialog.isOpen(), "Delete opens confirmation");
    detail::access(demo.ui).keyDown({.key=Key::escape});
    test::check(!*demo.deleteDialog.isOpen() && demo.itemsList.describe()->items.size()==21, "Escape cancels deletion");
    detail::access(demo.ui).keyDown({.key=Key::del});
    detail::access(demo.ui).keyDown({.key=Key::tab});
    detail::access(demo.ui).keyDown({.key=Key::space});
    detail::access(demo.ui).keyUp(Key::space);
    test::check(!*demo.deleteDialog.isOpen() && demo.itemsList.describe()->items.size()==20, "keyboard confirms deletion");
    test::check(!demo.itemsEdit.describe()->enabled && !demo.itemsDelete.describe()->enabled, "actions disabled with no selection");
    demo.itemsSearch.setValue(u"\u5171\u7528");
    test::check(bool(demo.itemsAdd.invoke()), "add from filtered list");
    demo.itemName.setValue(u"Outside filter");
    test::check(bool(demo.itemOk.invoke()), "filtered add succeeds");
    test::check(demo.itemsSearch.draft().value.value().empty() && demo.itemsList.describe()->items.size()==21, "filtered add clears search and reveals item");
    demo.nav.select({"about"});
    test::check(demo.name.setValue(u"X").status == Status::hidden, "general page hidden");
}

void itemSearchCountAndEmptyState() {
    struct SearchCase { std::u16string query, count; std::vector<std::string> ids; };
    const std::vector<SearchCase> cases = {
        {u"\u5171\u7528", u"\u5171 20 \u7b46\uff0c\u986f\u793a 2 \u7b46", {"T005", "T006"}},
        {u"\u7bc4\u672c 0", u"\u5171 20 \u7b46\uff0c\u986f\u793a 7 \u7b46", {"T001", "T002", "T003", "T004", "T007", "T008", "T009"}},
        {u"XYZ", u"\u5171 20 \u7b46\uff0c\u986f\u793a 0 \u7b46", {}},
        {u"", u"\u5171 20 \u7b46\uff0c\u986f\u793a 20 \u7b46", {}}
    };
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo;
        test::check(bool(demo.nav.select({"items"})), "open items page");
        auto& state = detail::access(demo.ui);
        const auto status = state.describe("app.status")->value;
        for (const auto& c : cases) {
            std::vector<std::u16string> redrawCounts;
            state.onInvalidate([&] { redrawCounts.push_back(state.describe("items.count")->value); });
            test::check(bool(demo.itemsSearch.setValue(c.query)), "search succeeds");
            state.onInvalidate({});
            test::check(!redrawCounts.empty() && std::all_of(redrawCounts.begin(), redrawCounts.end(), [&](const auto& value) { return value == c.count; }), "every requested redraw observes the updated count");
            auto list = demo.itemsList.describe();
            test::check(bool(list) && list->items.size() == (c.query.empty() ? 20 : c.ids.size()), "search returns the expected number of items");
            for (std::size_t i = 0; i < c.ids.size(); ++i)
                test::check(list->items[i].value.id == c.ids[i], "search preserves the expected item identities");
            test::FakeMeasure measure; ScrollState scroll;
            auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1, scroll);
            auto count = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& item) { return item.id == "items.count"; });
            std::u16string renderedCount;
            if (count != frame.items.end()) for (const auto& line : count->lines) renderedCount += line.text;
            test::check(count != frame.items.end() && renderedCount == c.count, "item count text matches the search result");
            if (c.query == u"XYZ") {
                auto* empty = frame.find(demo.itemsList);
                test::check(empty && empty->options.empty() && empty->lines.size() == 1 && empty->lines[0].text == u"\u6c92\u6709\u9805\u76ee", "empty search displays the empty-list message");
                TextProbe paint(state, *empty, *list, *appearance, 1, false);
                detail::componentType(ComponentKind::list).paint(paint);
                test::check(std::find(paint.drawn.begin(), paint.drawn.end(), u"\u6c92\u6709\u9805\u76ee") != paint.drawn.end(), "empty-list message is painted");
            }
            test::check(state.describe("app.status")->value == status, "search keeps status");
            test::check(!state.describe("app.undo")->enabled, "search does not create history");
        }
    }
}

int historySteps(settingsDemo::Demo& demo) {
    auto& state=detail::access(demo.ui); int count=0;
    while (state.describe("app.undo")->enabled) {
        test::check(bool(state.invoke("app.undo")), "history step can be undone");
        test::check(++count <= 100, "history is bounded");
    }
    return count;
}

void hs01StepMatrix() {
    using Action=std::function<void(settingsDemo::Demo&)>;
    struct Case { const char* name; Action setup, action; int steps; };
    auto none=[](settingsDemo::Demo&) {};
    auto items=[](settingsDemo::Demo& d) { test::check(bool(d.nav.select({"items"})), "show items"); };
    auto selected=[](settingsDemo::Demo& d) { d.nav.select({"items"}); d.itemsList.select({"T006"}); };
    const std::vector<Case> cases={
        {"name",none,[](auto& d){ d.name.focus(); d.name.setValue(u"Station"); d.nav.focus(); },1},
        {"notices",none,[](auto& d){ d.notices.toggle(); },1},
        {"mode",none,[](auto& d){ d.mode.select({"focus"}); },1},
        {"region",none,[](auto& d){ d.region.select({"south"}); },1},
        {"volume",none,[](auto& d){ d.volume.focus(); d.volume.setValue(45); d.volume.setValue(50); d.nav.focus(); },1},
        {"add",items,[](auto& d){ d.itemsAdd.invoke(); d.itemName.setValue(u"Station"); d.itemOk.invoke(); },1},
        {"edit",selected,[](auto& d){ d.itemsEdit.invoke(); d.itemName.setValue(u"Station"); d.itemOk.invoke(); },1},
        {"delete",selected,[](auto& d){ d.itemsDelete.invoke(); detail::access(d.ui).invoke("items.delete_dialog.confirm"); },1},
        {"font",none,[](auto& d){ auto p=d.ui.presentation(); p.fontFamily=u"Consolas"; d.setPresentation(p); },1},
        {"text size",none,[](auto& d){ auto p=d.ui.presentation(); p.textSize=28; d.setPresentation(p); },1},
        {"dark",none,[](auto& d){ auto p=d.ui.presentation(); p.variant=ThemeVariant::dark(); d.setPresentation(p); },1},
        {"compact",none,[](auto& d){ auto p=d.ui.presentation(); p.density=Density::compact; d.setPresentation(p); },1},
        {"reset presentation",[](auto& d){ auto p=d.ui.presentation(); p.fontFamily=u"Consolas"; d.setPresentation(p); p.textSize=28; d.setPresentation(p); p.variant=ThemeVariant::dark(); d.setPresentation(p); p.density=Density::compact; d.setPresentation(p); },[](auto& d){ d.setPresentation(PresentationSettings{}); },5},
        {"revert",[](auto& d){ d.notices.toggle(); },[](auto& d){ detail::access(d.ui).invoke("app.revert"); },2}
    };
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        for (const auto& c : cases) {
            settingsDemo::Demo d; c.setup(d); c.action(d);
            test::FakeMeasure measure; ScrollState scroll;
            arrange(d.page(),d.ui,*appearance,measure,960,720,1.f,scroll);
            test::check(historySteps(d)==c.steps, (std::string("HS01 step count: ")+c.name).c_str());
        }
    }
}

void hs01NonHistoryMatrix() {
    using Action=std::function<void(settingsDemo::Demo&)>;
    struct Case { const char* name; Action action; };
    const std::vector<Case> cases={
        {"search",[](auto& d){ d.nav.select({"items"}); d.itemsSearch.setValue(u"ABC"); }},
        {"selection",[](auto& d){ d.nav.select({"items"}); d.itemsList.select({"T003"}); }},
        {"page",[](auto& d){ d.nav.select({"about"}); }},
        {"candidate",[](auto& d){ d.region.focus(); d.region.open(); detail::access(d.ui).keyDown({.key=Key::down}); test::check(d.region.describe()->candidate == OptionValue{"central"} && *d.region.draft() == OptionValue{"north"}, "candidate moves without confirmation"); d.region.cancel(); }},
        {"save",[](auto& d){ test::check(bool(detail::access(d.ui).invoke("app.save")), "save baseline"); }}
    };
    for (const auto& c : cases) {
        settingsDemo::Demo d; d.notices.toggle(); c.action(d);
        test::check(historySteps(d)==1, (std::string("HS01 no step: ")+c.name).c_str());
    }
    settingsDemo::AboutInfo info; info.licenses={{{"lcars"},u"License",std::u16string(3000,u'A')}};
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo d(info); d.notices.toggle(); d.nav.select({"about"});
        test::FakeMeasure measure; ScrollState scroll;
        arrange(d.page(),d.ui,*appearance,measure,640,480,1.f,scroll);
        test::check(bool(d.ui.semantics().perform({"about.license_text"},Verb::focus)), "focus overflowing license");
        detail::access(d.ui).keyDown({.key=Key::end});
        test::check(d.licenseScroll.offset>0, "scroll moves viewport");
        test::check(historySteps(d)==1, "scroll does not add history");
    }
}

void it03UnchangedEditHasNoHistory() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.nav.select({"items"}); demo.itemsList.select({"T006"});
    test::check(bool(demo.itemsEdit.invoke()), "open T006");
    demo.itemName.setValue(u"\u5171\u7528\u7bc4\u672c B");
    test::check(bool(demo.itemOk.invoke()), "rename T006");
    auto items=demo.itemsList.describe()->items;
    test::check(items[4].label==u"\u5171\u7528\u7bc4\u672c" && items[5].label==u"\u5171\u7528\u7bc4\u672c B", "only T006 changed");
    test::check(bool(demo.itemsEdit.invoke()) && bool(demo.itemOk.invoke()), "confirm unchanged edit");
    test::check(bool(state.invoke("app.undo")), "undo changed edit");
    items=demo.itemsList.describe()->items;
    test::check(items[4].label==u"\u5171\u7528\u7bc4\u672c" && items[5].label==u"\u5171\u7528\u7bc4\u672c", "one undo restores T006");
    test::check(!state.describe("app.undo")->enabled, "unchanged edit adds no history step");
}

void hs03ValueEqualityAndSavedName() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    std::u16string title; demo.onDirtyChanged([&](bool value) { title = value ? u"*Settings Demo" : u"Settings Demo"; });
    demo.volume.focus(); demo.volume.setValue(45); demo.nav.focus();
    demo.volume.focus(); demo.volume.setValue(40); demo.nav.focus();
    test::check(title==u"Settings Demo" && !state.describe("app.save")->enabled, "returned volume equals baseline");
    test::check(bool(state.invoke("app.undo")) && *demo.volume.draft()==45, "undo second focus period");
    test::check(bool(state.invoke("app.undo")) && *demo.volume.draft()==40, "undo first focus period");
    test::check(!state.describe("app.undo")->enabled, "exactly two volume steps");
    demo.notices.toggle(); test::check(bool(state.invoke("app.save")), "save changed baseline");
    demo.name.focus(); demo.name.setValue(u"New name"); demo.nav.focus();
    test::check(title==u"*Settings Demo", "name change marks title dirty");
    test::check(bool(state.invoke("app.undo")), "undo name after save");
    test::check(title==u"Settings Demo" && !state.describe("app.save")->enabled, "undo to saved value clears title marker");
    test::check(state.describe("app.status")->value == u"\u6240\u6709\u4fee\u6539\u5df2\u5132\u5b58", "saved status restored");
}

void hs02UndoRedoSequence() {
    settingsDemo::Demo demo; auto& state = detail::access(demo.ui);
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

void historyDeleteRestore() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.nav.select({"items"}); demo.itemsList.select({"T003"}); demo.itemsDelete.invoke();
    test::check(bool(state.invoke("items.delete_dialog.confirm")), "confirm deletion");
    test::check(demo.itemsList.describe()->items.size()==19, "deleted item absent");
    test::check(bool(state.invoke("app.undo")), "undo deletion");
    auto list=demo.itemsList.describe();
    test::check(list->items.size()==20 && list->items[2].value.id=="T003", "undo restores original identity and order");
}

void historyKeyRepeat() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.notices.toggle(); demo.volume.setValue(45); demo.nav.focus();
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "first undo");
    for (int i=0; i<10; ++i) state.keyDown({.ctrl=true, .repeat=true, .letter=u'Z'});
    test::check(*demo.volume.draft()==40 && !*demo.notices.draft(), "repeated key does not undo another step");
}

void historyFieldShortcuts() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.notices.toggle(); demo.name.focus(); demo.name.setValue(u"A");
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "field undo");
    test::check(*demo.notices.draft()==false, "field undo leaves document history alone");
    demo.nav.focus(); state.keyUp(Key::other);
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "document undo");
    test::check(*demo.notices.draft(), "document undo restores previous setting");
}

void historyLimitAndBranch() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    for (int i=0; i<101; ++i) test::check(bool(demo.notices.toggle()), "toggle step");
    for (int i=0; i<100; ++i) test::check(bool(state.invoke("app.undo")), "undo retained step");
    test::check(!state.describe("app.undo")->enabled && !*demo.notices.draft(), "oldest step is discarded");
    test::check(bool(state.invoke("app.redo")), "redo retained step");
    demo.volume.setValue(45);
    test::check(!state.describe("app.redo")->enabled, "new step clears redo");
}

void historyPresentation() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    auto value=demo.ui.presentation(); value.textSize=28; value.variant=ThemeVariant::dark();
    test::check(bool(demo.setPresentation(value)), "presentation change succeeds");
    test::check(state.describe("app.save")->enabled, "presentation changes mark document dirty");
    test::check(bool(state.invoke("app.undo")), "undo presentation");
    test::check(demo.ui.presentation().textSize==16 && demo.ui.presentation().variant==ThemeVariant::light(), "undo applies presentation");
}

void historyItems() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.nav.select({"items"}); demo.itemsAdd.invoke(); demo.itemName.setValue(u"Station");
    test::check(bool(demo.itemOk.invoke()), "add item");
    test::check(demo.itemsList.describe()->items.size()==21, "new item visible");
    test::check(bool(state.invoke("app.undo")), "undo item addition");
    test::check(demo.itemsList.describe()->items.size()==20, "undo removes item");
    demo.itemsAdd.invoke(); demo.itemName.setValue(u"Next"); demo.itemOk.invoke();
    test::check(demo.itemsList.describe()->items.back().value.id=="T021", "undo restores next item id");
}

void historyRevertIsUndoable() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.notices.toggle(); demo.volume.setValue(55);
    test::check(bool(state.invoke("app.revert")), "revert restores baseline");
    test::check(*demo.notices.draft() && *demo.volume.draft()==40, "revert restores all values");
    test::check(bool(state.invoke("app.undo")), "revert itself can be undone");
    test::check(!*demo.notices.draft() && *demo.volume.draft()==55, "undo restores changes");
}

void historySaveFocusedName() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.name.focus(); demo.name.setValue(u"Station");
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'S'})), "save commits focused name");
    test::check(!state.describe("app.save")->enabled, "saved draft is clean");
    demo.nav.focus();
    test::check(!state.describe("app.save")->enabled, "blur after save remains clean");
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "name save keeps history");
    test::check(*demo.name.draft() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "undo returns old name");
}

void historyInvalidSave() {
    settingsDemo::Demo demo; auto& state=detail::access(demo.ui);
    demo.name.focus(); demo.name.setValue(u""); demo.nav.select({"about"});
    test::check(!state.keyDown({.ctrl=true, .letter=u'S'}), "invalid save is refused");
    test::check(demo.nav.selected()->id == "general", "invalid save returns to general");
    test::check(demo.name.describe()->focused && !demo.name.describe()->error.empty(), "invalid field receives focus and error");
}

void historyInvalidDraft() {
    settingsDemo::Demo demo;
    auto& state=detail::access(demo.ui);
    demo.name.focus(); demo.name.setValue(u""); demo.nav.focus();
    test::check(!demo.name.describe()->error.empty(), "invalid draft retains error");
    test::check(state.describe("app.undo")->enabled, "invalid draft can be discarded with undo");
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "undo discards rejected draft");
    test::check(*demo.name.draft() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "discard restores document value");
    test::check(demo.name.describe()->error.empty(), "discard clears error");
}

void historyTitleNotification() {
    settingsDemo::Demo demo;
    std::vector<bool> states;
    demo.onDirtyChanged([&](bool value) { states.push_back(value); });
    demo.name.focus(); demo.name.setValue(u"Station");
    test::check(!states.empty() && states.back(), "draft notifies dirty title");
    demo.name.undo();
    test::check(!states.back(), "discarding draft clears title marker");
}

void historySliderGroup() {
    settingsDemo::Demo demo;
    auto& state=detail::access(demo.ui);
    demo.volume.focus(); demo.volume.setValue(45); demo.volume.setValue(50); demo.volume.setValue(55);
    demo.nav.focus();
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "undo slider group");
    test::check(*demo.volume.draft() == 40, "slider focus period is one history step");
}

void historyDraftDirty() {
    settingsDemo::Demo demo;
    auto& state = detail::access(demo.ui);
    demo.name.focus(); demo.name.setValue(u"New name");
    test::check(state.describe("app.save")->enabled, "draft enables save");
    test::check(state.describe("app.status")->value == u"\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539", "draft updates status");
}

void historySaveRevert() {
    settingsDemo::Demo demo;
    auto& state = detail::access(demo.ui);
    demo.notices.toggle();
    test::check(state.describe("app.save")->enabled, "changed document can be saved");
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'S'})), "save shortcut");
    test::check(!state.describe("app.save")->enabled, "save clears dirty state");
    state.keyUp(Key::other);
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "undo after save");
    test::check(*demo.notices.draft(), "undo restores original value");
    test::check(state.describe("app.save")->enabled, "undo after save is dirty");
}

void historyToggleUndo() {
    settingsDemo::Demo demo;
    auto& state = detail::access(demo.ui);
    test::check(bool(demo.notices.toggle()), "change notices");
    test::check(bool(state.keyDown({.ctrl=true, .letter=u'Z'})), "undo shortcut");
    test::check(*demo.notices.draft(), "undo restores notices");
}
void aboutPageContent() {
    settingsDemo::AboutInfo info;
    info.appearance = u"Minimal Web"; info.renderer = u"GL version — renderer";
    info.build = u"123456789abc、Release"; info.settingsPath = u"C:\\settings.json";
    settingsDemo::Demo demo(info);
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
    settingsDemo::Demo withDiagnostics(info);
    test::check(withDiagnostics.ui.semantics().get({"about.diagnostics"})->value == u"診斷：基本診斷（只存記憶體）", "diagnostics present when selected");
    test::check(tree.get({"about.framework"})->value.find(lcars::frameworkVersion) != std::u16string::npos, "framework version is shown");
}
void aboutVerticalLabel() {
    settingsDemo::AboutInfo info;
    settingsDemo::Demo demo(info);
    test::check(bool(demo.nav.select({"about"})), "open About");
    auto node = demo.ui.semantics().get({"about.vertical"});
    test::check(node && node->role == Role::label && node->writingMode == WritingMode::vertical, "the About page has a vertical label");
    test::check(node->value == test::vertical::aboutSample, "the vertical label carries the sample sentence");
    // It shows every kind of piece the rules cover: a sideways word, a bracket, combined digits, a comma and a period.
    std::vector<VerticalForm> forms;
    for (const auto& segment : detail::segmentVertical(node->value, 3)) forms.push_back(segment.form);
    for (auto form : {VerticalForm::sideways, VerticalForm::bracket, VerticalForm::combined, VerticalForm::point, VerticalForm::upright})
        test::check(std::find(forms.begin(), forms.end(), form) != forms.end(), "the sample covers every piece form");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        test::FakeMeasure measure;
        auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto item = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& p) { return p.id == "about.vertical"; });
        test::check(item != frame.items.end() && item->vertical && item->columnLayout.columns.size() >= 2 && item->bounds.h > 0, "the page lays the label out in columns");
        auto semantic = demo.ui.semantics().get({"about.vertical"});
        test::check(semantic && semantic->bounds && semantic->name == test::vertical::aboutSample &&
                    test::vertical::drawnBox(semantic->bounds->x, semantic->bounds->y, semantic->bounds->x + semantic->bounds->w, semantic->bounds->y + semantic->bounds->h,
                                              0, 0, 960, 720, 200) && *semantic->bounds == intersect(item->bounds, item->clip),
                    "the semantic node reports the text in reading order and the box that is drawn"); 
    }
}
void td01FailureDiagnostic() {
    auto reset = settingsDemo::failureDiagnostic({lcars::RendererFailureKind::deviceReset, 0x8253, u"Graphics reset"});
    test::check(reset.kind == lcars::DiagnosticKind::deviceReset && reset.severity == lcars::DiagnosticSeverity::error &&
                reset.code == "renderer_failed.device_reset" && reset.nativeCode == 0x8253 && reset.nativeMessage == "Graphics reset" &&
                reset.backend == u"OpenGL", "TD01 a device reset becomes a device_reset diagnostic");
    auto present = settingsDemo::failureDiagnostic({lcars::RendererFailureKind::presentFailed, 5, u"SwapBuffers 失敗 \U0001F600"});
    test::check(present.kind == lcars::DiagnosticKind::other && present.code == "renderer_failed.present_failed" &&
                present.nativeMessage == "SwapBuffers \xE5\xA4\xB1\xE6\x95\x97 \xF0\x9F\x98\x80", "TD01 other failures keep kind and UTF-8 message");
    test::check(settingsDemo::failureDiagnostic({lcars::RendererFailureKind::frameError, 0, u""}).code == "renderer_failed.frame_error" &&
                settingsDemo::failureDiagnostic({lcars::RendererFailureKind::resourceFailed, 0, u""}).code == "renderer_failed.resource_failed",
                "TD01 every failure kind is named");
}
void aboutLicenses() {
    settingsDemo::AboutInfo info;
    info.licenses = {{{"lcars"}, u"framework", u"framework license"}, {{"freetype"}, u"freetype", u"freetype license"}, {{"harfbuzz"}, u"harfbuzz", u"harfbuzz license"}, {{"khronos"}, u"khronos", u"khronos license"}, {{"json"}, u"json", u"json license"}};
    settingsDemo::Demo demo(info);
    test::check(bool(demo.nav.select({"about"})), "open About");
    for (const auto& license : info.licenses) {
        test::check(bool(demo.licenses.select(license.id)), "select license");
        auto text = demo.ui.semantics().get({"about.license_text"});
        test::check(text && text->value == license.text, "selected license text is exposed");
    }
}
void aboutKeyboardScroll() {
    settingsDemo::AboutInfo info;
    info.licenses = {{{"lcars"}, u"framework", std::u16string(5000, u'x')}};
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo(info); test::FakeMeasure measure;
        demo.nav.select({"about"});
        auto frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto& state = detail::access(demo.ui);
        demo.licenses.focus();
        state.keyDown({.key=Key::tab});
        test::check(state.focused() == "about.license_text", "overflowing license receives Tab focus");
        auto semantic = demo.ui.semantics().get({"about.license_text"});
        test::check(semantic && semantic->role == Role::label && semantic->focusable, "license remains semantic text while focusable");
        demo.licenses.focus();
        test::check(bool(demo.ui.semantics().perform({"about.license_text"}, Verb::focus)), "semantic focus reaches overflowing license");
        auto item = std::find_if(frame.items.begin(), frame.items.end(), [](const auto& p) { return p.id == "about.license_text"; });
        test::check(item != frame.items.end(), "license has placement");
        auto description = state.describe("about.license_text");
        RoleProbe paint(state, *item, *description, *appearance, 1.f, true);
        detail::componentType(ComponentKind::label).paint(paint);
        test::check(paint.focused, "focused license draws a focus indicator");
        state.keyDown({.key=Key::down});
        test::check(demo.licenseScroll.offset > 0, "Down scrolls license");
        state.keyDown({.key=Key::end});
        frame = arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, demo.scroll);
        auto region = std::find_if(frame.regions.begin(), frame.regions.end(), [&](const auto& r) { return r.state == &demo.licenseScroll; });
        test::check(region != frame.regions.end() && region->offset == region->maximum, "End reveals last line");
        state.keyDown({.key=Key::home});
        test::check(demo.licenseScroll.offset == 0, "Home reveals first line");
        settingsDemo::AboutInfo shortInfo;
        shortInfo.licenses = {{{"lcars"}, u"framework", u"short text"}};
        settingsDemo::Demo shortDemo(shortInfo);
        shortDemo.nav.select({"about"});
        arrange(shortDemo.page(), shortDemo.ui, *appearance, measure, 960, 720, 1.f, shortDemo.scroll);
        auto shortLabel = shortDemo.ui.semantics().get({"about.license_text"});
        test::check(shortLabel && !shortLabel->focusable, "fitting license stays out of Tab order");
    }
}
// #80: the licence text breathes (line spacing 1.4, 8 DIP between paragraphs) and the appearance preview is centered;
// the settings pages offer no controls for either (ADR 0014: code switches and demo switches stay apart).
void demoTextFlow() {
    settingsDemo::AboutInfo info;
    info.licenses = {{{"lcars"}, u"framework", u"alpha\nbeta"}};
    settingsDemo::Demo demo(info);
    auto& tree = demo.ui.semantics();
    test::check(tree.get({"about.license_text"})->lineSpacing == 1.4 && tree.get({"about.license_text"})->paragraphSpacing == 8, "licence text: 1.4 spacing, 8 DIP paragraphs");
    test::check(tree.get({"about.license_text"})->textAlign == TextAlign::left && tree.get({"appearance.preview"})->textAlign == TextAlign::center, "the preview is centered, the licence text stays left");
    test::check(tree.get({"appearance.preview"})->lineSpacing == 1.0 && tree.get({"appearance.preview"})->paragraphSpacing == 0, "the preview keeps the default spacing");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
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
void failureRetryFlow() {
    struct Prompt : settingsDemo::NativePrompt {
        int attempts = 0, saves = 0;
        bool retry(std::u16string_view) override { return ++attempts < 3; }
        settingsDemo::SaveChoice save(bool) override { ++saves; return settingsDemo::SaveChoice::discard; }
        void error(std::u16string_view) override {}
    } prompt;
    int rebuilds=0, closes=0;
    settingsDemo::failureFlow(prompt, u"failed", [] { return true; }, [&] { ++rebuilds; return false; }, [] { return std::u16string{}; }, [&] { ++closes; });
    test::check(rebuilds==2 && closes==1 && prompt.saves==1, "retry only on choice then discard");
}
void failureChoices() {
    struct Prompt : settingsDemo::NativePrompt {
        settingsDemo::SaveChoice choice = settingsDemo::SaveChoice::cancel;
        bool sawBlank=false; int errors=0;
        bool retry(std::u16string_view) override { return false; }
        settingsDemo::SaveChoice save(bool blank) override { sawBlank=blank; return choice; }
        void error(std::u16string_view) override { ++errors; }
    } prompt;
    int writes=0;
    auto save=[&] { ++writes; return std::u16string(u"failed"); };
    test::check(!settingsDemo::failureClose(prompt,true,true,save) && prompt.sawBlank && writes==0, "cancel returns to retry");
    prompt.choice=settingsDemo::SaveChoice::save;
    test::check(!settingsDemo::failureClose(prompt,true,false,save) && writes==1 && prompt.errors==1, "save failure returns to retry");
    prompt.choice=settingsDemo::SaveChoice::discard;
    test::check(settingsDemo::failureClose(prompt,true,false,save) && writes==1, "discard does not save");
}
void failureDraftSave() {
    settingsDemo::Demo demo;
    settingsDemo::SettingsDocument saved=demo.document();
    demo.onSave([&](const auto& document) { saved=document; return std::u16string{}; });
    test::check(bool(demo.name.setValue(u"Draft")), "prepare draft");
    detail::access(demo.ui).rendererFailed(true);
    test::check(demo.isDirty() && demo.saveFailedRenderer().empty() && saved.name==u"Draft", "failure saves valid draft");
    detail::access(demo.ui).rendererFailed(false);
    test::check(bool(demo.name.setValue(u"")), "prepare blank draft");
    detail::access(demo.ui).rendererFailed(true);
    test::check(demo.blankNameDraft() && demo.saveFailedRenderer().empty() && saved.name==demo.document().name, "failure preserves committed name when draft blank");
}
void closeInvalidName() {
    settingsDemo::Demo demo; int closed=0;
    demo.onClose([&] { ++closed; });
    test::check(bool(demo.name.focus()) && bool(demo.name.setValue(u"")), "empty name draft");
    test::check(!demo.requestClose(), "invalid draft prompts");
    test::check(!demo.closeSave.invoke() && closed==0 && demo.name.describe()->focused && !demo.name.describe()->error.empty(), "invalid draft remains open and focused");
}
void closeSaveAndLayout() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo;
        int writes=0, closes=0;
        demo.onClose([&] { ++closes; });
        demo.onSave([&](const settingsDemo::SettingsDocument& document) { ++writes; test::check(document.volume==65, "saved changed volume"); return std::u16string{}; });
        test::check(bool(demo.volume.setValue(65)), "change before save");
        test::check(!demo.requestClose(), "prompt before save");
        test::FakeMeasure measure; ScrollState scroll;
        auto frame=arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll);
        auto has=[&](std::string_view id) { return std::any_of(frame.overlays.begin(), frame.overlays.end(), [&](const auto& item) { return item.id==id; }); };
        test::check(has("app.close_dialog.save") && !has("items.dialog.ok"), "MD04 active close dialog only");
        test::check(bool(demo.closeSave.invoke()) && writes==1 && closes==1, "save closes after successful write");
    }
    settingsDemo::Demo failed; int closed=0;
    failed.onClose([&] { ++closed; }); failed.onSave([](const auto&) { return std::u16string(u"disk full"); });
    test::check(bool(failed.volume.setValue(65)) && !failed.requestClose(), "prepare failed save");
    test::check(!failed.closeSave.invoke() && closed==0 && failed.isDirty(), "failed save keeps changes and window");
    test::check(failed.ui.semantics().get({"app.error_dialog.ok"})->focused, "error dialog available");
}
void closeItemDraft() {
    settingsDemo::Demo demo;
    test::check(bool(demo.volume.setValue(65)), "dirty document");
    test::check(bool(demo.nav.select({"items"})), "items page");
    auto before=demo.document();
    test::check(bool(demo.itemsAdd.invoke()), "open item dialog");
    test::check(bool(demo.itemName.setValue(u"discard me")), "type item draft");
    test::check(!demo.requestClose(), "dirty close vetoed");
    test::check(!demo.itemDialog.isOpen().value.value() && demo.closeDialog.isOpen().value.value(), "item canceled before close prompt");
    test::check(demo.document()==before, "item draft discarded");
    test::check(!demo.requestClose() && !demo.closeDialog.isOpen().value.value(), "second close cancels close dialog");
}
void closeDialogFlow() {
    settingsDemo::Demo demo;
    int closed = 0; demo.onClose([&] { ++closed; });
    test::check(bool(demo.volume.setValue(65)), "dirty volume");
    test::check(!demo.requestClose(), "dirty close is vetoed");
    test::check(demo.closeDialog.isOpen().value == true, "close dialog opened");
    test::check(demo.ui.semantics().get({"app.close_dialog.title"})->value == u"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f", "close title text");
    test::check(demo.ui.semantics().get({"app.close_dialog.save"})->name == u"\u5132\u5b58", "close save label");
    test::check(demo.ui.semantics().get({"app.close_dialog.discard"})->name == u"\u4e0d\u5132\u5b58", "close discard label");
    test::check(demo.ui.semantics().get({"app.close_dialog.cancel"})->name == u"\u53d6\u6d88", "close cancel label");
    test::check(bool(demo.closeCancel.invoke()), "cancel close");
    test::check(!demo.closeDialog.isOpen().value.value() && closed == 0, "cancel keeps application open");
    test::check(!demo.requestClose(), "second request vetoed");
    test::check(bool(demo.closeDiscard.invoke()) && closed == 1, "discard closes");
}
void appearanceSizeHistory() {
    settingsDemo::Demo demo;
    test::check(bool(demo.nav.select({"appearance"})), "open appearance page");
    auto& tree = demo.ui.semantics();
    test::check(bool(tree.get({"appearance.text_size"})), "text size control exists");
    test::check(bool(tree.perform({"appearance.text_size"}, Verb::focus)), "focus text size");
    test::check(bool(tree.perform({"appearance.text_size"}, Verb::setValue, 28)), "change text size");
    test::check(demo.ui.presentation().textSize == 28, "text size applies immediately");
    test::check(bool(tree.perform({"appearance.text_size"}, Verb::setValue, 26)), "second size in focus period");
    test::check(demo.ui.presentation().textSize == 26, "second size applies immediately");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true, .letter=u'Z'})), "undo appearance edit");
    test::check(demo.ui.presentation().textSize == 16, "undo restores presentation");
}
void appearanceToggleReset() {
    settingsDemo::Demo demo;
    demo.nav.select({"appearance"});
    auto& tree = demo.ui.semantics();
    auto settings=demo.ui.presentation(); settings.fontFamily=u"Consolas"; settings.textSize=25;
    test::check(bool(demo.setPresentation(settings)), "change font and size");
    for (const char* id : {"appearance.dark", "appearance.compact"})
        test::check(bool(tree.perform({id}, Verb::toggle)), "toggle appearance setting");
    test::check(demo.ui.presentation().variant == ThemeVariant::dark() && demo.ui.presentation().density == Density::compact, "appearance toggles apply");
    test::check(bool(tree.perform({"appearance.reset"}, Verb::invoke)), "reset appearance");
    test::check(demo.ui.presentation() == PresentationSettings{}, "reset restores defaults");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true,.letter=u'Z'})), "undo reset");
    test::check(demo.ui.presentation().variant == ThemeVariant::dark() && demo.ui.presentation().density == Density::compact &&
                demo.ui.presentation().fontFamily == u"Consolas" && demo.ui.presentation().textSize == 25, "reset is one history step");
}
void appearanceFontHistory() {
    settingsDemo::AboutInfo info;
    info.fonts = {{u"Consolas", u"Consolas"}};
    for (int i=0; i<149; ++i) { auto n=std::to_string(i); auto key=u"Font" + std::u16string(n.begin(),n.end()); info.fonts.push_back({key,key}); }
    settingsDemo::Demo demo(info); demo.nav.select({"appearance"});
    auto& tree=demo.ui.semantics();
    auto options=tree.children({"appearance.font"});
    test::check(options && options->size()==151, "default and all 150 families remain semantic options");
    test::check(bool(tree.perform({"appearance.font", OptionValue{"family:Consolas"}}, Verb::select)), "select font");
    test::check(demo.ui.presentation().fontFamily == u"Consolas", "font applies");
    test::check(bool(detail::access(demo.ui).keyDown({.ctrl=true,.letter=u'Z'})), "undo font");
    test::check(demo.ui.presentation().fontFamily.empty(), "font undo restores default");
}
void appearanceMissingFont() {
    settingsDemo::Demo demo;
    auto settings=demo.ui.presentation(); settings.fontFamily=u"NoSuchFont";
    test::check(bool(demo.setPresentation(settings)), "restore unavailable font");
    demo.nav.select({"appearance"});
    auto node=demo.ui.semantics().get({"appearance.font", OptionValue{"family:NoSuchFont"}});
    test::check(node && node->selected && node->name == u"NoSuchFont\uff08\u672a\u5b89\u88dd\uff09", "unavailable font remains selected");
    auto status=demo.ui.semantics().get({"appearance.font.status"});
    test::check(status && status->value.find(u"NoSuchFont") != std::u16string::npos, "unavailable font is explained");
}
void appearanceInterleavedHistory() {
    settingsDemo::Demo demo; demo.nav.select({"appearance"});
    auto& tree=demo.ui.semantics();
    tree.perform({"appearance.text_size"}, Verb::focus);
    tree.perform({"appearance.text_size"}, Verb::setValue, 20);
    tree.perform({"appearance.dark"}, Verb::toggle);
    tree.perform({"appearance.text_size"}, Verb::setValue, 22);
    auto& state=detail::access(demo.ui);
    state.keyDown({.ctrl=true,.letter=u'Z'});
    test::check(demo.ui.presentation().textSize==20 && demo.ui.presentation().variant==ThemeVariant::dark(), "undo latest size independently");
    state.keyDown({.ctrl=true,.letter=u'Z'});
    test::check(demo.ui.presentation().variant==ThemeVariant::light(), "undo intervening toggle independently");
}
// #79: the item dialog's note is a three-line multi-line field whose breaks survive saving and editing.
void itemNoteIsAMultilineField() {
    settingsDemo::Demo demo; demo.nav.select({"items"});
    test::check(bool(demo.itemsAdd.invoke()), "open the add item dialog");
    auto note = demo.ui.semantics().get({"items.dialog.note"});
    test::check(note && note->role == Role::textField && note->scroll && note->scroll->visibleLines == 3, "the note is a three-line multi-line text field");
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(demo.page(), demo.ui, webAppearance(), measure, 960, 720, 1, scroll);
    const auto* placed = frame.find(demo.itemNote);
    test::check(placed && placed->multiline && placed->view.visible == 3 && placed->textField.editor.h == 3 * measure.lineHeight(placed->textSize), "it is arranged three lines tall");
    test::check(demo.itemNote.describe()->scroll->visibleLines == 3 && !demo.itemName.describe()->multiline, "the name beside it stays single-line");
    demo.itemNote.focus();
    test::check(bool(detail::access(demo.ui).keyDown({.key = Key::enter})) && *demo.itemDialog.isOpen(), "Enter in the note breaks the line and the dialog stays open");
    test::check(demo.itemNote.draft().value.value() == u"\n", "the break is in the draft");
    const std::u16string text = u"first line\nsecond line\n\nfourth";
    test::check(bool(demo.itemNote.setValue(text)) && bool(demo.itemName.setValue(u"Multi")), "a note may hold line breaks");
    test::check(bool(demo.itemOk.invoke()) && !*demo.itemDialog.isOpen(), "the item is saved");
    auto document = demo.document();
    test::check(document.items.back().name == u"Multi" && document.items.back().note == text, "the saved note keeps its line breaks");
    test::check(bool(demo.itemsList.select(document.items.back().id)), "select it again");
    demo.itemsList.focus();
    test::check(bool(detail::access(demo.ui).keyDown({.key = Key::enter})) && *demo.itemDialog.isOpen(), "Enter on the list still opens the edit dialog");
    test::check(demo.itemNote.draft().value.value() == text, "editing loads the note with its breaks");
    test::check(demo.itemNote.setValue(std::u16string(200, u'x') + u"\ny").status == Status::invalidValue, "the 200 character limit still counts a line break");
}
// #81: the item dialog's note is a rich text field with a style bar above it; the buttons and the color list act on
// the note's selection, the selection stays and the note takes the focus back; the styles are saved with the item.
void itemNoteIsRichWithAStyleBar() {
    settingsDemo::Demo demo; demo.nav.select({"items"});
    test::check(bool(demo.itemsAdd.invoke()), "open the add item dialog");
    auto& tree = demo.ui.semantics();
    test::check(tree.get({"items.dialog.note"})->rich && !tree.get({"items.dialog.name"})->rich, "the note is rich, the name is not");
    for (const char* id : {"items.dialog.note.bold", "items.dialog.note.italic", "items.dialog.note.underline"}) test::check(tree.get({id})->role == Role::button, "a style button above the note");
    test::check(tree.get({"items.dialog.note.color"})->role == Role::dropdown && demo.itemNoteColor.describe()->items.size() == 4, "a color list with default, red, green and blue");
    test::check(bool(demo.itemNote.setValue(u"重要通知內容")) && bool(demo.itemName.setValue(u"Styled")), "write the note");
    auto& host = detail::access(demo.ui);
    demo.itemNote.focus(); demo.itemNote.select(0, 2);
    // A button press moves the focus to the button; invoking it applies the style and returns the focus.
    host.focus("items.dialog.note.bold");
    test::check(bool(demo.itemNoteBold.invoke()), "press the bold button");
    auto spans = [&] { return demo.itemNote.richValue()->spans; };
    test::check(spans() == std::vector<StyleSpan>{{.start = 0, .length = 2, .bold = true}}, "the selection is bold");
    test::check(host.focused() == "items.dialog.note" && host.edit("items.dialog.note")->selection.anchor == 0 && host.edit("items.dialog.note")->selection.caret == 2,
                "the focus is back in the note and the selection is unchanged");
    host.focus("items.dialog.note.italic"); demo.itemNoteItalic.invoke();
    host.focus("items.dialog.note.underline"); demo.itemNoteUnderline.invoke();
    test::check(spans() == std::vector<StyleSpan>{{.start = 0, .length = 2, .bold = true, .italic = true, .underline = true}}, "italic and underline combine with bold");
    host.focus("items.dialog.note.bold"); demo.itemNoteBold.invoke();
    test::check(!spans().empty() && !spans()[0].bold && spans()[0].italic, "a second press removes bold");
    demo.itemNote.select(2, 4);
    host.focus("items.dialog.note.color");
    test::check(bool(demo.itemNoteColor.select({"red"})), "choose red");
    test::check(spans().size() == 2 && spans()[1].start == 2 && spans()[1].length == 2 && spans()[1].color == "red" && host.focused() == "items.dialog.note",
                "the selection is red and the focus is back in the note");
    test::check(bool(demo.itemNoteColor.select({"default"})) && spans().size() == 1, "default returns the selection to the normal color");
    demo.itemNoteColor.select({"green"});
    test::check(bool(demo.itemOk.invoke()) && !*demo.itemDialog.isOpen(), "the item is saved");
    auto document = demo.document();
    test::check(document.items.back().note == u"重要通知內容" && document.items.back().noteSpans.size() == 2 && document.items.back().noteSpans[1].color == "green",
                "the saved item has the note's text and styles");
    test::check(bool(demo.itemsList.select(document.items.back().id)), "select it again");
    demo.itemsList.focus();
    test::check(bool(host.keyDown({.key = Key::enter})) && *demo.itemDialog.isOpen(), "Enter on the list opens the edit dialog");
    test::check(demo.itemNote.richValue()->spans == document.items.back().noteSpans, "editing loads the styles back");
    test::check(!demo.itemNoteColor.describe()->items.empty() && demo.itemNoteColor.draft()->has_value() && (*demo.itemNoteColor.draft())->id == "default", "the color list starts at default for each opening");
}
// The note's named colors stay readable (WCAG AA 4.5:1) on the field's surface in both appearances and both themes.
void noteColorsAreReadableInEveryAppearanceAndTheme() {
    settingsDemo::Demo demo; demo.nav.select({"items"}); demo.itemsAdd.invoke();
    auto colors = demo.itemNote.colors();
    test::check(colors && colors->size() == 3, "red, green and blue");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()})
        for (const auto& variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
            if (!appearance->supports(variant)) continue;
            const auto palette = appearance->palette(variant);
            for (const auto& color : *colors) {
                const auto value = variant == ThemeVariant::dark() ? color.dark : color.light;
                test::check(contrastRatio(value, palette.surface) >= 4.5, "a named note color is readable on the surface");
                test::check(contrastRatio(value, palette.selection) >= 3.0, "...and still distinguishable inside a selection");
            }
        }
}
// #73: the pointer-to-value basis is frozen at press, so a layout that moves under the pointer
// (text size widens the navigation list) cannot make the value wander while the mouse is still.
void sliderDragBasisIsFrozenWhenLayoutMoves() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo; demo.nav.select({"appearance"});
        test::FakeMeasure measure; ScrollState scroll;
        auto& state = detail::access(demo.ui);
        auto frame = [&] { return arrange(demo.page(), demo.ui, *appearance, measure, 960, 720, 1.f, scroll); };
        auto first = frame(); const auto* item = first.find(demo.textSize);
        test::check(item && item->sliderTrack.w > 0, "text size track is arranged");
        const int mouseX = item->sliderTrack.x + item->sliderTrack.w * 9 / 10;
        test::check(bool(state.pointerDown(item->id)), "press text size");
        state.sliderPointerDown(item->id, mouseX, item->sliderTrack);
        const int pressed = *demo.textSize.draft();
        std::vector<int> seen{pressed}; std::vector<int> trackX{item->sliderTrack.x};
        for (int i = 0; i < 6; ++i) {
            auto moved = frame(); const auto* now = moved.find(demo.textSize);
            trackX.push_back(now->sliderTrack.x);
            state.sliderPointer(now->id, mouseX);
            seen.push_back(*demo.textSize.draft());
        }
        test::check(std::any_of(trackX.begin(), trackX.end(), [&](int x) { return x != trackX.front(); }), "the layout does move while dragging");
        for (int v : seen) test::check(v == pressed, "a still mouse keeps the value while the layout moves");
        state.pointerUp(item->id);
    }
}
// What the slider paints, through the same entry the host uses.
struct SliderProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<Rect> thumbs; std::vector<std::u16string> texts;  // thumbs: content-role discs only (the #72 focus dot is not the thumb)
    detail::PaintRole current = detail::PaintRole::content;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect, Color) override {}
    void rounded(Rect rect, int, Color) override { if (current != detail::PaintRole::focusIndicator) thumbs.push_back(rect); }
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color) override { texts.emplace_back(value); }
    int thumbCenter() const { return thumbs.empty() ? -1 : thumbs.back().x + thumbs.back().w / 2; }
    bool titled(std::u16string_view title) const { return std::find(texts.begin(), texts.end(), std::u16string(title)) != texts.end(); }
};
SliderProbe paintSlider(settingsDemo::Demo& demo, const Appearance& appearance, const lcars::SliderHandle& slider, Rect& track) {
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(demo.page(), demo.ui, appearance, measure, 960, 720, 1.f, scroll);
    const auto* item = frame.find(slider); test::check(item != nullptr, "slider is arranged");
    track = item->sliderTrack;
    auto semantic = detail::access(demo.ui).describe(item->id);
    SliderProbe probe(detail::access(demo.ui), *item, *semantic, appearance, 1.f, true);
    detail::componentType(ComponentKind::slider).paint(probe);
    return probe;
}
bool nearPx(int a, int b) { return std::abs(a - b) <= 1; }
// #73: while pressed, the thumb is under the pointer and the value is the snapped one.
void sliderThumbFollowsPointerValueSnaps() {
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        settingsDemo::Demo demo; auto& state = detail::access(demo.ui);
        Rect track{};
        paintSlider(demo, *appearance, demo.volume, track);
        const int x = track.x + static_cast<int>(track.w * 0.47);
        test::check(bool(state.pointerDown("general.volume")) && bool(state.sliderPointerDown("general.volume", x, track)), "press volume at 47%");
        auto held = paintSlider(demo, *appearance, demo.volume, track);
        test::check(nearPx(held.thumbCenter(), x), "thumb is under the pointer, not on the 45 tick");
        test::check(held.titled(u"音量: 45"), "title shows the snapped value");
        test::check(*demo.volume.draft() == 45 && demo.ui.semantics().get({"general.volume"})->value == u"45", "semantic value is 45 while held");
        test::check(bool(state.sliderPointer("general.volume", x - 40)) && nearPx(paintSlider(demo, *appearance, demo.volume, track).thumbCenter(), x - 40), "thumb keeps following");
        state.sliderPointer("general.volume", x); state.pointerUp("general.volume");
        auto released = paintSlider(demo, *appearance, demo.volume, track);
        test::check(nearPx(released.thumbCenter(), track.x + static_cast<int>(std::lround(track.w * 0.45))), "release snaps the thumb to the 45 tick");
        test::check(!state.sliderDragX("general.volume"), "no drag display position after release");
    }
}
void sliderThumbStopsAtTrackEnds() {
    settingsDemo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    paintSlider(demo, webAppearance(), demo.volume, track);
    state.pointerDown("general.volume"); state.sliderPointerDown("general.volume", track.x + 10, track);
    state.sliderPointer("general.volume", track.x - 80);
    auto left = paintSlider(demo, webAppearance(), demo.volume, track);
    test::check(*demo.volume.draft() == 0 && nearPx(left.thumbCenter(), track.x), "beyond the left end: minimum, thumb at the end");
    state.sliderPointer("general.volume", track.x + track.w + 80);
    auto right = paintSlider(demo, webAppearance(), demo.volume, track);
    test::check(*demo.volume.draft() == 100 && nearPx(right.thumbCenter(), track.x + track.w), "beyond the right end: maximum, thumb at the end");
    state.pointerUp("general.volume");
}
void sliderInterruptedDragReturnsToTick() {
    settingsDemo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    paintSlider(demo, webAppearance(), demo.volume, track);
    const int x = track.x + static_cast<int>(track.w * 0.47);
    state.pointerDown("general.volume"); state.sliderPointerDown("general.volume", x, track);
    state.cancelInput();
    auto after = paintSlider(demo, webAppearance(), demo.volume, track);
    test::check(*demo.volume.draft() == 45, "the last snapped value is kept");
    test::check(nearPx(after.thumbCenter(), track.x + static_cast<int>(std::lround(track.w * 0.45))), "an interrupted drag puts the thumb on the value's tick");
    test::check(!bool(state.sliderPointer("general.volume", x + 100)), "an interrupted drag takes no more pointer moves");
}
void sliderDragIsOneWritePerValueAndOneUndo() {
    settingsDemo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    paintSlider(demo, webAppearance(), demo.volume, track);
    int writes = 0; std::vector<int> values;
    Ui ui; auto bare = ui.addSlider({.id = "v", .label = u"V"}, {0, 100, 5}).value.value();
    int model = 40; bare.connectSource({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    bare.connectImmediate([&](const Change<SliderHandle>& c) { ++writes; values.push_back(c.value); model = c.value; return Result{}; });
    auto& bareState = detail::access(ui); const Rect bareTrack{100, 0, 200, 20};
    bareState.pointerDown("v"); bareState.sliderPointerDown("v", 100 + 94, bareTrack);
    for (int x : {93, 94, 92, 91}) bareState.sliderPointer("v", 100 + x);
    bareState.pointerUp("v");
    test::check(writes == 1 && values == std::vector<int>{45}, "repeated moves inside one step write once");
    const int before = *demo.volume.draft();
    state.pointerDown("general.volume"); state.sliderPointerDown("general.volume", track.x, track);
    for (double r : {0.2, 0.5, 0.7, 0.9, 0.3}) state.sliderPointer("general.volume", track.x + static_cast<int>(track.w * r));
    state.sliderPointer("general.volume", track.x + static_cast<int>(track.w * 0.47)); state.pointerUp("general.volume");
    test::check(*demo.volume.draft() == 45, "drag ended on 45");
    test::check(bool(state.keyDown({.ctrl = true, .letter = u'Z'})) && *demo.volume.draft() == before, "one undo restores the value before the whole drag");
}
// #75: Ctrl+Z / Ctrl+Y restore the value through the application, so the thumb lands at once,
// while the user's own arrow key on the same slider slides.
void sliderUndoRedoLandAtOnce() {
    settingsDemo::Demo demo; auto& state = detail::access(demo.ui); Rect track{};
    std::uint64_t now = 0; state.animationTime([&] { return now; });
    paintSlider(demo, webAppearance(), demo.volume, track);
    auto tick = [&](int value) { return track.x + static_cast<int>(std::lround(track.w * value / 100.0)); };
    auto thumb = [&] { return paintSlider(demo, webAppearance(), demo.volume, track).thumbCenter(); };
    test::check(bool(demo.volume.focus()) && bool(state.keyDown({.key = Key::right})), "arrow right on the focused volume");
    test::check(*demo.volume.draft() == 45 && nearPx(thumb(), tick(40)), "the arrow key slides: 45 now, thumb still on 40");
    now = 150;
    test::check(nearPx(thumb(), tick(45)) && !demo.ui.animating().value.value(), "the slide lands on 45");
    test::check(bool(state.keyDown({.ctrl = true, .letter = u'Z'})) && *demo.volume.draft() == 40, "undo restores 40");
    test::check(nearPx(thumb(), tick(40)) && !demo.ui.animating().value.value(), "undo: the thumb is on 40 at once");
    test::check(bool(state.keyDown({.ctrl = true, .letter = u'Y'})) && *demo.volume.draft() == 45, "redo restores 45");
    test::check(nearPx(thumb(), tick(45)) && !demo.ui.animating().value.value(), "redo: the thumb is on 45 at once");
}

// Right-clicking a row of the Items page: the row is selected, and the menu's Edit and Delete are the
// buttons' own actions, so they match the Enter / Del shortcuts exactly.
void itemsContextMenuActsOnTheClickedRow() {
    settingsDemo::Demo demo;
    auto& core = detail::access(demo.ui);
    test::check(bool(demo.nav.select({"items"})), "open items page");
    auto rowOf = [&](const char* id) {
        auto options = demo.itemsList.describe()->items;
        auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.value.id == id; });
        return static_cast<std::size_t>(it - options.begin());
    };
    auto hasMenuVerb = [&](const char* id) {
        auto verbs = demo.ui.semantics().get({id})->verbs;
        return std::find(verbs.begin(), verbs.end(), Verb::contextMenu) != verbs.end();
    };
    test::check(hasMenuVerb("items.search") && hasMenuVerb("items.list") && !hasMenuVerb("items.add"),
                "the search field has the built-in menu, the list has the demo's, buttons have none");
    test::check(bool(core.menuPointerOpen("items.list", std::nullopt, std::nullopt, {10, 10})), "right button on empty space opens the list menu");
    auto menu = demo.ui.contextMenu();
    test::check(menu && menu->items.size() == 2 && menu->items[0].id == "items.edit" && menu->items[0].label == u"編輯(E)" && menu->items[0].shortcutHint == u"Enter" &&
                menu->items[1].id == "items.delete" && menu->items[1].label == u"刪除(D)" && menu->items[1].shortcutHint == u"Del" &&
                menu->items[0].accessKey == u'E' && menu->items[1].accessKey == u'D', "Edit(E) and Delete(D), with their shortcut hints");
    test::check(!menu->items[0].enabled && !menu->items[1].enabled, "both are off while nothing is selected, like the buttons");
    demo.ui.closeContextMenu();
    // Delete the clicked row, not the previous selection.
    test::check(bool(demo.itemsList.select({"T001"})), "a different row is selected first");
    test::check(bool(core.menuPointerOpen("items.list", rowOf("T003"), std::nullopt, {10, 10})), "right button on row T003");
    test::check(demo.itemsList.describe()->items[rowOf("T003")].selected && !demo.itemsList.describe()->items[rowOf("T001")].selected, "the clicked row is selected");
    menu = demo.ui.contextMenu();
    test::check(menu->items[0].enabled && menu->items[1].enabled, "Edit and Delete are on");
    test::check(bool(demo.ui.invokeMenuItem("items.delete")) && *demo.deleteDialog.isOpen(), "Delete opens the confirmation");
    const auto byMenu = demo.ui.semantics().get({"items.delete_dialog.message"})->value;
    test::check(byMenu.find(u"範本 03") != std::u16string::npos, "it names the clicked row");
    core.keyDown({.key = Key::escape});
    test::check(!*demo.deleteDialog.isOpen() && core.focused() == "items.list", "cancelling returns focus to the list");
    test::check(bool(core.keyDown({.key = Key::del})) && *demo.deleteDialog.isOpen() && demo.ui.semantics().get({"items.delete_dialog.message"})->value == byMenu,
                "the Del key opens the very same confirmation");
    core.keyDown({.key = Key::tab}); core.keyDown({.key = Key::space}); core.keyUp(Key::space);
    test::check(!*demo.deleteDialog.isOpen() && rowOf("T003") == demo.itemsList.describe()->items.size() && demo.itemsList.describe()->items.size() == 19,
                "confirming deletes row T003");
    // Edit through the menu with the keyboard: the menu key, then the quick key.
    test::check(bool(core.menuPointerOpen("items.list", rowOf("T005"), std::nullopt, {10, 10})), "right button on row T005");
    core.textInput(u"e");
    test::check(*demo.itemDialog.isOpen() && demo.itemName.draft().value.value() == u"共用範本", "the quick key E opens Edit on that row");
    demo.itemDialog.settle(false);
    // The menu key on the focused list hangs the menu from the current row.
    demo.itemsList.focus();
    test::check(bool(core.keyDown({.key = Key::menu})) && *demo.ui.contextMenuOpen(), "the menu key opens the list menu");
    core.keyDown({.key = Key::down}); core.keyDown({.key = Key::down});
    test::check(bool(core.keyDown({.key = Key::enter})) && *demo.deleteDialog.isOpen(), "Down, Down, Enter runs Delete(D)");
}

int main() { const test::Case cases[] = {{"items context menu", itemsContextMenuActsOnTheClickedRow}, {"focus ring controls are on the appearance page with their de", focusRingControlsAreOnTheAppearancePageWithTheirDefaults}, {"the focus ring controls apply to the presentation", theFocusRingControlsApplyToThePresentation}, {"a document that cannot be applied reports it", aDocumentThatCannotBeAppliedReportsTheFailure}, {"unchecking same-as-component starts at the appearance radius", uncheckingSameAsTheComponentStartsFromTheAppearancesOwnRadius}, {"the indicator off disables the other three and keeps their v", theIndicatorOffDisablesTheOtherThreeAndKeepsTheirValues}, {"ring colors pass three to one everywhere and follow the acce", ringColorsPassThreeToOneEverywhereAndFollowTheAccentByDefault}, {"ring colors paint the focus indicator", ringColorsPaintTheFocusIndicator}, {"the focus ring settings are history saved state and reset", theFocusRingSettingsAreHistorySavedStateAndReset}, {"undo while a ring slider has focus starts a new step", undoWhileARingSliderKeepsTheFocusStartsANewHistoryStep}, {"loading a document applies the focus ring settings", loadingADocumentAppliesTheFocusRingSettings}, {"slider undo redo land at once", sliderUndoRedoLandAtOnce}, {"item note multi-line", itemNoteIsAMultilineField}, {"item note rich", itemNoteIsRichWithAStyleBar}, {"note colors readable", noteColorsAreReadableInEveryAppearanceAndTheme}, {"slider thumb follows pointer", sliderThumbFollowsPointerValueSnaps}, {"slider thumb track ends", sliderThumbStopsAtTrackEnds}, {"slider interrupted drag", sliderInterruptedDragReturnsToTick}, {"slider one write and one undo", sliderDragIsOneWritePerValueAndOneUndo}, {"slider drag frozen basis", sliderDragBasisIsFrozenWhenLayoutMoves}, {"failure draft save", failureDraftSave}, {"failure choices", failureChoices}, {"close invalid name", closeInvalidName}, {"failure retry flow", failureRetryFlow}, {"close save and layout", closeSaveAndLayout}, {"close item draft", closeItemDraft}, {"close dialog flow", closeDialogFlow}, {"appearance interleaved history", appearanceInterleavedHistory}, {"appearance missing font", appearanceMissingFont}, {"appearance font", appearanceFontHistory}, {"appearance toggle and reset", appearanceToggleReset}, {"HS08 appearance size", appearanceSizeHistory}, {"HS01 counted steps", hs01StepMatrix}, {"HS01 non-history", hs01NonHistoryMatrix}, {"IT03 no-op edit history", it03UnchangedEditHasNoHistory}, {"HS03 value and saved state", hs03ValueEqualityAndSavedName}, {"HS02 full sequence", hs02UndoRedoSequence}, {"history toggle undo", historyToggleUndo}, {"history save", historySaveRevert}, {"history draft", historyDraftDirty}, {"history slider", historySliderGroup}, {"history title", historyTitleNotification}, {"history invalid", historyInvalidDraft}, {"history invalid save", historyInvalidSave}, {"history save name", historySaveFocusedName}, {"history revert", historyRevertIsUndoable}, {"history items", historyItems}, {"history presentation", historyPresentation}, {"history limit", historyLimitAndBranch}, {"history field", historyFieldShortcuts}, {"history delete", historyDeleteRestore}, {"history repeat", historyKeyRepeat}, {"About keyboard scroll", aboutKeyboardScroll}, {"TD01 failure diagnostic", td01FailureDiagnostic}, {"About licenses", aboutLicenses}, {"demo text flow", demoTextFlow}, {"About page content", aboutPageContent}, {"About vertical label", aboutVerticalLabel}, {"settings demo", exercise}, {"IT01 search count and empty state", itemSearchCountAndEmptyState}, {"semantic navigation pages", semanticNavigationPages}, {"semantic Items dialogs", semanticItemsAndDialogs}, {"accent choices pass contrast", accentChoicesPassContrastEverywhere}, {"accent repaints page", accentChoiceRepaintsThePageInTheChosenColor}, {"accent history and reset", accentIsHistorySavedStateAndReset}, {"accent loaded from document", loadingADocumentAppliesItsAccent}, {"accent tab order", accentFollowsTheExistingAppearanceControlsInTabOrder}, {"justify-left checkbox", theJustifyLeftCheckboxSwitchesThePresentationSetting}, {"dark newer components", darkNewComponentSweep}, {"compact newer components", compactNewComponentLayout}}; return test::run("settings demo", cases); }
