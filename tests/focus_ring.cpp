// Focus ring (#90): every component draws its focus indicator as one closed rounded frame whose
// thickness, radius, color and visibility the code controls.
//
// Seams under test (confirmed by the ticket's Agent Brief):
//   - Ui::set_presentation / PresentationSettings::{focus_ring_width, focus_ring_radius, focus_indicator}
//   - Palette::focus, Ui::set_palette_overrides / effective_palette, check_palette_contrast
//   - detail::component_type(kind).paint(ComponentPaint&) observed through a recording probe
//   - arrange(...) placements (layout must not move when the ring appears)
//   - detail::UiState::key_down / set_system_keyboard_cues (when the indicator shows)
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&web_appearance(), &material_appearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string name_of(const Appearance& a) { return std::string(a.name()); }
// Ui::effective_palette settles an empty `focus` to the accent; a palette straight from an appearance has not been settled yet.
Palette settled(Palette p) { p.focus = p.focus_color(); return p; }

// ---- presentation settings -------------------------------------------------------------------

void ring_width_and_radius_accept_their_bounds_and_refuse_the_rest() {
    Ui ui;
    check(!ui.presentation().focus_ring_width && !ui.presentation().focus_ring_radius, "both are empty by default (the appearance decides)");
    auto set = [&](std::optional<int> width, std::optional<int> radius) {
        auto settings = ui.presentation(); settings.focus_ring_width = width; settings.focus_ring_radius = radius;
        return ui.set_presentation(settings);
    };
    for (int width : {2, 3, 6}) check(bool(set(width, std::nullopt)) && ui.presentation().focus_ring_width == width, "2-6 DIP are accepted");
    check(bool(set(std::nullopt, std::nullopt)) && !ui.presentation().focus_ring_width, "empty again");
    for (int radius : {0, 1, 32}) check(bool(set(std::nullopt, radius)) && ui.presentation().focus_ring_radius == radius, "0-32 DIP are accepted");
    check(bool(set(4, 9)), "a valid pair is accepted");
    const auto before = ui.presentation();
    for (int width : {1, 7, 0, -3, 100}) {
        auto bad = ui.set_presentation([&] { auto s = before; s.focus_ring_width = width; s.text_size = 20; return s; }());
        check(bad.status == Status::invalid_value, "a ring width outside 2-6 is invalid_value");
        check(ui.presentation() == before, "a refused setting changes nothing, other fields of the same call included");
    }
    for (int radius : {-1, 33, -100, 1000}) {
        auto bad = ui.set_presentation([&] { auto s = before; s.focus_ring_radius = radius; s.text_size = 20; return s; }());
        check(bad.status == Status::invalid_value, "a ring radius outside 0-32 is invalid_value");
        check(ui.presentation() == before, "a refused radius changes nothing");
    }
    check(!(before == PresentationSettings{}), "operator== covers the two fields");
}

// ---- the `focus` palette role ----------------------------------------------------------------

void focus_is_a_palette_role_equal_to_accent_by_default() {
    auto roles = palette_roles();
    check(std::find(roles.begin(), roles.end(), "focus") != roles.end(), "focus is a named role");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto p = appearance->palette(variant);
            check(!p.focus && p.focus_color() == p.accent, (name_of(*appearance) + " " + variant.name + ": focus defaults to accent").c_str());
        }
    Palette p = web_appearance().palette();
    check(set_palette_value(p, "focus", Color{1, 2, 3}) && p.focus == Color{1, 2, 3}, "focus is settable by name");
    check(!set_palette_value(p, "focus", 5), "focus takes a color, not an int");
}

void replacing_accent_moves_focus_unless_focus_is_replaced_too() {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            const auto label = name_of(*appearance) + " " + variant.name;
            const auto base = appearance->palette(variant);
            // A dark variant needs light colors to stay readable; derive from the built-in accent by a small nudge instead of fixed values.
            Color nudged{static_cast<unsigned char>(base.accent.r == 255 ? 254 : base.accent.r + 1), base.accent.g, base.accent.b};
            Ui only_accent;
            check(bool(only_accent.set_palette_overrides({{std::string(appearance->name()), variant, "accent", nudged}})), (label + ": accent override accepted").c_str());
            auto p = *only_accent.effective_palette(*appearance, variant);
            check(p.accent == nudged && p.focus == nudged, (label + ": focus follows the replaced accent").c_str());
            Ui only_focus;
            Color own{static_cast<unsigned char>(base.accent.r == 255 ? 253 : base.accent.r + 2), base.accent.g, base.accent.b};
            check(bool(only_focus.set_palette_overrides({{std::string(appearance->name()), variant, "focus", own}})), (label + ": focus override accepted").c_str());
            p = *only_focus.effective_palette(*appearance, variant);
            check(p.focus == own && p.accent == base.accent, (label + ": replacing focus alone leaves accent alone").c_str());
            Ui both;
            check(bool(both.set_palette_overrides({{std::string(appearance->name()), variant, "accent", nudged}, {std::string(appearance->name()), variant, "focus", own}})), (label + ": both accepted").c_str());
            p = *both.effective_palette(*appearance, variant);
            check(p.focus == own && p.accent == nudged, (label + ": an explicit focus wins over the follow rule").c_str());
        }
}

// Review item 2: "focus not set => follows accent" holds for every appearance, not only inside one brace initializer.
struct AccentAfterwards final : Appearance {
    // A custom appearance that starts from the Web palette and then assigns its own accent (and maybe focus).
    explicit AccentAfterwards(std::optional<Color> own_focus = std::nullopt) : own_focus_(own_focus) {}
    static constexpr Color accent{10, 80, 160};
    std::string_view name() const override { return "Accent afterwards"; }
    Palette palette() const override {
        Palette p = web_appearance().palette(); p.accent = accent; p.on_accent = {255, 255, 255};
        if (own_focus_) p.focus = *own_focus_;
        return p;
    }
    FieldParts field(Rect b, int s) const override { return web_appearance().field(b, s); }
    int radius() const override { return 6; }
    FieldLayout arrange_field(int w, const FieldText& t, float s) const override { return web_appearance().arrange_field(w, t, s); }
    int button_height(int l, float s) const override { return web_appearance().button_height(l, s); }
    int button_padding(float s) const override { return web_appearance().button_padding(s); }
    std::optional<Color> own_focus_;
};
void a_custom_appearance_that_assigns_accent_afterwards_keeps_the_ring_on_its_accent() {
    Ui ui;
    AccentAfterwards custom;
    auto p = ui.effective_palette(custom, ThemeVariant::light());
    check(p && p->accent == AccentAfterwards::accent && p->focus == AccentAfterwards::accent, "focus left out follows the accent the custom appearance assigned later");
    Ui overridden;
    check(bool(overridden.set_palette_overrides({{"Minimal Web", ThemeVariant::light(), "accent", Color{20, 110, 50}}})), "an accent override on a built-in is accepted");
    p = overridden.effective_palette(custom, ThemeVariant::light());
    check(p && p->focus == AccentAfterwards::accent, "overrides never touch a custom appearance, its ring still follows its own accent");
    const Color own{120, 40, 160};
    AccentAfterwards explicit_focus{own};
    p = ui.effective_palette(explicit_focus, ThemeVariant::light());
    check(p && p->accent == AccentAfterwards::accent && p->focus == own, "a custom appearance that sets focus itself keeps it");
}

void focus_needs_three_to_one_on_every_surface() {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto p = appearance->palette(variant);
            const auto label = name_of(*appearance) + " " + variant.name;
            for (auto surface : {p.background, p.panel, p.surface})
                check(contrast_ratio(p.focus_color(), surface) >= 3.0, (label + ": the built-in ring color is 3:1 on every surface").c_str());
            check(check_palette_contrast(p).empty(), (label + ": the built-in palette passes every rule").c_str());
        }
    // The just-passing / just-failing literals per surface live with the other contrast rules (tests/palette_override.cpp).
}


// ---- the generic driver: every component kind, both appearances, both densities, two scales -------

using detail::PaintRole;
bool has_role(PaintRole value, PaintRole bit) { return (static_cast<unsigned>(value) & static_cast<unsigned>(bit)) != 0; }
int scaled(float scale, int dip) { return std::max(1, static_cast<int>(std::lround(dip * scale))); }
bool inside(Rect part, Rect whole) { return part.x >= whole.x && part.y >= whole.y && part.x + part.w <= whole.x + whole.w && part.y + part.h <= whole.y + whole.h; }
bool disjoint(Rect a, Rect b) { auto i = intersect(a, b); return i.w <= 0 || i.h <= 0; }

struct Cmd { enum class Kind { fill, rounded, text } kind; Rect rect; int radius; Color color; PaintRole role; Rect clip; };
struct Probe final : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<Cmd> cmds;
    Rect clip_rect{-100000, -100000, 200000, 200000};
    PaintRole current = PaintRole::content;
    test::FakeMeasure measure;
    void role(PaintRole value) override { current = value; }
    void fill(Rect rect, Color color) override { cmds.push_back({Cmd::Kind::fill, rect, 0, color, current, clip_rect}); }
    void rounded(Rect rect, int radius, Color color) override { cmds.push_back({Cmd::Kind::rounded, rect, radius, color, current, clip_rect}); }
    void clip(Rect rect) override { clip_rect = intersect(clip_rect, rect); }
    void text(std::u16string_view value, int pixels, int x, int y, Color color) override {
        if (value.empty()) return;
        const int width = static_cast<int>(std::ceil(measure.measure(value, pixels).width));
        cmds.push_back({Cmd::Kind::text, {x, y, width, measure.line_height(pixels)}, 0, color, current, clip_rect});
    }
    void vertical_text(std::u16string_view value, VerticalForm, int, Rect cell, Color color) override {
        if (!value.empty()) cmds.push_back({Cmd::Kind::text, cell, 0, color, current, clip_rect});
    }
    int count(PaintRole bit) const { return static_cast<int>(std::count_if(cmds.begin(), cmds.end(), [&](const Cmd& c) { return has_role(c.role, bit); })); }
};

// The ring is the pair of rounded commands marked focus_indicator: the outer one in the ring color, the inner one in the
// color of what the ring frames. Anything else marked focus_indicator (a strip, a dot) is not a ring.
struct Ring { std::size_t index; Rect outer, inner; int outer_radius, inner_radius; Color color, base; PaintRole role; };
std::optional<Ring> find_ring(const std::vector<Cmd>& cmds) {
    for (std::size_t i = 0; i + 1 < cmds.size(); ++i)
        if (has_role(cmds[i].role, PaintRole::focus_indicator) && cmds[i].kind == Cmd::Kind::rounded &&
            has_role(cmds[i + 1].role, PaintRole::focus_indicator) && cmds[i + 1].kind == Cmd::Kind::rounded)
            return Ring{i, cmds[i].rect, cmds[i + 1].rect, cmds[i].radius, cmds[i + 1].radius, cmds[i].color, cmds[i + 1].color, cmds[i].role};
    return std::nullopt;
}

struct Page {
    Ui ui; ScrollState scroll, label_scroll; test::FakeMeasure measure;
    std::vector<layout::Node> nodes;
    std::vector<std::string> ids;
    Page() {
        auto add = [&](std::string id, layout::Node node) { ids.push_back(std::move(id)); nodes.push_back(std::move(node)); };
        auto text = ui.add_text({.id = "text", .kind = Kind::text, .label = u"Name", .value = u"abc"}).value.value();
        add("text", layout::control(text));
        auto note = ui.add_text({.id = "note", .kind = Kind::text, .label = u"Notes", .value = u"a\nb\nc\nd\ne", .multiline = true, .visible_lines = 3}).value.value();
        add("note", layout::control(note));
        auto rich = ui.add_text({.id = "rich", .kind = Kind::text, .label = u"Description", .value = u"bold plain", .multiline = true, .visible_lines = 3, .rich = true}).value.value();
        add("rich", layout::control(rich));
        auto dropdown = ui.add_dropdown({.id = "dropdown", .kind = Kind::dropdown, .label = u"Region"}, {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
        add("dropdown", layout::control(dropdown));
        auto box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox, .label = u"Remember me"}).value.value();
        add("box", layout::control(box));
        auto button = ui.add_button({.id = "button", .kind = Kind::button, .label = u"Go"}).value.value();
        add("button", layout::control(button));
        auto radio_none = ui.add_radio_group({.id = "radio_none", .kind = Kind::radio_group, .label = u"Mode"}, {{{"a"}, u"A", false}, {{"b"}, u"B"}, {{"c"}, u"C"}}).value.value();
        add("radio_none", layout::control(radio_none));
        auto radio_sel = ui.add_radio_group({.id = "radio_sel", .kind = Kind::radio_group, .label = u"Size"}, {{{"a"}, u"A"}, {{"b"}, u"B"}, {{"c"}, u"C"}}).value.value();
        radio_sel.connect_source({[] { return std::optional<OptionValue>{}; }, [](auto) { return [] {}; }});
        radio_sel.connect_immediate([](const OptionChange&) { return Result{}; });
        check(bool(radio_sel.select({"b"})), "a radio option is selected");
        add("radio_sel", layout::control(radio_sel));
        auto slider = ui.add_slider({.id = "slider", .kind = Kind::slider, .label = u"Volume"}, {0, 100, 5}).value.value();
        add("slider", layout::control(slider));
        auto list = ui.add_list({.id = "list", .kind = Kind::list, .label = u"Items"}, {{{"i1"}, u"One"}, {{"i2"}, u"Two"}, {{"i3"}, u"Three"}}).value.value();
        list.connect_source({[] { return std::optional<OptionValue>{}; }, [](auto) { return [] {}; }});
        list.connect_immediate([](std::optional<OptionValue>) { return Result{}; });
        check(bool(list.select({"i2"})), "a list row is selected");
        add("list", layout::control(list));
        auto tabs = ui.add_tabs({.id = "tabs", .kind = Kind::tabs, .label = u"Pages"}, {{{"t1"}, u"One", true, {}}, {{"t2"}, u"Two", true, {}}}).value.value();
        add("tabs", layout::control(tabs));
        auto nav = ui.add_navigation_list({.id = "nav", .kind = Kind::navigation_list}, {{{"n1"}, u"General", true, {}}, {{"n2"}, u"Other", true, {}}}, {"n1"}).value.value();
        add("nav", layout::control(nav));
        auto scrolled = ui.add_label({.id = "scroll_text", .kind = Kind::label, .value = std::u16string(1200, u'x')}).value.value();
        add("scroll_text", layout::scroll_text(scrolled, label_scroll, 60));
        auto vertical = ui.add_label({.id = "vertical", .kind = Kind::label, .value = std::u16string(900, u'字'), .writing_mode = WritingMode::vertical}).value.value();
        add("vertical", layout::vertical_text(vertical, 120));
    }
    detail::UiState& state() { return detail::access(ui); }
    void settings(const std::function<void(PresentationSettings&)>& change) {
        auto value = ui.presentation(); change(value);
        check(bool(ui.set_presentation(value)), "presentation accepted");
    }
    Frame frame(const Appearance& appearance, float scale = 1.f) {
        return arrange(layout::column(nodes), ui, appearance, measure, 640, 6000, scale, scroll);
    }
};
const Placement* placed(const Frame& frame, const std::string& id) {
    auto found = std::find_if(frame.items.begin(), frame.items.end(), [&](const Placement& p) { return p.id == id; });
    return found == frame.items.end() ? nullptr : &*found;
}

struct Run { const Appearance& appearance; Density density; float scale; };
const Run runs[] = {
    {web_appearance(), Density::standard, 1.f}, {web_appearance(), Density::compact, 1.f}, {web_appearance(), Density::standard, 1.5f}, {web_appearance(), Density::compact, 1.5f},
    {material_appearance(), Density::standard, 1.f}, {material_appearance(), Density::compact, 1.f}, {material_appearance(), Density::standard, 1.5f}, {material_appearance(), Density::compact, 1.5f},
};
std::string label_of(const Run& r, const std::string& id) {
    return std::string(r.appearance.name()) + (r.density == Density::compact ? " compact " : " standard ") + std::to_string(r.scale) + " " + id;
}
// Calls body(page, frame, placement, semantics, painted probe, label) for every subject focused in turn.
// `always` shows the indicator after programmatic focus.
template <class Body>
void for_each_focused(const Run& r, const std::function<void(PresentationSettings&)>& tweak, Body&& body) {
    for (const auto& subject : Page().ids) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.density = r.density; s.focus_indicator = FocusIndicatorMode::always; tweak(s); });
        auto frame = page.frame(r.appearance, r.scale);
        const Placement* item = placed(frame, subject);
        check(item != nullptr, (label_of(r, subject) + " is arranged").c_str());
        check(bool(page.state().focus(subject)), (label_of(r, subject) + " takes focus").c_str());
        auto semantics = page.state().describe(subject);
        check(semantics && semantics->focused, (label_of(r, subject) + " reports focus").c_str());
        Probe probe(page.state(), *item, *semantics, r.appearance, r.scale, true);
        detail::component_type(semantics->kind).paint(probe);
        body(page, frame, *item, *semantics, probe, label_of(r, subject));
    }
}
int ring_px(const Page& page, float scale) { return scaled(scale, page.ui.presentation().focus_ring_width.value_or(3)); }

// What each component frames (ticket item 1). Returns the rectangle the ring must have.
Rect expected_outer(const Placement& item, const Semantics& s, int width_px) {
    auto offset = [](Rect r, Rect origin) { return Rect{r.x + origin.x, r.y + origin.y, r.w, r.h}; };
    if (s.kind == Kind::text) return offset(item.field.surface, item.bounds);
    if (s.kind == Kind::radio_group) {
        std::size_t row = 0;
        bool found = false;
        for (std::size_t i = 0; i < s.options.size(); ++i) if (s.options[i].selected) { row = i; found = true; break; }
        if (!found) for (std::size_t i = 0; i < s.options.size(); ++i) if (s.options[i].enabled) { row = i; break; }
        return item.options[row];
    }
    if (s.kind == Kind::list || s.kind == Kind::tabs) {
        for (std::size_t i = 0; i < s.options.size(); ++i) if (s.candidate && s.options[i].value == *s.candidate) return item.options[i];
        return {};
    }
    if (s.kind == Kind::navigation_list) {
        for (std::size_t i = 0; i < s.options.size(); ++i) if (s.options[i].selected) return item.options[i];
        return {};
    }
    if (s.kind == Kind::slider) return {item.bounds.x, item.bounds.y, item.bounds.w, item.slider_track.y + item.slider_track.h + width_px - item.bounds.y};
    if (s.kind == Kind::label) return item.clip;
    return item.bounds;  // button, checkbox, dropdown
}

void every_component_draws_one_closed_rounded_ring() {
    for (const auto& r : runs)
        for_each_focused(r, [](PresentationSettings&) {}, [&](Page& page, const Frame&, const Placement& item, const Semantics& s, Probe& probe, const std::string& label) {
            const auto ring = find_ring(probe.cmds);
            check(ring.has_value(), (label + ": the focus indicator is a rounded outer + inner pair").c_str());
            if (!ring) return;
            check(probe.count(PaintRole::focus_indicator) == 2, (label + ": and nothing else is marked as focus indicator (no strip, bar, underline or dot)").c_str());
            const int w = ring_px(page, r.scale);
            check(ring->outer.w >= 2 * w + 1 && ring->outer.h >= 2 * w + 1, (label + ": the ring has room for its frame").c_str());
            check(ring->inner.x - ring->outer.x == w && ring->inner.y - ring->outer.y == w &&
                  ring->outer.x + ring->outer.w - (ring->inner.x + ring->inner.w) == w && ring->outer.y + ring->outer.h - (ring->inner.y + ring->inner.h) == w,
                  (label + ": the frame is the same thickness on all four sides").c_str());
            check(ring->color == page.state().effective_palette(r.appearance).focus_color() && ring->color != ring->base, (label + ": the ring is drawn in the focus color").c_str());
            check(ring->outer == expected_outer(item, s, w), (label + ": the ring frames what ticket item 1 says").c_str());
            const int own = s.kind == Kind::navigation_list && r.appearance.name() == "Material-inspired" ? ring->outer.h / 2 : scaled(r.scale, r.appearance.radius());
            const int capped = std::min(own, std::min(ring->outer.w, ring->outer.h) / 2);
            check(ring->outer_radius == capped, (label + ": the default radius is the component's own, a capsule when it is taller").c_str());
            check(ring->inner_radius == std::max(0, ring->outer_radius - w), (label + ": the inner edge is the outer radius minus the thickness").c_str());
            check(s.kind == Kind::label || inside(ring->outer, intersect(item.bounds, item.clip)),
                  (label + ": the ring is drawn inside the component, so a scroll area does not clip it").c_str());
        });
}

// Review item 5: the acceptance line names single-line, multi-line and rich text fields, underline style included. The driver
// must really reach each kind, so a field kind dropped from the page (or an appearance without underline fields) fails here.
void the_driver_reaches_single_line_multi_line_rich_and_underline_text_fields() {
    int single = 0, multi = 0, rich = 0, underline = 0, boxed = 0;
    for (const auto& r : runs)
        for_each_focused(r, [](PresentationSettings&) {}, [&](Page&, const Frame&, const Placement& item, const Semantics& s, Probe& probe, const std::string& label) {
            if (s.kind != Kind::text) return;
            check(find_ring(probe.cmds).has_value(), (label + ": a text field draws its ring").c_str());
            (s.rich ? rich : s.multiline ? multi : single) += 1;
            (item.field.underline ? underline : boxed) += 1;
        });
    check(single > 0 && multi > 0 && rich > 0, "single-line, multi-line and rich text fields are all driven");
    check(underline > 0 && boxed > 0, "so are underline-style and boxed fields (one per appearance)");
}

void the_ring_never_covers_content_and_never_erases_it() {
    for (const auto& r : runs)
        for (int width : {2, 3, 6})
            for_each_focused(r, [&](PresentationSettings& s) { s.focus_ring_width = width; }, [&](Page&, const Frame&, const Placement&, const Semantics& s, Probe& probe, const std::string& base) {
                const auto label = base + " width " + std::to_string(width);
                const auto ring = find_ring(probe.cmds);
                check(ring.has_value(), (label + ": ring present").c_str());
                if (!ring) return;
                check(ring->inner.x - ring->outer.x == scaled(r.scale, width), (label + ": the ring is as thick as the setting").c_str());
                for (std::size_t i = 0; i < probe.cmds.size(); ++i) {
                    if (i == ring->index || i == ring->index + 1) continue;
                    const auto& c = probe.cmds[i];
                    const Rect shown = intersect(c.rect, c.clip);
                    if (shown.w <= 0 || shown.h <= 0) continue;
                    // Before the ring: the ring's inside fill would erase anything that lies in it, unless it is a backdrop wider than the ring.
                    if (i < ring->index) check(disjoint(shown, ring->inner) || inside(ring->outer, shown), (label + ": nothing drawn before the ring lies under it (the ring would erase it)").c_str());
                    // After the ring: it stays clear of the band, and text keeps its whole width inside the ring (a clip must not cut it).
                    else {
                        check(disjoint(shown, ring->outer) || inside(shown, ring->inner), (label + ": nothing drawn after the ring touches its band").c_str());
                        // A vertical label starts at its right edge and scrolls toward the left, so only its right side must be clear.
                        const bool starts_right = s.kind == Kind::label && s.writing_mode == WritingMode::vertical;
                        if (c.kind == Cmd::Kind::text && !disjoint(c.rect, ring->outer))
                            check((starts_right || c.rect.x >= ring->inner.x) && c.rect.x + c.rect.w <= ring->inner.x + ring->inner.w, (label + ": text inside the ring is not cut at its sides").c_str());
                    }
                }
            });
}

// ---- layout, settings, colors and `never` ----------------------------------------------------------

std::vector<int> geometry(const Frame& frame) {
    std::vector<int> v;
    auto add = [&](Rect r) { v.insert(v.end(), {r.x, r.y, r.w, r.h}); };
    for (const auto* list : {&frame.items, &frame.overlays})
        for (const auto& p : *list) {
            add(p.bounds); add(p.clip);
            for (const auto* lines : {&p.lines, &p.label_lines, &p.description_lines, &p.error_lines}) for (const auto& l : *lines) add(l.bounds);
            for (const auto& o : p.options) add(o);
            add(p.slider_track); add(p.progress_track);
            add(p.field.label); add(p.field.surface); add(p.field.editor); add(p.field.description); add(p.field.error);
            add(p.view.text); add(p.view.track);
            add(p.column_layout.track); add(p.column_layout.thumb);
            for (const auto& column : p.column_layout.columns) add(column.bounds);
        }
    return v;
}

void pressing_tab_through_the_page_moves_nothing() {
    for (const auto& r : runs) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.density = r.density; });
        const auto before = geometry(page.frame(r.appearance, r.scale));
        for (std::size_t press = 0; press < page.ids.size() + 2; ++press) {
            page.state().key_down({Key::tab});
            check(page.ui.focus_indicator_visible(), "a key shows the indicator");
            check(geometry(page.frame(r.appearance, r.scale)) == before, (label_of(r, "tab press " + std::to_string(press)) + ": every position is the same with the ring showing").c_str());
        }
    }
}

void a_new_thickness_changes_the_next_frames_ring_and_padding() {
    for (const auto& r : runs) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.density = r.density; });
        auto narrow = page.frame(r.appearance, r.scale);
        std::vector<int> heights;
        for (const auto& id : {"button", "text", "radio_sel", "list", "tabs", "nav", "slider"}) heights.push_back(placed(narrow, id)->bounds.h);
        const auto before = geometry(narrow);
        page.settings([&](PresentationSettings& s) { s.focus_ring_width = 6; });
        auto wide = page.frame(r.appearance, r.scale);
        std::size_t i = 0;
        for (const auto& id : {"button", "text", "radio_sel", "list", "tabs", "nav", "slider"}) {
            const int now = placed(wide, id)->bounds.h;
            check(now >= heights[i], (label_of(r, id) + ": a thicker ring never makes a component smaller").c_str());
            ++i;
        }
        check(geometry(wide) != before, (label_of(r, "page") + ": the next frame is laid out for the new thickness").c_str());
        // The padding follows too: a radio row and a list row grow so that a 6 DIP ring still clears the text, in every run.
        check(placed(wide, "radio_sel")->bounds.h > heights[2], (label_of(r, "radio_sel") + ": option rows grow with the ring").c_str());
    }
}

void a_set_radius_replaces_each_components_own_and_a_large_one_makes_a_capsule() {
    for (const auto& r : runs)
        for (int radius : {0, 10, 32})
            for_each_focused(r, [&](PresentationSettings& s) { s.focus_ring_radius = radius; }, [&](Page& page, const Frame&, const Placement&, const Semantics&, Probe& probe, const std::string& base) {
                const auto label = base + " radius " + std::to_string(radius);
                const auto ring = find_ring(probe.cmds);
                check(ring.has_value(), (label + ": ring present").c_str());
                if (!ring) return;
                const int wanted = static_cast<int>(std::lround(radius * r.scale));
                const int capped = std::min(wanted, std::min(ring->outer.w, ring->outer.h) / 2);
                check(ring->outer_radius == capped, (label + ": the set radius replaces the component's own, capped at a capsule").c_str());
                check(ring->inner_radius == std::max(0, capped - ring_px(page, r.scale)), (label + ": the inner radius is the outer one minus the thickness").c_str());
            });
}

void the_ring_follows_the_focus_color_and_turns_error_colored_when_invalid() {
    for (const auto& variant : variants)
        for (const auto& r : runs) {
            if (r.density != Density::standard || r.scale != 1.f) continue;
            for_each_focused(r, [&](PresentationSettings& s) { s.variant = variant; }, [&](Page& page, const Frame&, const Placement& item, const Semantics& s, Probe& probe, const std::string& base) {
                const auto label = base + " " + variant.name;
                const auto palette = r.appearance.palette(variant);
                const auto ring = find_ring(probe.cmds);
                check(ring && ring->color == palette.focus_color() && palette.focus_color() == palette.accent, (label + ": the ring is drawn in the palette's focus color").c_str());
                auto invalid = s; invalid.invalid = true;
                Probe again(page.state(), item, invalid, r.appearance, r.scale, true);
                detail::component_type(s.kind).paint(again);
                const auto error_ring = find_ring(again.cmds);
                check(error_ring && error_ring->color == palette.error && has_role(error_ring->role, PaintRole::error_indicator), (label + ": an invalid component's ring is the error color").c_str());
            });
        }
}

void an_unfocused_invalid_field_keeps_its_thin_error_frame() {
    for (const auto* appearance : builtins) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.focus_indicator = FocusIndicatorMode::always; });
        auto frame = page.frame(*appearance);
        auto palette = appearance->palette(ThemeVariant::light());
        for (const char* id : {"text", "dropdown"}) {
            auto semantics = *page.state().describe(id);
            semantics.invalid = true; semantics.focused = false;
            Probe probe(page.state(), *placed(frame, id), semantics, *appearance, 1.f, true);
            detail::component_type(semantics.kind).paint(probe);
            check(!find_ring(probe.cmds) && probe.count(PaintRole::focus_indicator) == 0, (std::string(id) + ": no focus ring while unfocused").c_str());
            const bool thin = std::any_of(probe.cmds.begin(), probe.cmds.end(), [&](const Cmd& c) { return has_role(c.role, PaintRole::error_indicator) && c.color == palette.error; });
            check(thin, (std::string(appearance->name()) + " " + id + ": the thin edge (or the underline) is drawn in the error color").c_str());
        }
    }
}

void replacing_focus_or_accent_recolors_the_ring_as_the_ticket_says() {
    for (const auto* appearance : builtins) {
        const std::string name(appearance->name());
        auto nudge = [](Color c) { return Color{static_cast<unsigned char>(c.r == 255 ? 254 : c.r + 1), c.g, c.b}; };
        const auto base = appearance->palette(ThemeVariant::light());
        auto ring_and_thumb = [&](const std::vector<PaletteOverride>& overrides) {
            Page page;
            check(bool(page.ui.set_palette_overrides(overrides)), "override accepted");
            page.settings([](PresentationSettings& s) { s.focus_indicator = FocusIndicatorMode::always; });
            auto frame = page.frame(*appearance);
            check(bool(page.state().focus("slider")), "slider takes focus");
            auto semantics = *page.state().describe("slider");
            Probe probe(page.state(), *placed(frame, "slider"), semantics, *appearance, 1.f, true);
            detail::component_type(Kind::slider).paint(probe);
            auto ring = find_ring(probe.cmds);
            check(ring.has_value(), "slider ring present");
            const auto thumb = std::find_if(probe.cmds.begin(), probe.cmds.end(), [&](const Cmd& c) { return c.kind == Cmd::Kind::rounded && c.role == PaintRole::content && c.rect.w == c.rect.h && c.rect.w > 8; });
            return std::pair<Color, Color>{ring->color, thumb == probe.cmds.end() ? Color{0, 0, 0} : thumb->color};
        };
        auto [plain_ring, plain_thumb] = ring_and_thumb({});
        check(plain_ring == base.accent && plain_thumb == base.accent, (name + ": by default the ring and the thumb are the accent").c_str());
        auto [own_ring, own_thumb] = ring_and_thumb({{name, ThemeVariant::light(), "focus", nudge(base.focus_color())}});
        check(own_ring == nudge(base.focus_color()) && own_thumb == base.accent, (name + ": replacing focus recolors only the ring").c_str());
        auto [moved_ring, moved_thumb] = ring_and_thumb({{name, ThemeVariant::light(), "accent", nudge(base.accent)}});
        check(moved_ring == nudge(base.accent) && moved_thumb == nudge(base.accent), (name + ": replacing accent moves the ring along").c_str());
        auto [both_ring, both_thumb] = ring_and_thumb({{name, ThemeVariant::light(), "accent", nudge(base.accent)}, {name, ThemeVariant::light(), "focus", nudge(nudge(base.focus_color()))}});
        check(both_ring == nudge(nudge(base.focus_color())) && both_thumb == nudge(base.accent), (name + ": an explicit focus is not dragged along by accent").c_str());
        Ui ui;
        auto refused = ui.set_palette_overrides({{name, ThemeVariant::light(), "focus", Color{base.background.r, base.background.g, base.background.b}}});
        check(refused.status == Status::invalid_value, (name + ": a ring color without 3:1 on the page is refused").c_str());
        check(ui.palette_overrides().empty() && *ui.effective_palette(*appearance, ThemeVariant::light()) == settled(base), (name + ": a refused list changes nothing").c_str());
    }
}

// ---- never -----------------------------------------------------------------------------------

void never_draws_no_focus_indicator_on_any_component() {
    for (const auto& r : runs)
        for_each_focused(r, [](PresentationSettings& s) { s.focus_indicator = FocusIndicatorMode::never; }, [&](Page& page, const Frame&, const Placement& item, const Semantics& s, Probe& probe, const std::string& label) {
            check(probe.count(PaintRole::focus_indicator) == 0, (label + ": never draws no focus indicator").c_str());
            check(!find_ring(probe.cmds), (label + ": no ring").c_str());
            check(s.focused && page.state().focused() == s.id, (label + ": focus itself is untouched").c_str());
            check(!page.ui.focus_indicator_visible(), (label + ": the public query says hidden").c_str());
            if (s.kind == Kind::text) check(probe.count(PaintRole::caret) == 1, (label + ": the caret still draws, editable fields included").c_str());
            if (s.kind == Kind::slider) {
                // the thumb stays its plain size: the same as an unfocused slider's
                auto unfocused = s; unfocused.focused = false;
                Probe plain(page.state(), item, unfocused, r.appearance, r.scale, true);
                detail::component_type(Kind::slider).paint(plain);
                auto size = [](const Probe& p) { for (const auto& c : p.cmds) if (c.kind == Cmd::Kind::rounded && c.rect.w == c.rect.h && c.rect.w > 8) return c.rect.w; return 0; };
                check(size(probe) > 0 && size(probe) == size(plain), (label + ": the slider thumb does not grow").c_str());
            }
        });
}

void never_wins_over_keyboard_cues_and_keys_and_editable_text() {
    Page page;
    page.settings([](PresentationSettings& s) { s.focus_indicator = FocusIndicatorMode::never; });
    auto frame = page.frame(web_appearance());
    page.state().set_system_keyboard_cues(true);       // Windows asks for cues everywhere (SPI_GETKEYBOARDCUES)
    page.state().key_down({Key::tab});
    check(bool(page.state().focus("button")), "button takes focus");
    for (const char* id : {"button", "text", "slider"}) {
        check(bool(page.state().focus(id)), "focus moves");
        auto semantics = *page.state().describe(id);
        Probe probe(page.state(), *placed(frame, id), semantics, web_appearance(), 1.f, true);
        detail::component_type(semantics.kind).paint(probe);
        check(probe.count(PaintRole::focus_indicator) == 0 && !page.ui.focus_indicator_visible(), (std::string(id) + ": cues and keys cannot bring it back").c_str());
    }
    page.settings([](PresentationSettings& s) { s.focus_indicator = FocusIndicatorMode::automatic; });
    check(page.ui.focus_indicator_visible(), "leaving never shows it again at once (cues are on)");
}

void never_leaves_the_semantic_layer_and_the_focus_tooltip_alone() {
    Ui ui;
    auto first = ui.add_button({.id = "first", .kind = Kind::button, .label = u"First"}).value.value();
    auto second = ui.add_button({.id = "second", .kind = Kind::button, .label = u"Second"}).value.value();
    auto tip = ui.add_tooltip("second.tip", second, u"Help", 100).value.value();
    auto settings = ui.presentation(); settings.focus_indicator = FocusIndicatorMode::never;
    check(bool(ui.set_presentation(settings)), "never accepted");
    auto& state = detail::access(ui);
    std::vector<SemanticEvent> events;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { events.push_back(e); });
    state.tooltip_time(0); state.window_active(true);
    check(bool(first.focus()), "first takes focus");
    state.key_down({Key::tab});
    ui.semantics().dispatch_events();
    check(second.describe()->focused && ui.semantics().get({"second"})->focused, "UIA and the semantic tree report the new focus");
    check(std::any_of(events.begin(), events.end(), [](const SemanticEvent& e) { return e.kind == SemanticEventKind::focus_changed && e.ref.id == "second"; }), "the focus event is delivered");
    state.tooltip_time(1000);
    check(*tip.visible(), "the tooltip that follows keyboard focus still appears");
    (void)subscription;
}
}
int main() {
    static const test::Case cases[] = {
        {"ring_width_and_radius_accept_their_bounds_and_refuse_the_rest", ring_width_and_radius_accept_their_bounds_and_refuse_the_rest},
        {"focus_is_a_palette_role_equal_to_accent_by_default", focus_is_a_palette_role_equal_to_accent_by_default},
        {"a_custom_appearance_that_assigns_accent_afterwards_keeps_the_ring_on_its_accent", a_custom_appearance_that_assigns_accent_afterwards_keeps_the_ring_on_its_accent},
        {"replacing_accent_moves_focus_unless_focus_is_replaced_too", replacing_accent_moves_focus_unless_focus_is_replaced_too},
        {"focus_needs_three_to_one_on_every_surface", focus_needs_three_to_one_on_every_surface},
        {"every_component_draws_one_closed_rounded_ring", every_component_draws_one_closed_rounded_ring},
        {"the_driver_reaches_single_line_multi_line_rich_and_underline_text_fields", the_driver_reaches_single_line_multi_line_rich_and_underline_text_fields},
        {"the_ring_never_covers_content_and_never_erases_it", the_ring_never_covers_content_and_never_erases_it},
        {"pressing_tab_through_the_page_moves_nothing", pressing_tab_through_the_page_moves_nothing},
        {"a_new_thickness_changes_the_next_frames_ring_and_padding", a_new_thickness_changes_the_next_frames_ring_and_padding},
        {"a_set_radius_replaces_each_components_own_and_a_large_one_makes_a_capsule", a_set_radius_replaces_each_components_own_and_a_large_one_makes_a_capsule},
        {"the_ring_follows_the_focus_color_and_turns_error_colored_when_invalid", the_ring_follows_the_focus_color_and_turns_error_colored_when_invalid},
        {"an_unfocused_invalid_field_keeps_its_thin_error_frame", an_unfocused_invalid_field_keeps_its_thin_error_frame},
        {"replacing_focus_or_accent_recolors_the_ring_as_the_ticket_says", replacing_focus_or_accent_recolors_the_ring_as_the_ticket_says},
        {"never_draws_no_focus_indicator_on_any_component", never_draws_no_focus_indicator_on_any_component},
        {"never_wins_over_keyboard_cues_and_keys_and_editable_text", never_wins_over_keyboard_cues_and_keys_and_editable_text},
        {"never_leaves_the_semantic_layer_and_the_focus_tooltip_alone", never_leaves_the_semantic_layer_and_the_focus_tooltip_alone},
    };
    return test::run("focus_ring", cases);
}
