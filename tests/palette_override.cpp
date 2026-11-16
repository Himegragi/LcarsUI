// Palette overrides for the built-in appearances (#78): behavior through Ui, SemanticTree and the paint seam.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&webAppearance(), &materialAppearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string name_of(const Appearance& a) { return std::string(a.name()); }
// Ui::effectivePalette settles an empty `focus` to the accent; a palette straight from an appearance has not been settled yet.
Palette settled(Palette p) { p.focus = p.focusColor(); return p; }
Color gray(unsigned char v) { return {v, v, v}; }
PaletteOverride entry(const Appearance& a, ThemeVariant v, std::string role, PaletteValue value) {
    return {name_of(a), std::move(v), std::move(role), std::move(value)};
}
bool contains(const std::u16string& text, std::u16string_view part) { return text.find(part) != std::u16string::npos; }
const Color green{20, 120, 60};

struct ColorProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<Color> colors;
    void fill(Rect, Color c) override { colors.push_back(c); }
    void rounded(Rect, int, Color c) override { colors.push_back(c); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color c) override { colors.push_back(c); }
    bool has(Color c) const { return std::any_of(colors.begin(), colors.end(), [c](Color x) { return x == c; }); }
};
// Paints every placement of a checked checkbox page with the appearance and the Ui's current variant.
std::vector<Color> paint_colors(Ui& ui, const Appearance& appearance, const CheckboxHandle& box) {
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::column({layout::component(box)}), ui, appearance, measure, 400, 300, 1.f, scroll);
    auto& state = detail::access(ui);
    std::vector<Color> all;
    for (const auto& item : frame.items) {
        auto semantics = state.describe(item.id);
        ColorProbe probe(state, item, *semantics, appearance, 1.f, true);
        detail::component_type(semantics->kind).paint(probe);
        all.insert(all.end(), probe.colors.begin(), probe.colors.end());
    }
    return all;
}
// Not std::find: Color is 3 bytes, and MSVC 14.51 sends std::find to a vectorized path that static_asserts on that size.
bool has(const std::vector<Color>& colors, Color c) { return std::any_of(colors.begin(), colors.end(), [c](Color x) { return x == c; }); }

void contrast_ratio_matches_wcag_reference_values() {
    check(std::fabs(contrastRatio({0, 0, 0}, {255, 255, 255}) - 21.0) < 0.001, "black on white is 21:1");
    check(std::fabs(contrastRatio({255, 255, 255}, {0, 0, 0}) - 21.0) < 0.001, "ratio is symmetric");
    check(std::fabs(contrastRatio(gray(0x76), gray(255)) - 4.54) < 0.005, "#767676 on white is 4.54:1");
    check(std::fabs(contrastRatio(gray(0x77), gray(255)) - 4.48) < 0.005, "#777777 on white is 4.48:1");
    check(std::fabs(contrastRatio(gray(0x94), gray(255)) - 3.03) < 0.005, "#949494 on white is 3.03:1");
    check(std::fabs(contrastRatio(gray(0x40), gray(0x40)) - 1.0) < 1e-9, "equal colors are 1:1");
}

void builtin_palettes_pass_the_contrast_rules() {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants)
            check(checkPaletteContrast(appearance->palette(variant)).empty(), (name_of(*appearance) + " " + variant.name + " passes").c_str());
}

// The ticket's proposed values (literals) and the WCAG 1.4.11 floor, independent of checkPaletteContrast.
void builtin_outline_meets_three_to_one_on_every_surface() {
    struct Expected { const Appearance* appearance; ThemeVariant variant; Color outline; };
    const Expected expected[] = {
        {&webAppearance(), ThemeVariant::light(), {118, 128, 145}}, {&webAppearance(), ThemeVariant::dark(), {128, 138, 156}},
        {&materialAppearance(), ThemeVariant::light(), {121, 116, 126}}, {&materialAppearance(), ThemeVariant::dark(), {147, 143, 153}},
    };
    for (const auto& e : expected) {
        auto p = e.appearance->palette(e.variant);
        auto label = name_of(*e.appearance) + " " + e.variant.name;
        check(p.outline == e.outline, (label + " outline value").c_str());
        for (auto [name, surface] : {std::pair{"surface", p.surface}, {"background", p.background}, {"panel", p.panel}})
            check(contrastRatio(p.outline, surface) >= 3.0, (label + " outline on " + name + " >= 3:1").c_str());
        check(contrastRatio(p.border, p.surface) < 3.0, (label + " border stays a light divider").c_str());
    }
}

void without_overrides_every_palette_is_the_builtin_value() {
    Ui ui;
    check(ui.paletteOverrides().empty(), "no overrides initially");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto effective = ui.effectivePalette(*appearance, variant);
            check(effective && *effective == settled(appearance->palette(variant)), "effective palette equals the built-in palette");
        }
}

void overriding_web_light_accent_changes_only_that_combination() {
    Ui ui;
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", green)})), "override accepted");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto expected = appearance->palette(variant);
            if (appearance == &webAppearance() && variant == ThemeVariant::light()) expected.accent = green;
            auto effective = ui.effectivePalette(*appearance, variant);
            check(effective && *effective == settled(expected), (name_of(*appearance) + " " + variant.name + " effective palette").c_str());
        }
    check(ui.paletteOverrides().size() == 1 && ui.paletteOverrides()[0].role == "accent", "override list is readable");
}

// One driver over every palette role: each override changes exactly its own field.
void every_palette_role_is_overridable_and_isolated() {
    auto roles = paletteRoles();
    check(roles.size() == 16 && std::find(roles.begin(), roles.end(), "outline") != roles.end() && std::find(roles.begin(), roles.end(), "focus") != roles.end(), "eleven core colors (incl. outline), scrim, hover, focus and the scrim_alpha and hover_alpha values are roles");
    check(std::find(roles.begin(), roles.end(), "scrim_alpha") != roles.end() && std::find(roles.begin(), roles.end(), "border") != roles.end(), "known role names present");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants)
            for (auto role : roles) {
                auto base = appearance->palette(variant);
                auto original = *paletteValue(base, role);
                // Nudge one channel by one step: distinct from the built-in value, contrast rules keep passing.
                PaletteValue changed = std::holds_alternative<int>(original) ? PaletteValue{std::get<int>(original) + 1}
                    : PaletteValue{Color{static_cast<unsigned char>(std::get<Color>(original).r == 255 ? 254 : std::get<Color>(original).r + 1), std::get<Color>(original).g, std::get<Color>(original).b}};
                Ui ui;
                auto result = ui.setPaletteOverrides({entry(*appearance, variant, std::string(role), changed)});
                auto label = name_of(*appearance) + " " + variant.name + " " + std::string(role);
                check(bool(result), (label + " accepted").c_str());
                auto effective = ui.effectivePalette(*appearance, variant);
                check(effective.value.has_value(), (label + " readable").c_str());
                for (auto other : roles) {
                    const bool follows = role == "accent" && other == "focus";  // the ring follows a replaced accent unless focus is replaced itself
                    auto expected = other == role || follows ? changed : *paletteValue(base, other);
                    check(*paletteValue(*effective, other) == expected, (label + " field " + std::string(other)).c_str());
                }
                // The same list leaves every other appearance and variant at its built-in value.
                for (const auto* other_appearance : builtins)
                    for (const auto& other_variant : variants)
                        if (other_appearance != appearance || !(other_variant == variant))
                            check(*ui.effectivePalette(*other_appearance, other_variant) == settled(other_appearance->palette(other_variant)), (label + " leaves others untouched").c_str());
            }
}

void role_helpers_reject_unknown_roles_and_wrong_kinds() {
    Palette p = webAppearance().palette();
    check(!paletteValue(p, "glow").has_value(), "unknown role has no value");
    check(!setPaletteValue(p, "glow", Color{1, 2, 3}), "unknown role is refused");
    check(!setPaletteValue(p, "accent", 7), "a color role needs a Color");
    check(!setPaletteValue(p, "scrim_alpha", Color{1, 2, 3}), "scrim_alpha needs an int");
    check(!setPaletteValue(p, "scrim_alpha", 256) && !setPaletteValue(p, "scrim_alpha", -1), "scrim_alpha is 0-255");
    check(setPaletteValue(p, "scrim_alpha", 255) && p.scrimAlpha == 255, "scrim_alpha 255 is accepted");
    check(setPaletteValue(p, "scrim_alpha", 0) && p.scrimAlpha == 0, "scrim_alpha 0 is accepted");
    check(p == [&] { auto q = webAppearance().palette(); q.scrimAlpha = 0; return q; }(), "only scrim_alpha changed");
}

// hover_off: the case sets hoverAlpha to 0 so only the un-hovered rule decides (the hovered rows leave the default overlay on).
struct ContrastCase { const char* pair; std::vector<std::pair<std::string, Color>> passing, failing; bool hover_off = false; };
// Web light. "passing" sits just above the threshold (4.54:1 or 3.03:1), "failing" just below it.
std::vector<ContrastCase> contrast_cases() {
    std::vector<std::pair<std::string, Color>> surfaces{{"background", gray(255)}, {"panel", gray(255)}, {"surface", gray(255)}, {"selection", gray(255)}, {"text", gray(0x76)}};
    auto with = [&](std::string role, Color c) { auto list = surfaces; for (auto& e : list) if (e.first == role) e.second = c; return list; };
    std::vector<std::pair<std::string, Color>> outlined{{"outline", gray(0x94)}, {"background", gray(255)}, {"panel", gray(255)}, {"surface", gray(255)}};
    auto with_outline = [&](std::string role, Color c) { auto list = outlined; for (auto& e : list) if (e.first == role) e.second = c; return list; };
    auto hovered_outline = outlined;
    for (auto& e : hovered_outline) if (e.first == "outline") e.second = gray(0x8A);
    auto with_hovered = [&](std::string role, Color c) { auto list = hovered_outline; for (auto& e : list) if (e.first == role) e.second = c; return list; };
    std::vector<std::pair<std::string, Color>> ringed{{"focus", gray(0x94)}, {"background", gray(255)}, {"panel", gray(255)}, {"surface", gray(255)}};
    auto with_ring = [&](std::string role, Color c) { auto list = ringed; for (auto& e : list) if (e.first == role) e.second = c; return list; };
    return {
        {"text/background", surfaces, with("background", gray(250))},
        {"text/panel", surfaces, with("panel", gray(250))},
        {"text/surface", surfaces, with("surface", gray(250))},
        {"text/selection", surfaces, with("selection", gray(250))},
        {"muted/surface", {{"muted", gray(0x76)}}, {{"muted", gray(0x77)}}},
        {"error/surface", {{"error", gray(0x76)}}, {{"error", gray(0x77)}}},
        {"on_accent/accent", {{"accent", gray(0x76)}, {"on_accent", gray(255)}}, {{"accent", gray(0x77)}, {"on_accent", gray(255)}}},
        // The ring follows a replaced accent, so the page background is lightened to white too: the case is about accent/surface only.
        {"accent/surface", {{"accent", gray(0x94)}, {"on_accent", gray(0)}, {"background", gray(255)}}, {{"accent", gray(0x95)}, {"on_accent", gray(0)}, {"background", gray(255)}}},
        // outline: 3:1 (#949494 on white is 3.03:1) on each of surface, background and panel; the surfaces are lightened to white so only the one varied surface decides.
        // focus (the ring): 3:1 on each of surface, background and panel, same literals as outline (#949494 is 3.03:1 on white, 2.91:1 on 250).
        {"focus/surface", ringed, with_ring("surface", gray(250))},
        {"focus/background", ringed, with_ring("background", gray(250))},
        {"focus/panel", ringed, with_ring("panel", gray(250))},
        {"outline/surface", outlined, with_outline("surface", gray(250)), true},
        {"outline/background", outlined, with_outline("background", gray(250)), true},
        {"outline/panel", outlined, with_outline("panel", gray(250)), true},
        // outline on the hovered surfaces (Web light hover = 6% black, so white becomes 240): #8A8A8A is 3.03:1 on 240, and a surface of 250 hovers to 235 (2.90:1) while staying above 3:1 un-hovered.
        {"outline/hovered surface", hovered_outline, with_hovered("surface", gray(250))},
        {"outline/hovered background", hovered_outline, with_hovered("background", gray(250))},
        {"outline/hovered panel", hovered_outline, with_hovered("panel", gray(250))},
    };
}
std::vector<PaletteOverride> web_light(const std::vector<std::pair<std::string, Color>>& list, bool hover_off = false) {
    std::vector<PaletteOverride> out;
    for (const auto& [role, color] : list) out.push_back(entry(webAppearance(), ThemeVariant::light(), role, color));
    if (hover_off) out.push_back(entry(webAppearance(), ThemeVariant::light(), "hover_alpha", 0));
    return out;
}
void each_contrast_rule_has_a_passing_and_a_failing_case() {
    for (const auto& c : contrast_cases()) {
        Ui ui;
        auto good = ui.setPaletteOverrides(web_light(c.passing, c.hover_off));
        check(bool(good), (std::string(c.pair) + " just-passing case is accepted").c_str());
        auto before = ui.paletteOverrides();
        auto bad = ui.setPaletteOverrides(web_light(c.failing, c.hover_off));
        check(bad.status == Status::invalidValue, (std::string(c.pair) + " just-failing case is rejected").c_str());
        std::u16string pair(c.pair, c.pair + std::string(c.pair).size());
        check(contains(bad.message, pair), (std::string(c.pair) + " message names the pair").c_str());
        check(contains(bad.message, u"Minimal Web") && contains(bad.message, u"light"), "message names appearance and variant");
        check(contains(bad.message, u"needs") && contains(bad.message, u":1"), "message carries the ratio");
        check(ui.paletteOverrides() == before, (std::string(c.pair) + " rejected list leaves the active list unchanged").c_str());
        auto effective = ui.effectivePalette(webAppearance(), ThemeVariant::light());
        check(effective && *effective != webAppearance().palette(ThemeVariant::light()), "active overrides still apply");
    }
}
void rejection_message_reports_the_actual_ratio() {
    Ui ui;
    auto bad = ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "muted", gray(0x77))});
    check(bad.status == Status::invalidValue && contains(bad.message, u"4.47:1"), "#777777 on white reports 4.47:1 (truncated)");
    Ui ui2;
    auto near_surface = ui2.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "text", Color{250, 250, 250})});
    check(near_surface.status == Status::invalidValue && contains(near_surface.message, u"text/surface") && contains(near_surface.message, u"text/panel"), "a text color close to the surfaces fails every surface pair");
}

void invalid_entries_are_rejected_and_keep_the_active_list() {
    Ui ui;
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", green)})), "baseline");
    auto active = ui.paletteOverrides();
    auto bad = [&](std::vector<PaletteOverride> list, const char* label) {
        auto r = ui.setPaletteOverrides(std::move(list));
        check(r.status == Status::invalidValue, label);
        check(ui.paletteOverrides() == active, label);
    };
    bad({{"No Such Appearance", ThemeVariant::light(), "accent", green}}, "unknown appearance name");
    bad({entry(webAppearance(), ThemeVariant::highContrast(), "accent", green)}, "unsupported variant");
    bad({entry(webAppearance(), ThemeVariant{"sepia"}, "accent", green)}, "unknown variant");
    bad({entry(webAppearance(), ThemeVariant::light(), "glow", green)}, "unknown role");
    bad({entry(webAppearance(), ThemeVariant::light(), "scrim_alpha", 256)}, "scrim_alpha 256");
    bad({entry(webAppearance(), ThemeVariant::light(), "scrim_alpha", -1)}, "scrim_alpha -1");
    bad({entry(webAppearance(), ThemeVariant::light(), "accent", 5)}, "int for a color role");
    bad({entry(webAppearance(), ThemeVariant::light(), "scrim_alpha", green)}, "color for scrim_alpha");
    bad({entry(webAppearance(), ThemeVariant::light(), "accent", green), entry(webAppearance(), ThemeVariant::light(), "accent", gray(10))}, "duplicate entry");
    // The valid first entry of a partly invalid list is not applied either.
    bad({entry(webAppearance(), ThemeVariant::dark(), "accent", Color{250, 200, 100}), entry(webAppearance(), ThemeVariant::light(), "glow", green)}, "all or nothing");
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "scrim_alpha", 255), entry(materialAppearance(), ThemeVariant::dark(), "scrim_alpha", 0)})), "scrim_alpha bounds 0 and 255 are accepted");
}

void replacing_and_clearing_the_list_takes_effect_immediately() {
    Ui ui;
    int invalidations = 0; detail::access(ui).on_invalidate([&] { ++invalidations; });
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", green)})) && invalidations == 1, "setting requests a repaint");
    auto orange = Color{150, 70, 0};
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", orange)})) && invalidations == 2, "replacing requests a repaint");
    check(ui.effectivePalette(webAppearance(), ThemeVariant::light())->accent == orange, "whole list is replaced, not merged");
    check(bool(ui.setPaletteOverrides({})) && invalidations == 3, "clearing requests a repaint");
    check(ui.paletteOverrides().empty() && *ui.effectivePalette(webAppearance(), ThemeVariant::light()) == settled(webAppearance().palette()), "cleared list restores built-in values");
    check(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "glow", green)}).status == Status::invalidValue && invalidations == 3, "a rejected list does not repaint");
}

void off_thread_calls_report_wrong_thread() {
    Ui ui; Status status = Status::ok;
    std::thread([&] { status = ui.setPaletteOverrides({}).status; }).join();
    check(status == Status::wrongThread, "setting needs the UI thread");
}

void a_custom_appearance_is_never_overridden() {
    struct Custom final : Appearance {
        std::string_view name() const override { return "Minimal Web"; }  // same name, different object
        Palette palette() const override { return webAppearance().palette(); }
        int radius() const override { return 6; }
        TextFieldLayout arrangeTextField(int w, const TextFieldHeights& t, float s) const override { return webAppearance().arrangeTextField(w, t, s); }
        int buttonHeight(int l, float s) const override { return webAppearance().buttonHeight(l, s); }
        int buttonPadding(float s) const override { return webAppearance().buttonPadding(s); }
    } custom;
    Ui ui;
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", green)})), "override accepted");
    auto effective = ui.effectivePalette(custom, ThemeVariant::light());
    check(effective && *effective == settled(custom.palette()), "a custom appearance keeps its own palette even when it reuses a built-in name");
}

void painting_uses_the_overridden_colors_for_the_current_combination() {
    Ui ui;
    auto box = ui.addCheckbox({.id = "box", .label = u"Box"}).value.value();
    box.connectSource({[] { return true; }, [](std::function<void()>) { return [] {}; }});  // a checked box
    auto web_accent = webAppearance().palette().accent, web_dark_accent = webAppearance().palette(ThemeVariant::dark()).accent;
    auto material_accent = materialAppearance().palette().accent;
    check(has(paint_colors(ui, webAppearance(), box), web_accent), "built-in accent paints without overrides");
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", green)})), "override accepted");
    auto light = paint_colors(ui, webAppearance(), box);
    check(has(light, green) && !has(light, web_accent), "web light paints green instead of the built-in accent");
    check(has(paint_colors(ui, materialAppearance(), box), material_accent), "material light is unchanged");
    auto settings = ui.presentation(); settings.variant = ThemeVariant::dark();
    check(bool(ui.setPresentation(settings)), "switch to dark");
    auto dark = paint_colors(ui, webAppearance(), box);
    check(has(dark, web_dark_accent) && !has(dark, green), "web dark keeps its built-in accent");
    settings.variant = ThemeVariant::light(); check(bool(ui.setPresentation(settings)), "back to light");
    check(has(paint_colors(ui, webAppearance(), box), green), "light shows the override again");
    check(bool(ui.setPaletteOverrides({})), "clear");
    check(has(paint_colors(ui, webAppearance(), box), web_accent), "clearing restores the built-in accent on the next frame");
}

// Records which color each rect gets, so the field scrollbar (track and thumb) can be told apart from other fills.
struct RectProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<std::pair<Rect, Color>> shapes;
    void fill(Rect r, Color c) override { shapes.push_back({r, c}); }
    void rounded(Rect r, int, Color c) override { shapes.push_back({r, c}); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color c) override { shapes.push_back({{}, c}); }
    bool colored(Color c) const { return std::any_of(shapes.begin(), shapes.end(), [&](const auto& s) { return s.second == c; }); }
    bool rect_has(Rect r, Color c) const { return std::any_of(shapes.begin(), shapes.end(), [&](const auto& s) { return s.first.x == r.x && s.first.y == r.y && s.first.w == r.w && s.first.h == r.h && s.second == c; }); }
};

void a_multiline_field_paints_with_the_overridden_palette() {
    Ui ui;
    auto field = ui.addTextField({.id = "note", .value = u"1\n2\n3\n4\n5\n6", .multiline = true, .visibleLines = 2}).value.value();
    detail::access(ui).focus("note");
    const auto& web = webAppearance();
    const Color track{245, 240, 250}, thumb{200, 30, 120};
    auto paint = [&] {
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::component(field), ui, web, measure, 400, 300, 1.f, scroll);
        auto& state = detail::access(ui);
        const auto& item = frame.items.front();
        auto semantics = state.describe(item.id);
        auto probe = std::make_unique<RectProbe>(state, item, *semantics, web, 1.f, true);
        detail::component_type(semantics->kind).paint(*probe);
        return std::make_pair(std::move(probe), item.view);
    };
    auto [before, view] = paint();
    check(view.thumb.h > 0, "the content overflows, so the field draws its own scrollbar");
    check(before->rect_has(view.track, web.palette().panel) && before->rect_has(view.thumb, web.palette().outline), "without overrides the scrollbar uses the built-in panel and outline colors (#86)");
    check(before->colored(web.palette().accent), "without overrides the focused field draws the built-in accent");
    check(bool(ui.setPaletteOverrides({entry(web, ThemeVariant::light(), "panel", track), entry(web, ThemeVariant::light(), "outline", thumb), entry(web, ThemeVariant::light(), "accent", green)})), "overrides accepted");
    auto [after, overridden] = paint();
    check(after->rect_has(overridden.track, track) && !after->rect_has(overridden.track, web.palette().panel), "the scrollbar track follows the panel override");
    check(after->rect_has(overridden.thumb, thumb) && !after->rect_has(overridden.thumb, web.palette().outline), "the scrollbar thumb follows the outline override");
    check(after->colored(green) && !after->colored(web.palette().accent), "the focused field's accent (caret and edge) follows the accent override");
}

void the_semantic_layer_reads_the_effective_palette_read_only() {
    Ui ui;
    check(ui.semantics().effectivePalette().status == Status::notFound, "no appearance bound yet");
    ui.bindAppearance(&webAppearance());
    check(bool(ui.setPaletteOverrides({entry(webAppearance(), ThemeVariant::light(), "accent", green), entry(webAppearance(), ThemeVariant::dark(), "accent", Color{250, 200, 100})})), "overrides accepted");
    auto light = ui.semantics().effectivePalette();
    check(light && *light == *ui.effectivePalette(webAppearance(), ThemeVariant::light()) && light->accent == green, "semantic palette equals the typed palette (light)");
    auto settings = ui.presentation(); settings.variant = ThemeVariant::dark();
    check(bool(ui.setPresentation(settings)), "switch to dark");
    auto dark = ui.semantics().effectivePalette();
    check(dark && *dark == *ui.effectivePalette(webAppearance(), ThemeVariant::dark()) && dark->accent == (Color{250, 200, 100}), "semantic palette follows the variant");
    ui.bindAppearance(&materialAppearance());
    check(*ui.semantics().effectivePalette() == settled(materialAppearance().palette(ThemeVariant::dark())), "semantic palette follows the bound appearance");
    ui.bindAppearance(nullptr);
    check(ui.semantics().effectivePalette().status == Status::notFound, "unbound again");
    Status status = Status::ok;
    ui.bindAppearance(&webAppearance());
    std::thread([&] { status = ui.semantics().effectivePalette().status; }).join();
    check(status == Status::wrongThread, "the semantic read needs the UI thread");
}
}
int main() {
    static const test::Case cases[] = {
        {"contrast_ratio_matches_wcag_reference_values", contrast_ratio_matches_wcag_reference_values},
        {"builtin_palettes_pass_the_contrast_rules", builtin_palettes_pass_the_contrast_rules},
        {"builtin_outline_meets_three_to_one_on_every_surface", builtin_outline_meets_three_to_one_on_every_surface},
        {"without_overrides_every_palette_is_the_builtin_value", without_overrides_every_palette_is_the_builtin_value},
        {"overriding_web_light_accent_changes_only_that_combination", overriding_web_light_accent_changes_only_that_combination},
        {"every_palette_role_is_overridable_and_isolated", every_palette_role_is_overridable_and_isolated},
        {"role_helpers_reject_unknown_roles_and_wrong_kinds", role_helpers_reject_unknown_roles_and_wrong_kinds},
        {"each_contrast_rule_has_a_passing_and_a_failing_case", each_contrast_rule_has_a_passing_and_a_failing_case},
        {"rejection_message_reports_the_actual_ratio", rejection_message_reports_the_actual_ratio},
        {"invalid_entries_are_rejected_and_keep_the_active_list", invalid_entries_are_rejected_and_keep_the_active_list},
        {"replacing_and_clearing_the_list_takes_effect_immediately", replacing_and_clearing_the_list_takes_effect_immediately},
        {"off_thread_calls_report_wrong_thread", off_thread_calls_report_wrong_thread},
        {"a_custom_appearance_is_never_overridden", a_custom_appearance_is_never_overridden},
        {"painting_uses_the_overridden_colors_for_the_current_combination", painting_uses_the_overridden_colors_for_the_current_combination},
        {"a_multiline_field_paints_with_the_overridden_palette", a_multiline_field_paints_with_the_overridden_palette},
        {"the_semantic_layer_reads_the_effective_palette_read_only", the_semantic_layer_reads_the_effective_palette_read_only},
    };
    return test::run("palette_override", cases);
}
