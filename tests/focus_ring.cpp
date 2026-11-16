// Focus ring (#90): every component draws its focus indicator as one closed rounded frame whose
// thickness, radius, color and visibility the code controls.
//
// Seams under test (confirmed by the ticket's Agent Brief):
//   - Ui::setPresentation / PresentationSettings::{focusRingWidth, focusRingRadius, focusIndicator}
//   - Palette::focus, Ui::setPaletteOverrides / effectivePalette, checkPaletteContrast
//   - detail::componentType(kind).paint(ComponentPaint&) observed through a recording probe
//   - arrange(...) placements (layout must not move when the ring appears)
//   - detail::UiState::keyDown / setSystemKeyboardCues (when the indicator shows)
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include "focus_marks.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&webAppearance(), &materialAppearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string nameOf(const Appearance& a) { return std::string(a.name()); }
// Ui::effectivePalette settles an empty `focus` to the accent; a palette straight from an appearance has not been settled yet.
Palette settled(Palette p) { p.focus = p.focusColor(); return p; }

// ---- presentation settings -------------------------------------------------------------------

void ringWidthAndRadiusAcceptTheirBoundsAndRefuseTheRest() {
    Ui ui;
    check(!ui.presentation().focusRingWidth && !ui.presentation().focusRingRadius, "both are empty by default (the appearance decides)");
    auto set = [&](std::optional<int> width, std::optional<int> radius) {
        auto settings = ui.presentation(); settings.focusRingWidth = width; settings.focusRingRadius = radius;
        return ui.setPresentation(settings);
    };
    for (int width : {2, 3, 6}) check(bool(set(width, std::nullopt)) && ui.presentation().focusRingWidth == width, "2-6 DIP are accepted");
    check(bool(set(std::nullopt, std::nullopt)) && !ui.presentation().focusRingWidth, "empty again");
    for (int radius : {0, 1, 32}) check(bool(set(std::nullopt, radius)) && ui.presentation().focusRingRadius == radius, "0-32 DIP are accepted");
    check(bool(set(4, 9)), "a valid pair is accepted");
    const auto before = ui.presentation();
    for (int width : {1, 7, 0, -3, 100}) {
        auto bad = ui.setPresentation([&] { auto s = before; s.focusRingWidth = width; s.textSize = 20; return s; }());
        check(bad.status == Status::invalidValue, "a ring width outside 2-6 is invalid_value");
        check(ui.presentation() == before, "a refused setting changes nothing, other fields of the same call included");
    }
    for (int radius : {-1, 33, -100, 1000}) {
        auto bad = ui.setPresentation([&] { auto s = before; s.focusRingRadius = radius; s.textSize = 20; return s; }());
        check(bad.status == Status::invalidValue, "a ring radius outside 0-32 is invalid_value");
        check(ui.presentation() == before, "a refused radius changes nothing");
    }
    check(!(before == PresentationSettings{}), "operator== covers the two fields");
}

// ---- the `focus` palette role ----------------------------------------------------------------

void focusIsAPaletteRoleEqualToAccentByDefault() {
    auto roles = paletteRoles();
    check(std::find(roles.begin(), roles.end(), "focus") != roles.end(), "focus is a named role");
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto p = appearance->palette(variant);
            check(!p.focus && p.focusColor() == p.accent, (nameOf(*appearance) + " " + variant.name + ": focus defaults to accent").c_str());
        }
    Palette p = webAppearance().palette();
    check(setPaletteValue(p, "focus", Color{1, 2, 3}) && p.focus == Color{1, 2, 3}, "focus is settable by name");
    check(!setPaletteValue(p, "focus", 5), "focus takes a color, not an int");
}

void replacingAccentMovesFocusUnlessFocusIsReplacedToo() {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            const auto label = nameOf(*appearance) + " " + variant.name;
            const auto base = appearance->palette(variant);
            // A dark variant needs light colors to stay readable; derive from the built-in accent by a small nudge instead of fixed values.
            Color nudged{static_cast<unsigned char>(base.accent.r == 255 ? 254 : base.accent.r + 1), base.accent.g, base.accent.b};
            Ui onlyAccent;
            check(bool(onlyAccent.setPaletteOverrides({{std::string(appearance->name()), variant, "accent", nudged}})), (label + ": accent override accepted").c_str());
            auto p = *onlyAccent.effectivePalette(*appearance, variant);
            check(p.accent == nudged && p.focus == nudged, (label + ": focus follows the replaced accent").c_str());
            Ui onlyFocus;
            Color own{static_cast<unsigned char>(base.accent.r == 255 ? 253 : base.accent.r + 2), base.accent.g, base.accent.b};
            check(bool(onlyFocus.setPaletteOverrides({{std::string(appearance->name()), variant, "focus", own}})), (label + ": focus override accepted").c_str());
            p = *onlyFocus.effectivePalette(*appearance, variant);
            check(p.focus == own && p.accent == base.accent, (label + ": replacing focus alone leaves accent alone").c_str());
            Ui both;
            check(bool(both.setPaletteOverrides({{std::string(appearance->name()), variant, "accent", nudged}, {std::string(appearance->name()), variant, "focus", own}})), (label + ": both accepted").c_str());
            p = *both.effectivePalette(*appearance, variant);
            check(p.focus == own && p.accent == nudged, (label + ": an explicit focus wins over the follow rule").c_str());
        }
}

// Review item 2: "focus not set => follows accent" holds for every appearance, not only inside one brace initializer.
struct AccentAfterwards final : Appearance {
    // A custom appearance that starts from the Web palette and then assigns its own accent (and maybe focus).
    explicit AccentAfterwards(std::optional<Color> ownFocus = std::nullopt) : ownFocus_(ownFocus) {}
    static constexpr Color accent{10, 80, 160};
    std::string_view name() const override { return "Accent afterwards"; }
    Palette palette() const override {
        Palette p = webAppearance().palette(); p.accent = accent; p.onAccent = {255, 255, 255};
        if (ownFocus_) p.focus = *ownFocus_;
        return p;
    }
    int radius() const override { return 6; }
    TextFieldLayout arrangeTextField(int w, const TextFieldHeights& t, float s) const override { return webAppearance().arrangeTextField(w, t, s); }
    int buttonHeight(int l, float s) const override { return webAppearance().buttonHeight(l, s); }
    int buttonPadding(float s) const override { return webAppearance().buttonPadding(s); }
    std::optional<Color> ownFocus_;
};
void aCustomAppearanceThatAssignsAccentAfterwardsKeepsTheRingOnItsAccent() {
    Ui ui;
    AccentAfterwards custom;
    auto p = ui.effectivePalette(custom, ThemeVariant::light());
    check(p && p->accent == AccentAfterwards::accent && p->focus == AccentAfterwards::accent, "focus left out follows the accent the custom appearance assigned later");
    Ui overridden;
    check(bool(overridden.setPaletteOverrides({{"Minimal Web", ThemeVariant::light(), "accent", Color{20, 110, 50}}})), "an accent override on a built-in is accepted");
    p = overridden.effectivePalette(custom, ThemeVariant::light());
    check(p && p->focus == AccentAfterwards::accent, "overrides never touch a custom appearance, its ring still follows its own accent");
    const Color own{120, 40, 160};
    AccentAfterwards explicitFocus{own};
    p = ui.effectivePalette(explicitFocus, ThemeVariant::light());
    check(p && p->accent == AccentAfterwards::accent && p->focus == own, "a custom appearance that sets focus itself keeps it");
}

void focusNeedsThreeToOneOnEverySurface() {
    for (const auto* appearance : builtins)
        for (const auto& variant : variants) {
            auto p = appearance->palette(variant);
            const auto label = nameOf(*appearance) + " " + variant.name;
            for (auto surface : {p.background, p.panel, p.surface})
                check(contrastRatio(p.focusColor(), surface) >= 3.0, (label + ": the built-in ring color is 3:1 on every surface").c_str());
            check(checkPaletteContrast(p).empty(), (label + ": the built-in palette passes every rule").c_str());
        }
    // The just-passing / just-failing literals per surface live with the other contrast rules (tests/palette_override.cpp).
}


// ---- the generic driver: every component kind, both appearances, both densities, two scales -------

using detail::PaintRole;
bool hasRole(PaintRole value, PaintRole bit) { return (static_cast<unsigned>(value) & static_cast<unsigned>(bit)) != 0; }
int scaled(float scale, int dip) { return std::max(1, static_cast<int>(std::lround(dip * scale))); }
bool inside(Rect part, Rect whole) { return part.x >= whole.x && part.y >= whole.y && part.x + part.w <= whole.x + whole.w && part.y + part.h <= whole.y + whole.h; }
bool disjoint(Rect a, Rect b) { auto i = intersect(a, b); return i.w <= 0 || i.h <= 0; }

struct Cmd { enum class Kind { fill, rounded, text } kind; Rect rect; int radius; Color color; PaintRole role; Rect clip; };
struct Probe final : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<Cmd> cmds;
    Rect clipRect{-100000, -100000, 200000, 200000};
    PaintRole current = PaintRole::content;
    test::FakeMeasure measure;
    void role(PaintRole value) override { current = value; }
    void fill(Rect rect, Color color) override { cmds.push_back({Cmd::Kind::fill, rect, 0, color, current, clipRect}); }
    void rounded(Rect rect, int radius, Color color) override { cmds.push_back({Cmd::Kind::rounded, rect, radius, color, current, clipRect}); }
    void clip(Rect rect) override { clipRect = intersect(clipRect, rect); }
    void text(std::u16string_view value, int pixels, int x, int y, Color color) override {
        if (value.empty()) return;
        const int width = static_cast<int>(std::ceil(measure.measure(value, pixels).width));
        cmds.push_back({Cmd::Kind::text, {x, y, width, measure.lineHeight(pixels)}, 0, color, current, clipRect});
    }
    void verticalText(std::u16string_view value, VerticalForm, int, Rect cell, Color color) override {
        if (!value.empty()) cmds.push_back({Cmd::Kind::text, cell, 0, color, current, clipRect});
    }
    int count(PaintRole bit) const { return static_cast<int>(std::count_if(cmds.begin(), cmds.end(), [&](const Cmd& c) { return hasRole(c.role, bit); })); }
};

// The ring is the pair of rounded commands marked focusIndicator: the outer one in the ring color, the inner one in the
// color of what the ring frames. Anything else marked focusIndicator (a strip, a dot) is not a ring.
struct Ring { std::size_t index; Rect outer, inner; int outerRadius, innerRadius; Color color, base; PaintRole role; };
std::optional<Ring> findRing(const std::vector<Cmd>& cmds) {
    for (std::size_t i = 0; i + 1 < cmds.size(); ++i)
        if (hasRole(cmds[i].role, PaintRole::focusIndicator) && cmds[i].kind == Cmd::Kind::rounded &&
            hasRole(cmds[i + 1].role, PaintRole::focusIndicator) && cmds[i + 1].kind == Cmd::Kind::rounded)
            return Ring{i, cmds[i].rect, cmds[i + 1].rect, cmds[i].radius, cmds[i + 1].radius, cmds[i].color, cmds[i + 1].color, cmds[i].role};
    return std::nullopt;
}

struct Page {
    Ui ui; ScrollState scroll, labelScroll; test::FakeMeasure measure;
    std::vector<layout::Node> nodes;
    std::vector<std::string> ids;
    Page() {
        auto add = [&](std::string id, layout::Node node) { ids.push_back(std::move(id)); nodes.push_back(std::move(node)); };
        auto text = ui.addTextField({.id = "text", .label = u"Name", .value = u"abc"}).value.value();
        add("text", layout::component(text));
        auto note = ui.addTextField({.id = "note", .label = u"Notes", .value = u"a\nb\nc\nd\ne", .multiline = true, .visibleLines = 3}).value.value();
        add("note", layout::component(note));
        auto rich = ui.addTextField({.id = "rich", .label = u"Description", .value = u"bold plain", .multiline = true, .visibleLines = 3, .rich = true}).value.value();
        add("rich", layout::component(rich));
        auto dropdown = ui.addDropdown({.id = "dropdown", .label = u"Region"}, {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
        add("dropdown", layout::component(dropdown));
        auto box = ui.addCheckbox({.id = "box", .label = u"Remember me"}).value.value();
        add("box", layout::component(box));
        auto button = ui.addButton({.id = "button", .label = u"Go"}).value.value();
        add("button", layout::component(button));
        auto radioNone = ui.addRadioGroup({.id = "radio_none", .label = u"Mode"}, {{{"a"}, u"A", false}, {{"b"}, u"B"}, {{"c"}, u"C"}}).value.value();
        add("radio_none", layout::component(radioNone));
        auto radioSel = ui.addRadioGroup({.id = "radio_sel", .label = u"Size"}, {{{"a"}, u"A"}, {{"b"}, u"B"}, {{"c"}, u"C"}}).value.value();
        radioSel.connectSource({[] { return std::optional<OptionValue>{}; }, [](auto) { return [] {}; }});
        radioSel.connectImmediate([](const Change<RadioGroupHandle>&) { return Result{}; });
        check(bool(radioSel.select({"b"})), "a radio option is selected");
        add("radio_sel", layout::component(radioSel));
        auto slider = ui.addSlider({.id = "slider", .label = u"Volume"}, {0, 100, 5}).value.value();
        add("slider", layout::component(slider));
        auto list = ui.addList({.id = "list", .label = u"Items"}, {{{"i1"}, u"One"}, {{"i2"}, u"Two"}, {{"i3"}, u"Three"}}).value.value();
        list.connectSource({[] { return std::optional<OptionValue>{}; }, [](auto) { return [] {}; }});
        list.connectImmediate([](const Change<ListHandle>&) { return Result{}; });
        check(bool(list.select({"i2"})), "a list row is selected");
        add("list", layout::component(list));
        auto tabs = ui.addTabs({.id = "tabs", .label = u"Pages"}, {{{"t1"}, u"One", true, {}}, {{"t2"}, u"Two", true, {}}}).value.value();
        add("tabs", layout::component(tabs));
        auto nav = ui.addNavigationList({.id = "nav"}, {{{"n1"}, u"General", true, {}}, {{"n2"}, u"Other", true, {}}}, {"n1"}).value.value();
        add("nav", layout::component(nav));
        auto scrolled = ui.addLabel({.id = "scroll_text", .value = std::u16string(1200, u'x')}).value.value();
        add("scroll_text", layout::scrollText(scrolled, labelScroll, 60));
        auto vertical = ui.addLabel({.id = "vertical", .value = std::u16string(900, u'字'), .writingMode = WritingMode::vertical}).value.value();
        add("vertical", layout::verticalText(vertical, 120));
    }
    detail::UiState& state() { return detail::access(ui); }
    void settings(const std::function<void(PresentationSettings&)>& change) {
        auto value = ui.presentation(); change(value);
        check(bool(ui.setPresentation(value)), "presentation accepted");
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
    {webAppearance(), Density::standard, 1.f}, {webAppearance(), Density::compact, 1.f}, {webAppearance(), Density::standard, 1.5f}, {webAppearance(), Density::compact, 1.5f},
    {materialAppearance(), Density::standard, 1.f}, {materialAppearance(), Density::compact, 1.f}, {materialAppearance(), Density::standard, 1.5f}, {materialAppearance(), Density::compact, 1.5f},
};
std::string labelOf(const Run& r, const std::string& id) {
    return std::string(r.appearance.name()) + (r.density == Density::compact ? " compact " : " standard ") + std::to_string(r.scale) + " " + id;
}
// Calls body(page, frame, placement, semantics, painted probe, label) for every subject focused in turn.
// `always` shows the indicator after programmatic focus.
template <class Body>
void forEachFocused(const Run& r, const std::function<void(PresentationSettings&)>& tweak, Body&& body) {
    for (const auto& subject : Page().ids) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.density = r.density; s.focusIndicator = FocusIndicatorMode::always; tweak(s); });
        auto frame = page.frame(r.appearance, r.scale);
        const Placement* item = placed(frame, subject);
        check(item != nullptr, (labelOf(r, subject) + " is arranged").c_str());
        check(bool(page.state().focus(subject)), (labelOf(r, subject) + " takes focus").c_str());
        auto semantics = page.state().describe(subject);
        check(semantics && semantics->focused, (labelOf(r, subject) + " reports focus").c_str());
        Probe probe(page.state(), *item, *semantics, r.appearance, r.scale, true);
        detail::componentType(semantics->kind).paint(probe);
        body(page, frame, *item, *semantics, probe, labelOf(r, subject));
    }
}
int ringPx(const Page& page, float scale) { return scaled(scale, page.ui.presentation().focusRingWidth.value_or(3)); }

// What each component frames (ticket item 1), worked out from the page fixture and the placement's own rectangles, never from the
// rules production uses to choose a row or to size the ring. Returns the rectangle the ring must have; an empty result for the
// slider, whose ring has no single rectangle in the placement (see the slider check in the driver).
std::optional<Rect> expectedOuter(const std::string& id, const Placement& item, const ComponentSnapshot& s) {
    auto offset = [](Rect r, Rect origin) { return Rect{r.x + origin.x, r.y + origin.y, r.w, r.h}; };
    // The row of one option, found by its value in the semantic option list.
    auto row = [&](const char* value) { for (std::size_t i = 0; i < s.items.size(); ++i) if (s.items[i].value == OptionValue{value}) return item.options[i]; return Rect{}; };
    if (s.kind == ComponentKind::textField) return offset(item.textField.surface, item.bounds);
    // radioNone has nothing selected and its first option "a" is disabled: the ring is on "b", the first one that can be chosen.
    // radioSel has "b" selected. A list and tabs frame their candidate row, navigation its selected row: both are state the
    // semantic layer reports, not a rule of the painter.
    if (id == "radio_none" || id == "radio_sel") return row("b");
    if (s.kind == ComponentKind::list || s.kind == ComponentKind::tabs) return s.candidate ? row(s.candidate->id.c_str()) : Rect{};
    if (s.kind == ComponentKind::navigationList) { for (const auto& option : s.items) if (option.selected) return row(option.value.id.c_str()); return Rect{}; }
    if (s.kind == ComponentKind::slider) return std::nullopt;
    if (s.kind == ComponentKind::label) return item.clip;
    return item.bounds;  // button, checkbox, dropdown
}

void everyComponentDrawsOneClosedRoundedRing() {
    for (const auto& r : runs)
        forEachFocused(r, [](PresentationSettings&) {}, [&](Page& page, const Frame&, const Placement& item, const ComponentSnapshot& s, Probe& probe, const std::string& label) {
            const auto ring = findRing(probe.cmds);
            check(ring.has_value(), (label + ": the focus indicator is a rounded outer + inner pair").c_str());
            if (!ring) return;
            check(focusMarks::isOneRing(probe.count(PaintRole::focusIndicator)), (label + ": and nothing else is marked as focus indicator (no strip, bar, underline or dot)").c_str());
            const int w = ringPx(page, r.scale);
            check(ring->outer.w >= 2 * w + 1 && ring->outer.h >= 2 * w + 1, (label + ": the ring has room for its frame").c_str());
            check(ring->inner.x - ring->outer.x == w && ring->inner.y - ring->outer.y == w &&
                  ring->outer.x + ring->outer.w - (ring->inner.x + ring->inner.w) == w && ring->outer.y + ring->outer.h - (ring->inner.y + ring->inner.h) == w,
                  (label + ": the frame is the same thickness on all four sides").c_str());
            check(ring->color == page.state().effectivePalette(r.appearance).focusColor() && ring->color != ring->base, (label + ": the ring is drawn in the focus color").c_str());
            if (const auto wanted = expectedOuter(s.id, item, s)) check(ring->outer == *wanted, (label + ": the ring frames what ticket item 1 says").c_str());
            else {
                // The slider: the ring encloses the title line and the track, stays inside the component, and the thumb is not under its band.
                bool textsInside = true; int texts = 0;
                for (const auto& c : probe.cmds) if (c.kind == Cmd::Kind::text) { ++texts; textsInside = textsInside && inside(c.rect, ring->inner); }
                check(texts > 0 && textsInside && inside(item.sliderTrack, ring->inner) && inside(ring->outer, item.bounds), (label + ": the slider's ring encloses its title and its track and stays inside the slider").c_str());
            }
            const int own = s.kind == ComponentKind::navigationList && r.appearance.name() == "Material-inspired" ? ring->outer.h / 2 : scaled(r.scale, r.appearance.radius());
            const int capped = std::min(own, std::min(ring->outer.w, ring->outer.h) / 2);
            check(ring->outerRadius == capped, (label + ": the default radius is the component's own, a capsule when it is taller").c_str());
            check(ring->innerRadius == std::max(0, ring->outerRadius - w), (label + ": the inner edge is the outer radius minus the thickness").c_str());
            check(s.kind == ComponentKind::label || inside(ring->outer, intersect(item.bounds, item.clip)),
                  (label + ": the ring is drawn inside the component, so a scroll area does not clip it").c_str());
            if (s.kind == ComponentKind::slider) {
                int size = 0; for (const auto& c : probe.cmds) if (c.kind == Cmd::Kind::rounded && focusMarks::isThumb(c.rect)) size = c.rect.w;
                check(size == focusMarks::enlargedThumb(r.appearance, r.scale), (label + ": the thumb is enlarged while the ring shows").c_str());
            }
        });
}

// Review item 5: the acceptance line names single-line, multi-line and rich text fields, underline style included. The driver
// must really reach each kind, so a field kind dropped from the page (or an appearance without underline fields) fails here.
void theDriverReachesSingleLineMultiLineRichAndUnderlineTextFields() {
    int single = 0, multi = 0, rich = 0, underline = 0, boxed = 0;
    for (const auto& r : runs)
        forEachFocused(r, [](PresentationSettings&) {}, [&](Page&, const Frame&, const Placement& item, const ComponentSnapshot& s, Probe& probe, const std::string& label) {
            if (s.kind != ComponentKind::textField) return;
            check(findRing(probe.cmds).has_value(), (label + ": a text field draws its ring").c_str());
            (s.rich ? rich : s.multiline ? multi : single) += 1;
            (item.textField.underline ? underline : boxed) += 1;
        });
    check(single > 0 && multi > 0 && rich > 0, "single-line, multi-line and rich text fields are all driven");
    check(underline > 0 && boxed > 0, "so are underline-style and boxed fields (one per appearance)");
}

void theRingNeverCoversContentAndNeverErasesIt() {
    for (const auto& r : runs)
        for (int width : {2, 3, 6})
            forEachFocused(r, [&](PresentationSettings& s) { s.focusRingWidth = width; }, [&](Page&, const Frame&, const Placement&, const ComponentSnapshot& s, Probe& probe, const std::string& base) {
                const auto label = base + " width " + std::to_string(width);
                const auto ring = findRing(probe.cmds);
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
                        const bool startsRight = s.kind == ComponentKind::label && s.writingMode == WritingMode::vertical;
                        if (c.kind == Cmd::Kind::text && !disjoint(c.rect, ring->outer))
                            check((startsRight || c.rect.x >= ring->inner.x) && c.rect.x + c.rect.w <= ring->inner.x + ring->inner.w, (label + ": text inside the ring is not cut at its sides").c_str());
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
            for (const auto* lines : {&p.lines, &p.labelLines, &p.descriptionLines, &p.errorLines}) for (const auto& l : *lines) add(l.bounds);
            for (const auto& o : p.options) add(o);
            add(p.sliderTrack); add(p.progressTrack);
            add(p.textField.label); add(p.textField.surface); add(p.textField.editor); add(p.textField.description); add(p.textField.error);
            add(p.view.text); add(p.view.track);
            add(p.columnLayout.track); add(p.columnLayout.thumb);
            for (const auto& column : p.columnLayout.columns) add(column.bounds);
        }
    return v;
}

void pressingTabThroughThePageMovesNothing() {
    for (const auto& r : runs) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.density = r.density; });
        const auto before = geometry(page.frame(r.appearance, r.scale));
        for (std::size_t press = 0; press < page.ids.size() + 2; ++press) {
            page.state().keyDown({Key::tab});
            check(page.ui.focusIndicatorVisible(), "a key shows the indicator");
            check(geometry(page.frame(r.appearance, r.scale)) == before, (labelOf(r, "tab press " + std::to_string(press)) + ": every position is the same with the ring showing").c_str());
        }
    }
}

void aNewThicknessChangesTheNextFramesRingAndPadding() {
    for (const auto& r : runs) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.density = r.density; });
        auto narrow = page.frame(r.appearance, r.scale);
        std::vector<int> heights;
        for (const auto& id : {"button", "text", "radio_sel", "list", "tabs", "nav", "slider"}) heights.push_back(placed(narrow, id)->bounds.h);
        const auto before = geometry(narrow);
        page.settings([&](PresentationSettings& s) { s.focusRingWidth = 6; });
        auto wide = page.frame(r.appearance, r.scale);
        std::size_t i = 0;
        for (const auto& id : {"button", "text", "radio_sel", "list", "tabs", "nav", "slider"}) {
            const int now = placed(wide, id)->bounds.h;
            check(now >= heights[i], (labelOf(r, id) + ": a thicker ring never makes a component smaller").c_str());
            ++i;
        }
        check(geometry(wide) != before, (labelOf(r, "page") + ": the next frame is laid out for the new thickness").c_str());
        // The padding follows too: a radio row and a list row grow so that a 6 DIP ring still clears the text, in every run.
        check(placed(wide, "radio_sel")->bounds.h > heights[2], (labelOf(r, "radio_sel") + ": option rows grow with the ring").c_str());
    }
}

void aSetRadiusReplacesEachComponentsOwnAndALargeOneMakesACapsule() {
    for (const auto& r : runs)
        for (int radius : {0, 10, 32})
            forEachFocused(r, [&](PresentationSettings& s) { s.focusRingRadius = radius; }, [&](Page& page, const Frame&, const Placement&, const ComponentSnapshot&, Probe& probe, const std::string& base) {
                const auto label = base + " radius " + std::to_string(radius);
                const auto ring = findRing(probe.cmds);
                check(ring.has_value(), (label + ": ring present").c_str());
                if (!ring) return;
                const int wanted = static_cast<int>(std::lround(radius * r.scale));
                const int capped = std::min(wanted, std::min(ring->outer.w, ring->outer.h) / 2);
                check(ring->outerRadius == capped, (label + ": the set radius replaces the component's own, capped at a capsule").c_str());
                check(ring->innerRadius == std::max(0, capped - ringPx(page, r.scale)), (label + ": the inner radius is the outer one minus the thickness").c_str());
            });
}

void theRingFollowsTheFocusColorAndTurnsErrorColoredWhenInvalid() {
    for (const auto& variant : variants)
        for (const auto& r : runs) {
            if (r.density != Density::standard || r.scale != 1.f) continue;
            forEachFocused(r, [&](PresentationSettings& s) { s.variant = variant; }, [&](Page& page, const Frame&, const Placement& item, const ComponentSnapshot& s, Probe& probe, const std::string& base) {
                const auto label = base + " " + variant.name;
                const auto palette = r.appearance.palette(variant);
                const auto ring = findRing(probe.cmds);
                check(ring && ring->color == palette.focusColor() && palette.focusColor() == palette.accent, (label + ": the ring is drawn in the palette's focus color").c_str());
                auto invalid = s; invalid.invalid = true;
                Probe again(page.state(), item, invalid, r.appearance, r.scale, true);
                detail::componentType(s.kind).paint(again);
                const auto errorRing = findRing(again.cmds);
                check(errorRing && errorRing->color == palette.error && hasRole(errorRing->role, PaintRole::errorIndicator), (label + ": an invalid component's ring is the error color").c_str());
            });
        }
}

void anUnfocusedInvalidFieldKeepsItsThinErrorFrame() {
    for (const auto* appearance : builtins) {
        Page page;
        page.settings([&](PresentationSettings& s) { s.focusIndicator = FocusIndicatorMode::always; });
        auto frame = page.frame(*appearance);
        auto palette = appearance->palette(ThemeVariant::light());
        for (const char* id : {"text", "dropdown"}) {
            auto semantics = *page.state().describe(id);
            semantics.invalid = true; semantics.focused = false;
            Probe probe(page.state(), *placed(frame, id), semantics, *appearance, 1.f, true);
            detail::componentType(semantics.kind).paint(probe);
            check(!findRing(probe.cmds) && probe.count(PaintRole::focusIndicator) == 0, (std::string(id) + ": no focus ring while unfocused").c_str());
            const bool thin = std::any_of(probe.cmds.begin(), probe.cmds.end(), [&](const Cmd& c) { return hasRole(c.role, PaintRole::errorIndicator) && c.color == palette.error; });
            check(thin, (std::string(appearance->name()) + " " + id + ": the thin edge (or the underline) is drawn in the error color").c_str());
        }
    }
}

void replacingFocusOrAccentRecolorsTheRingAsTheTicketSays() {
    for (const auto* appearance : builtins) {
        const std::string name(appearance->name());
        auto nudge = [](Color c) { return Color{static_cast<unsigned char>(c.r == 255 ? 254 : c.r + 1), c.g, c.b}; };
        const auto base = appearance->palette(ThemeVariant::light());
        auto ringAndThumb = [&](const std::vector<PaletteOverride>& overrides) {
            Page page;
            check(bool(page.ui.setPaletteOverrides(overrides)), "override accepted");
            page.settings([](PresentationSettings& s) { s.focusIndicator = FocusIndicatorMode::always; });
            auto frame = page.frame(*appearance);
            check(bool(page.state().focus("slider")), "slider takes focus");
            auto semantics = *page.state().describe("slider");
            Probe probe(page.state(), *placed(frame, "slider"), semantics, *appearance, 1.f, true);
            detail::componentType(ComponentKind::slider).paint(probe);
            auto ring = findRing(probe.cmds);
            check(ring.has_value(), "slider ring present");
            const auto thumb = std::find_if(probe.cmds.begin(), probe.cmds.end(), [&](const Cmd& c) { return c.kind == Cmd::Kind::rounded && c.role == PaintRole::content && focusMarks::isThumb(c.rect); });
            return std::pair<Color, Color>{ring->color, thumb == probe.cmds.end() ? Color{0, 0, 0} : thumb->color};
        };
        auto [plainRing, plainThumb] = ringAndThumb({});
        check(plainRing == base.accent && plainThumb == base.accent, (name + ": by default the ring and the thumb are the accent").c_str());
        auto [ownRing, ownThumb] = ringAndThumb({{name, ThemeVariant::light(), "focus", nudge(base.focusColor())}});
        check(ownRing == nudge(base.focusColor()) && ownThumb == base.accent, (name + ": replacing focus recolors only the ring").c_str());
        auto [movedRing, movedThumb] = ringAndThumb({{name, ThemeVariant::light(), "accent", nudge(base.accent)}});
        check(movedRing == nudge(base.accent) && movedThumb == nudge(base.accent), (name + ": replacing accent moves the ring along").c_str());
        auto [bothRing, bothThumb] = ringAndThumb({{name, ThemeVariant::light(), "accent", nudge(base.accent)}, {name, ThemeVariant::light(), "focus", nudge(nudge(base.focusColor()))}});
        check(bothRing == nudge(nudge(base.focusColor())) && bothThumb == nudge(base.accent), (name + ": an explicit focus is not dragged along by accent").c_str());
        Ui ui;
        auto refused = ui.setPaletteOverrides({{name, ThemeVariant::light(), "focus", Color{base.background.r, base.background.g, base.background.b}}});
        check(refused.status == Status::invalidValue, (name + ": a ring color without 3:1 on the page is refused").c_str());
        check(ui.paletteOverrides().empty() && *ui.effectivePalette(*appearance, ThemeVariant::light()) == settled(base), (name + ": a refused list changes nothing").c_str());
    }
}

// ---- never -----------------------------------------------------------------------------------

void neverDrawsNoFocusIndicatorOnAnyComponent() {
    for (const auto& r : runs)
        forEachFocused(r, [](PresentationSettings& s) { s.focusIndicator = FocusIndicatorMode::never; }, [&](Page& page, const Frame&, const Placement& item, const ComponentSnapshot& s, Probe& probe, const std::string& label) {
            check(probe.count(PaintRole::focusIndicator) == 0, (label + ": never draws no focus indicator").c_str());
            check(!findRing(probe.cmds), (label + ": no ring").c_str());
            check(s.focused && page.state().focused() == s.id, (label + ": focus itself is untouched").c_str());
            check(!page.ui.focusIndicatorVisible(), (label + ": the public query says hidden").c_str());
            if (s.kind == ComponentKind::textField) check(probe.count(PaintRole::caret) == 1, (label + ": the caret still draws, editable fields included").c_str());
            if (s.kind == ComponentKind::slider) {
                // the thumb stays its plain size
                int size = 0; for (const auto& c : probe.cmds) if (c.kind == Cmd::Kind::rounded && focusMarks::isThumb(c.rect)) size = c.rect.w;
                check(size == focusMarks::plainThumb(r.appearance, r.scale), (label + ": the slider thumb does not grow").c_str());
            }
        });
}

void neverWinsOverKeyboardCuesAndKeysAndEditableText() {
    Page page;
    page.settings([](PresentationSettings& s) { s.focusIndicator = FocusIndicatorMode::never; });
    auto frame = page.frame(webAppearance());
    page.state().setSystemKeyboardCues(true);       // Windows asks for cues everywhere (SPI_GETKEYBOARDCUES)
    page.state().keyDown({Key::tab});
    check(bool(page.state().focus("button")), "button takes focus");
    for (const char* id : {"button", "text", "slider"}) {
        check(bool(page.state().focus(id)), "focus moves");
        auto semantics = *page.state().describe(id);
        Probe probe(page.state(), *placed(frame, id), semantics, webAppearance(), 1.f, true);
        detail::componentType(semantics.kind).paint(probe);
        check(probe.count(PaintRole::focusIndicator) == 0 && !page.ui.focusIndicatorVisible(), (std::string(id) + ": cues and keys cannot bring it back").c_str());
    }
    page.settings([](PresentationSettings& s) { s.focusIndicator = FocusIndicatorMode::automatic; });
    check(page.ui.focusIndicatorVisible(), "leaving never shows it again at once (cues are on)");
}

void neverLeavesTheSemanticLayerAndTheFocusTooltipAlone() {
    Ui ui;
    auto first = ui.addButton({.id = "first", .label = u"First"}).value.value();
    auto second = ui.addButton({.id = "second", .label = u"Second"}).value.value();
    auto tip = ui.addTooltip("second.tip", second, u"Help", 100).value.value();
    auto settings = ui.presentation(); settings.focusIndicator = FocusIndicatorMode::never;
    check(bool(ui.setPresentation(settings)), "never accepted");
    auto& state = detail::access(ui);
    std::vector<SemanticEvent> events;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { events.push_back(e); });
    state.tooltipTime(0); state.windowActive(true);
    check(bool(first.focus()), "first takes focus");
    state.keyDown({Key::tab});
    ui.semantics().dispatchEvents();
    check(second.describe()->focused && ui.semantics().get({"second"})->focused, "UIA and the semantic tree report the new focus");
    check(std::any_of(events.begin(), events.end(), [](const SemanticEvent& e) { return e.kind == SemanticEventKind::focusChanged && e.ref.id == "second"; }), "the focus event is delivered");
    state.tooltipTime(1000);
    check(*tip.visible(), "the tooltip that follows keyboard focus still appears");
    (void)subscription;
}
}
int main() {
    static const test::Case cases[] = {
        {"ring_width_and_radius_accept_their_bounds_and_refuse_the_rest", ringWidthAndRadiusAcceptTheirBoundsAndRefuseTheRest},
        {"focus_is_a_palette_role_equal_to_accent_by_default", focusIsAPaletteRoleEqualToAccentByDefault},
        {"a_custom_appearance_that_assigns_accent_afterwards_keeps_the_ring_on_its_accent", aCustomAppearanceThatAssignsAccentAfterwardsKeepsTheRingOnItsAccent},
        {"replacing_accent_moves_focus_unless_focus_is_replaced_too", replacingAccentMovesFocusUnlessFocusIsReplacedToo},
        {"focus_needs_three_to_one_on_every_surface", focusNeedsThreeToOneOnEverySurface},
        {"every_component_draws_one_closed_rounded_ring", everyComponentDrawsOneClosedRoundedRing},
        {"the_driver_reaches_single_line_multi_line_rich_and_underline_text_fields", theDriverReachesSingleLineMultiLineRichAndUnderlineTextFields},
        {"the_ring_never_covers_content_and_never_erases_it", theRingNeverCoversContentAndNeverErasesIt},
        {"pressing_tab_through_the_page_moves_nothing", pressingTabThroughThePageMovesNothing},
        {"a_new_thickness_changes_the_next_frames_ring_and_padding", aNewThicknessChangesTheNextFramesRingAndPadding},
        {"a_set_radius_replaces_each_components_own_and_a_large_one_makes_a_capsule", aSetRadiusReplacesEachComponentsOwnAndALargeOneMakesACapsule},
        {"the_ring_follows_the_focus_color_and_turns_error_colored_when_invalid", theRingFollowsTheFocusColorAndTurnsErrorColoredWhenInvalid},
        {"an_unfocused_invalid_field_keeps_its_thin_error_frame", anUnfocusedInvalidFieldKeepsItsThinErrorFrame},
        {"replacing_focus_or_accent_recolors_the_ring_as_the_ticket_says", replacingFocusOrAccentRecolorsTheRingAsTheTicketSays},
        {"never_draws_no_focus_indicator_on_any_component", neverDrawsNoFocusIndicatorOnAnyComponent},
        {"never_wins_over_keyboard_cues_and_keys_and_editable_text", neverWinsOverKeyboardCuesAndKeysAndEditableText},
        {"never_leaves_the_semantic_layer_and_the_focus_tooltip_alone", neverLeavesTheSemanticLayerAndTheFocusTooltipAlone},
    };
    return test::run("focus_ring", cases);
}
