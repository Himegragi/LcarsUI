// Control outlines (#86): which palette color draws the edge that identifies a control.
// One generic driver walks every component kind x appearance x theme variant and asserts the paint
// seam (ComponentPaint) colors; the expectations are a table of the ticket's rules, not a re-run of the code.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <algorithm>
#include <string>
#include <vector>
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&web_appearance(), &material_appearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};

struct RoleProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    detail::PaintRole current = detail::PaintRole::content;
    std::vector<std::pair<detail::PaintRole, Color>> colors;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect, Color c) override { colors.emplace_back(current, c); }
    void rounded(Rect, int, Color c) override { colors.emplace_back(current, c); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color c) override { colors.emplace_back(current, c); }
    bool has(Color c, detail::PaintRole role = detail::PaintRole::content) const {
        return std::any_of(colors.begin(), colors.end(), [&](const auto& e) {
            return e.second == c && (role == detail::PaintRole::content || (unsigned(e.first) & unsigned(role)) != 0);
        });
    }
};

// What each component kind identifies itself by, taken from the ticket's rules.
struct Subject {
    std::string id;
    bool outlined;         // true: the edge is `outline` when enabled, `border` when disabled
    bool border_enabled;   // true: `border` is expected to be painted while enabled
    bool disableable;      // the Semantics copy can be disabled (the component paints a disabled look)
};
const std::vector<Subject> subjects{
    {"text", true, false, true}, {"dropdown", true, false, true}, {"box", true, true, true}, {"radio", true, false, true},
    {"slider", true, false, true}, {"progress_empty", true, false, false},
    {"button", false, true, true}, {"nav", false, false, false}, {"progress_value", false, true, false},
};

struct Page {
    Ui ui;
    std::vector<ComponentHandle> handles;
    Page() {
        auto text = ui.add_text({.id = "text", .kind = Kind::text, .label = u"Name"}).value.value();
        auto dropdown = ui.add_dropdown({.id = "dropdown", .kind = Kind::dropdown, .label = u"Region"}, {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
        auto box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox, .label = u"Box"}).value.value();
        auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group, .label = u"Mode"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
        auto slider = ui.add_slider({.id = "slider", .kind = Kind::slider, .label = u"Volume"}, {0, 100, 5}).value.value();
        auto empty = ui.add_progress({.id = "progress_empty", .kind = Kind::progress, .label = u"Loading"}).value.value();
        auto valued = ui.add_progress({.id = "progress_value", .kind = Kind::progress, .label = u"Copying"}).value.value();
        valued.connect_source({[] { return 40; }, [](std::function<void()>) { return [] {}; }});
        auto button = ui.add_button({.id = "button", .kind = Kind::button, .label = u"Go"}).value.value();
        auto nav = ui.add_navigation_list({.id = "nav", .kind = Kind::navigation_list}, {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}}, {"one"}).value.value();
        handles = {text, dropdown, box, radio, slider, empty, valued, button, nav};
    }
    Frame frame(const Appearance& appearance) {
        test::FakeMeasure measure; ScrollState scroll;
        std::vector<layout::Node> nodes;
        for (const auto& h : handles) nodes.push_back(layout::control(h));
        return arrange(layout::column(nodes), ui, appearance, measure, 640, 2000, 1.f, scroll);
    }
};
RoleProbe paint(Page& page, const Appearance& appearance, const Placement& item, const Semantics& semantics) {
    RoleProbe probe(detail::access(page.ui), item, semantics, appearance, 1.f, true);
    detail::component_type(semantics.kind).paint(probe);
    return probe;
}
// Calls `per_subject` for every subject x appearance x variant with its arranged placement and semantics.
template <class Check>
void sweep(Check&& per_subject) {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            Page page;
            auto settings = page.ui.presentation(); settings.variant = variant;
            check(bool(page.ui.set_presentation(settings)), "dark/light accepted");
            auto frame = page.frame(*appearance);
            auto palette = appearance->palette(variant);
            for (const auto& subject : subjects) {
                auto found = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& i) { return i.id == subject.id; });
                const Placement* item = found == frame.items.end() ? nullptr : &*found;
                check(item != nullptr, (subject.id + " is arranged").c_str());
                auto semantics = detail::access(page.ui).describe(subject.id);
                check(semantics.has_value(), (subject.id + " has semantics").c_str());
                auto label = std::string(appearance->name()) + " " + variant.name + " " + subject.id;
                per_subject(page, *appearance, palette, subject, *item, *semantics, label);
            }
        }
}

void enabled_controls_draw_outline_where_the_edge_identifies_them() {
    sweep([](Page& page, const Appearance& a, const Palette& p, const Subject& s, const Placement& item, const Semantics& sem, const std::string& label) {
        auto probe = paint(page, a, item, sem);
        check(probe.has(p.outline) == s.outlined, (label + ": enabled outline").c_str());
        if (!s.outlined) check(!probe.has(p.outline), (label + ": decorative edge keeps border").c_str());
        if (s.border_enabled) check(probe.has(p.border), (label + ": border is painted").c_str());
    });
}

void disabled_controls_draw_the_light_border_instead_of_the_outline() {
    sweep([](Page& page, const Appearance& a, const Palette& p, const Subject& s, const Placement& item, const Semantics& sem, const std::string& label) {
        if (!s.disableable) return;
        auto value = sem; value.enabled = false; value.focused = false;
        auto probe = paint(page, a, item, value);
        check(!probe.has(p.outline), (label + ": disabled has no outline").c_str());
        if (s.outlined) check(probe.has(p.border), (label + ": disabled edge is border").c_str());
        // A disabled edge is lighter than the enabled outline on the same surface.
        if (s.outlined) check(contrast_ratio(p.border, p.surface) < contrast_ratio(p.outline, p.surface), (label + ": disabled lighter").c_str());
    });
}

void focus_and_error_edges_keep_their_colors() {
    sweep([](Page& page, const Appearance& a, const Palette& p, const Subject& s, const Placement& item, const Semantics& sem, const std::string& label) {
        if (s.id != "text" && s.id != "dropdown") return;
        auto focused = sem; focused.enabled = true; focused.focused = true; focused.invalid = false;
        auto on_focus = paint(page, a, item, focused);
        check(on_focus.has(p.accent, detail::PaintRole::focus_indicator) && !on_focus.has(p.outline), (label + ": focus edge is accent").c_str());
        auto invalid = sem; invalid.enabled = true; invalid.focused = false; invalid.invalid = true;
        auto on_error = paint(page, a, item, invalid);
        check(on_error.has(p.error, detail::PaintRole::error_indicator) && !on_error.has(p.outline), (label + ": error edge is error").c_str());
    });
}

void focused_checkbox_radio_and_slider_keep_the_outline() {
    sweep([](Page& page, const Appearance& a, const Palette& p, const Subject& s, const Placement& item, const Semantics& sem, const std::string& label) {
        if (s.id != "box" && s.id != "radio" && s.id != "slider") return;
        auto focused = sem; focused.enabled = true; focused.focused = true;
        check(paint(page, a, item, focused).has(p.outline), (label + ": focused keeps outline").c_str());
    });
}

void radio_with_every_option_disabled_draws_the_light_border() {
    sweep([](Page& page, const Appearance& a, const Palette& p, const Subject& s, const Placement& item, const Semantics& sem, const std::string& label) {
        if (s.id != "radio") return;
        auto value = sem; value.enabled = true;
        for (auto& o : value.options) o.enabled = false;
        auto probe = paint(page, a, item, value);
        check(!probe.has(p.outline) && probe.has(p.border), (label + ": disabled options use border").c_str());
        auto one = sem; one.options[0].enabled = false;
        check(paint(page, a, item, one).has(p.outline), (label + ": a remaining enabled option keeps the outline").c_str());
    });
}

void progress_track_follows_whether_a_value_is_shown() {
    for (const auto* a : builtins)
        for (const auto& variant : variants) {
            Page page;
            auto settings = page.ui.presentation(); settings.variant = variant; page.ui.set_presentation(settings);
            auto frame = page.frame(*a);
            auto p = a->palette(variant);
            for (const auto* id : {"progress_empty", "progress_value"}) {
                auto item = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& i) { return i.id == id; });
                auto sem = detail::access(page.ui).describe(id);
                check(item != frame.items.end() && sem, "progress arranged");
                bool has_value = !sem->value.empty();
                auto probe = paint(page, *a, *item, *sem);
                check(has_value == (std::string(id) == "progress_value"), "progress_value shows text, progress_empty does not");
                check(probe.has(has_value ? p.border : p.outline), "track color follows the value text");
                check(!probe.has(has_value ? p.outline : p.border), "the other color is absent from the track");
            }
        }
}

void an_override_of_outline_reaches_the_painted_edges() {
    Page page;
    const Color custom{40, 40, 40};
    check(bool(page.ui.set_palette_overrides({{"Minimal Web", ThemeVariant::light(), "outline", custom}})), "outline override accepted");
    auto frame = page.frame(web_appearance());
    auto palette = *page.ui.effective_palette(web_appearance(), ThemeVariant::light());
    check(palette.outline == custom, "effective palette carries the override");
    for (const auto& subject : subjects) {
        if (!subject.outlined) continue;
        auto item = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& i) { return i.id == subject.id; });
        auto sem = detail::access(page.ui).describe(subject.id);
        check(item != frame.items.end() && sem, "subject arranged");
        check(paint(page, web_appearance(), *item, *sem).has(custom), (subject.id + ": paints the overridden outline").c_str());
    }
    check(!web_appearance().palette().outline.r || web_appearance().palette().outline != custom, "built-in value is untouched");
}
}
int main() {
    static const test::Case cases[] = {
        {"enabled_controls_draw_outline_where_the_edge_identifies_them", enabled_controls_draw_outline_where_the_edge_identifies_them},
        {"disabled_controls_draw_the_light_border_instead_of_the_outline", disabled_controls_draw_the_light_border_instead_of_the_outline},
        {"focus_and_error_edges_keep_their_colors", focus_and_error_edges_keep_their_colors},
        {"focused_checkbox_radio_and_slider_keep_the_outline", focused_checkbox_radio_and_slider_keep_the_outline},
        {"radio_with_every_option_disabled_draws_the_light_border", radio_with_every_option_disabled_draws_the_light_border},
        {"progress_track_follows_whether_a_value_is_shown", progress_track_follows_whether_a_value_is_shown},
        {"an_override_of_outline_reaches_the_painted_edges", an_override_of_outline_reaches_the_painted_edges},
    };
    return test::run("outline", cases);
}
