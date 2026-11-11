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
#include <thread>
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&web_appearance(), &material_appearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string name_of(const Appearance& a) { return std::string(a.name()); }
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
    bool has(Color c) const { return std::find(colors.begin(), colors.end(), c) != colors.end(); }
};
// Paints every placement of a checked checkbox page with the appearance and the Ui's current variant.
std::vector<Color> paint_colors(Ui& ui, const Appearance& appearance, const CheckboxHandle& box) {
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::column({layout::control(box)}), ui, appearance, measure, 400, 300, 1.f, scroll);
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
bool has(const std::vector<Color>& colors, Color c) { return std::find(colors.begin(), colors.end(), c) != colors.end(); }

void contrast_ratio_matches_wcag_reference_values() {
    check(std::fabs(contrast_ratio({0, 0, 0}, {255, 255, 255}) - 21.0) < 0.001, "black on white is 21:1");
    check(std::fabs(contrast_ratio({255, 255, 255}, {0, 0, 0}) - 21.0) < 0.001, "ratio is symmetric");
    check(std::fabs(contrast_ratio(gray(0x76), gray(255)) - 4.54) < 0.005, "#767676 on white is 4.54:1");
    check(std::fabs(contrast_ratio(gray(0x77), gray(255)) - 4.48) < 0.005, "#777777 on white is 4.48:1");
    check(std::fabs(contrast_ratio(gray(0x94), gray(255)) - 3.03) < 0.005, "#949494 on white is 3.03:1");
    check(std::fabs(contrast_ratio(gray(0x40), gray(0x40)) - 1.0) < 1e-9, "equal colors are 1:1");
}

void builtin_palettes_pass_the_contrast_rules() {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants)
            check(check_palette_contrast(appearance->palette(variant)).empty(), (name_of(*appearance) + " " + variant.name + " passes").c_str());
}

void without_overrides_every_palette_is_the_builtin_value() {
    Ui ui;
    check(ui.palette_overrides().empty(), "no overrides initially");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto effective = ui.effective_palette(*appearance, variant);
            check(effective && *effective == appearance->palette(variant), "effective palette equals the built-in palette");
        }
}

void overriding_web_light_accent_changes_only_that_combination() {
    Ui ui;
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", green)})), "override accepted");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto expected = appearance->palette(variant);
            if (appearance == &web_appearance() && variant == ThemeVariant::light()) expected.accent = green;
            auto effective = ui.effective_palette(*appearance, variant);
            check(effective && *effective == expected, (name_of(*appearance) + " " + variant.name + " effective palette").c_str());
        }
    check(ui.palette_overrides().size() == 1 && ui.palette_overrides()[0].role == "accent", "override list is readable");
}

// One driver over every palette role: each override changes exactly its own field.
void every_palette_role_is_overridable_and_isolated() {
    auto roles = palette_roles();
    check(roles.size() == 12, "ten core colors, scrim and scrim_alpha are roles");
    check(std::find(roles.begin(), roles.end(), "scrim_alpha") != roles.end() && std::find(roles.begin(), roles.end(), "border") != roles.end(), "known role names present");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants)
            for (auto role : roles) {
                auto base = appearance->palette(variant);
                auto original = *palette_value(base, role);
                // Nudge one channel by one step: distinct from the built-in value, contrast rules keep passing.
                PaletteValue changed = std::holds_alternative<int>(original) ? PaletteValue{std::get<int>(original) + 1}
                    : PaletteValue{Color{static_cast<unsigned char>(std::get<Color>(original).r == 255 ? 254 : std::get<Color>(original).r + 1), std::get<Color>(original).g, std::get<Color>(original).b}};
                Ui ui;
                auto result = ui.set_palette_overrides({entry(*appearance, variant, std::string(role), changed)});
                auto label = name_of(*appearance) + " " + variant.name + " " + std::string(role);
                check(bool(result), (label + " accepted").c_str());
                auto effective = ui.effective_palette(*appearance, variant);
                check(effective.value.has_value(), (label + " readable").c_str());
                for (auto other : roles) {
                    auto expected = other == role ? changed : *palette_value(base, other);
                    check(*palette_value(*effective, other) == expected, (label + " field " + std::string(other)).c_str());
                }
                // The same list leaves every other appearance and variant at its built-in value.
                for (const auto* other_appearance : builtins)
                    for (const auto& other_variant : variants)
                        if (other_appearance != appearance || !(other_variant == variant))
                            check(*ui.effective_palette(*other_appearance, other_variant) == other_appearance->palette(other_variant), (label + " leaves others untouched").c_str());
            }
}

void role_helpers_reject_unknown_roles_and_wrong_kinds() {
    Palette p = web_appearance().palette();
    check(!palette_value(p, "hover").has_value(), "unknown role has no value");
    check(!set_palette_value(p, "hover", Color{1, 2, 3}), "unknown role is refused");
    check(!set_palette_value(p, "accent", 7), "a color role needs a Color");
    check(!set_palette_value(p, "scrim_alpha", Color{1, 2, 3}), "scrim_alpha needs an int");
    check(!set_palette_value(p, "scrim_alpha", 256) && !set_palette_value(p, "scrim_alpha", -1), "scrim_alpha is 0-255");
    check(set_palette_value(p, "scrim_alpha", 255) && p.scrim_alpha == 255, "scrim_alpha 255 is accepted");
    check(set_palette_value(p, "scrim_alpha", 0) && p.scrim_alpha == 0, "scrim_alpha 0 is accepted");
    check(p == [&] { auto q = web_appearance().palette(); q.scrim_alpha = 0; return q; }(), "only scrim_alpha changed");
}

struct ContrastCase { const char* pair; std::vector<std::pair<std::string, Color>> passing, failing; };
// Web light. "passing" sits just above the threshold (4.54:1 or 3.03:1), "failing" just below it.
std::vector<ContrastCase> contrast_cases() {
    std::vector<std::pair<std::string, Color>> surfaces{{"background", gray(255)}, {"panel", gray(255)}, {"surface", gray(255)}, {"selection", gray(255)}, {"text", gray(0x76)}};
    auto with = [&](std::string role, Color c) { auto list = surfaces; for (auto& e : list) if (e.first == role) e.second = c; return list; };
    return {
        {"text/background", surfaces, with("background", gray(250))},
        {"text/panel", surfaces, with("panel", gray(250))},
        {"text/surface", surfaces, with("surface", gray(250))},
        {"text/selection", surfaces, with("selection", gray(250))},
        {"muted/surface", {{"muted", gray(0x76)}}, {{"muted", gray(0x77)}}},
        {"error/surface", {{"error", gray(0x76)}}, {{"error", gray(0x77)}}},
        {"on_accent/accent", {{"accent", gray(0x76)}, {"on_accent", gray(255)}}, {{"accent", gray(0x77)}, {"on_accent", gray(255)}}},
        {"accent/surface", {{"accent", gray(0x94)}, {"on_accent", gray(0)}}, {{"accent", gray(0x95)}, {"on_accent", gray(0)}}},
    };
}
std::vector<PaletteOverride> web_light(const std::vector<std::pair<std::string, Color>>& list) {
    std::vector<PaletteOverride> out;
    for (const auto& [role, color] : list) out.push_back(entry(web_appearance(), ThemeVariant::light(), role, color));
    return out;
}
void each_contrast_rule_has_a_passing_and_a_failing_case() {
    for (const auto& c : contrast_cases()) {
        Ui ui;
        auto good = ui.set_palette_overrides(web_light(c.passing));
        check(bool(good), (std::string(c.pair) + " just-passing case is accepted").c_str());
        auto before = ui.palette_overrides();
        auto bad = ui.set_palette_overrides(web_light(c.failing));
        check(bad.status == Status::invalid_value, (std::string(c.pair) + " just-failing case is rejected").c_str());
        std::u16string pair(c.pair, c.pair + std::string(c.pair).size());
        check(contains(bad.message, pair), (std::string(c.pair) + " message names the pair").c_str());
        check(contains(bad.message, u"Minimal Web") && contains(bad.message, u"light"), "message names appearance and variant");
        check(contains(bad.message, u"needs") && contains(bad.message, u":1"), "message carries the ratio");
        check(ui.palette_overrides() == before, (std::string(c.pair) + " rejected list leaves the active list unchanged").c_str());
        auto effective = ui.effective_palette(web_appearance(), ThemeVariant::light());
        check(effective && *effective != web_appearance().palette(ThemeVariant::light()), "active overrides still apply");
    }
}
void rejection_message_reports_the_actual_ratio() {
    Ui ui;
    auto bad = ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "muted", gray(0x77))});
    check(bad.status == Status::invalid_value && contains(bad.message, u"4.47:1"), "#777777 on white reports 4.47:1 (truncated)");
    Ui ui2;
    auto near_surface = ui2.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "text", Color{250, 250, 250})});
    check(near_surface.status == Status::invalid_value && contains(near_surface.message, u"text/surface") && contains(near_surface.message, u"text/panel"), "a text color close to the surfaces fails every surface pair");
}

void invalid_entries_are_rejected_and_keep_the_active_list() {
    Ui ui;
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", green)})), "baseline");
    auto active = ui.palette_overrides();
    auto bad = [&](std::vector<PaletteOverride> list, const char* label) {
        auto r = ui.set_palette_overrides(std::move(list));
        check(r.status == Status::invalid_value, label);
        check(ui.palette_overrides() == active, label);
    };
    bad({{"No Such Appearance", ThemeVariant::light(), "accent", green}}, "unknown appearance name");
    bad({entry(web_appearance(), ThemeVariant::high_contrast(), "accent", green)}, "unsupported variant");
    bad({entry(web_appearance(), ThemeVariant{"sepia"}, "accent", green)}, "unknown variant");
    bad({entry(web_appearance(), ThemeVariant::light(), "hover", green)}, "unknown role");
    bad({entry(web_appearance(), ThemeVariant::light(), "scrim_alpha", 256)}, "scrim_alpha 256");
    bad({entry(web_appearance(), ThemeVariant::light(), "scrim_alpha", -1)}, "scrim_alpha -1");
    bad({entry(web_appearance(), ThemeVariant::light(), "accent", 5)}, "int for a color role");
    bad({entry(web_appearance(), ThemeVariant::light(), "scrim_alpha", green)}, "color for scrim_alpha");
    bad({entry(web_appearance(), ThemeVariant::light(), "accent", green), entry(web_appearance(), ThemeVariant::light(), "accent", gray(10))}, "duplicate entry");
    // The valid first entry of a partly invalid list is not applied either.
    bad({entry(web_appearance(), ThemeVariant::dark(), "accent", Color{250, 200, 100}), entry(web_appearance(), ThemeVariant::light(), "hover", green)}, "all or nothing");
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "scrim_alpha", 255), entry(material_appearance(), ThemeVariant::dark(), "scrim_alpha", 0)})), "scrim_alpha bounds 0 and 255 are accepted");
}

void replacing_and_clearing_the_list_takes_effect_immediately() {
    Ui ui;
    int invalidations = 0; detail::access(ui).on_invalidate([&] { ++invalidations; });
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", green)})) && invalidations == 1, "setting requests a repaint");
    auto orange = Color{150, 70, 0};
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", orange)})) && invalidations == 2, "replacing requests a repaint");
    check(ui.effective_palette(web_appearance(), ThemeVariant::light())->accent == orange, "whole list is replaced, not merged");
    check(bool(ui.set_palette_overrides({})) && invalidations == 3, "clearing requests a repaint");
    check(ui.palette_overrides().empty() && *ui.effective_palette(web_appearance(), ThemeVariant::light()) == web_appearance().palette(), "cleared list restores built-in values");
    check(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "hover", green)}).status == Status::invalid_value && invalidations == 3, "a rejected list does not repaint");
}

void off_thread_calls_report_wrong_thread() {
    Ui ui; Status status = Status::ok;
    std::thread([&] { status = ui.set_palette_overrides({}).status; }).join();
    check(status == Status::wrong_thread, "setting needs the UI thread");
}

void a_custom_appearance_is_never_overridden() {
    struct Custom final : Appearance {
        std::string_view name() const override { return "Minimal Web"; }  // same name, different object
        Palette palette() const override { return web_appearance().palette(); }
        FieldParts field(Rect b, int s) const override { return web_appearance().field(b, s); }
        int radius() const override { return 6; }
        FieldLayout arrange_field(int w, const FieldText& t, float s) const override { return web_appearance().arrange_field(w, t, s); }
        int button_height(int l, float s) const override { return web_appearance().button_height(l, s); }
        int button_padding(float s) const override { return web_appearance().button_padding(s); }
    } custom;
    Ui ui;
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", green)})), "override accepted");
    auto effective = ui.effective_palette(custom, ThemeVariant::light());
    check(effective && *effective == custom.palette(), "a custom appearance keeps its own palette even when it reuses a built-in name");
}

void painting_uses_the_overridden_colors_for_the_current_combination() {
    Ui ui;
    auto box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox, .label = u"Box"}).value.value();
    box.connect_source({[] { return true; }, [](std::function<void()>) { return [] {}; }});  // a checked box
    auto web_accent = web_appearance().palette().accent, web_dark_accent = web_appearance().palette(ThemeVariant::dark()).accent;
    auto material_accent = material_appearance().palette().accent;
    check(has(paint_colors(ui, web_appearance(), box), web_accent), "built-in accent paints without overrides");
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", green)})), "override accepted");
    auto light = paint_colors(ui, web_appearance(), box);
    check(has(light, green) && !has(light, web_accent), "web light paints green instead of the built-in accent");
    check(has(paint_colors(ui, material_appearance(), box), material_accent), "material light is unchanged");
    auto settings = ui.presentation(); settings.variant = ThemeVariant::dark();
    check(bool(ui.set_presentation(settings)), "switch to dark");
    auto dark = paint_colors(ui, web_appearance(), box);
    check(has(dark, web_dark_accent) && !has(dark, green), "web dark keeps its built-in accent");
    settings.variant = ThemeVariant::light(); check(bool(ui.set_presentation(settings)), "back to light");
    check(has(paint_colors(ui, web_appearance(), box), green), "light shows the override again");
    check(bool(ui.set_palette_overrides({})), "clear");
    check(has(paint_colors(ui, web_appearance(), box), web_accent), "clearing restores the built-in accent on the next frame");
}

void the_semantic_layer_reads_the_effective_palette_read_only() {
    Ui ui;
    check(ui.semantics().effective_palette().status == Status::not_found, "no appearance bound yet");
    ui.bind_appearance(&web_appearance());
    check(bool(ui.set_palette_overrides({entry(web_appearance(), ThemeVariant::light(), "accent", green), entry(web_appearance(), ThemeVariant::dark(), "accent", Color{250, 200, 100})})), "overrides accepted");
    auto light = ui.semantics().effective_palette();
    check(light && *light == *ui.effective_palette(web_appearance(), ThemeVariant::light()) && light->accent == green, "semantic palette equals the typed palette (light)");
    auto settings = ui.presentation(); settings.variant = ThemeVariant::dark();
    check(bool(ui.set_presentation(settings)), "switch to dark");
    auto dark = ui.semantics().effective_palette();
    check(dark && *dark == *ui.effective_palette(web_appearance(), ThemeVariant::dark()) && dark->accent == (Color{250, 200, 100}), "semantic palette follows the variant");
    ui.bind_appearance(&material_appearance());
    check(*ui.semantics().effective_palette() == material_appearance().palette(ThemeVariant::dark()), "semantic palette follows the bound appearance");
    ui.bind_appearance(nullptr);
    check(ui.semantics().effective_palette().status == Status::not_found, "unbound again");
    Status status = Status::ok;
    ui.bind_appearance(&web_appearance());
    std::thread([&] { status = ui.semantics().effective_palette().status; }).join();
    check(status == Status::wrong_thread, "the semantic read needs the UI thread");
}
}
int main() {
    static const test::Case cases[] = {
        {"contrast_ratio_matches_wcag_reference_values", contrast_ratio_matches_wcag_reference_values},
        {"builtin_palettes_pass_the_contrast_rules", builtin_palettes_pass_the_contrast_rules},
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
        {"the_semantic_layer_reads_the_effective_palette_read_only", the_semantic_layer_reads_the_effective_palette_read_only},
    };
    return test::run("palette_override", cases);
}
