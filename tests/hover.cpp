// Pointer hover (#74): palette values, the presentation switch, the hover target state machine,
// the paint seam (PaintRole::hover) and the typed / semantic simulation entries.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "render.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <algorithm>
#include <cmath>
#include <set>
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&web_appearance(), &material_appearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string name_of(const Appearance& a) { return std::string(a.name()); }
std::string label_of(const Appearance& a, const ThemeVariant& v) { return name_of(a) + " " + v.name; }

// ---- fixture: one of every pointer-operable component, plus a dialog ------------------------

struct Page {
    Ui ui; ScrollState scroll; test::FakeMeasure measure;
    ButtonHandle save, off, ok; CheckboxHandle box; RadioGroupHandle radio; DropdownHandle drop; ListHandle list;
    TabsHandle tabs; NavigationListHandle nav; SliderHandle slider; TextHandle field, notes; ProgressHandle progress;
    LabelHandle label, vertical; DialogHandle dialog; TooltipHandle tip;
    TextHandle aligned, aligned_notes;  // #80: aligned, spaced text; #79: multi-line
    int height = 2400;
    bool list_first = false;  // lets a scroll test reach the list rows inside a short window
    Page() {
        auto& s = detail::access(ui);
        save = ui.add_button({.id = "save", .kind = Kind::button, .label = u"Save"}).value.value();
        off = ui.add_button({.id = "off", .kind = Kind::button, .label = u"Off", .enabled = false}).value.value();
        ok = ui.add_button({.id = "ok", .kind = Kind::button, .label = u"OK"}).value.value();
        box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox, .label = u"Box"}).value.value();
        radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group, .label = u"Radio"},
            {{{"a"}, u"A"}, {{"b"}, u"B", false}, {{"c"}, u"C"}}).value.value();
        drop = ui.add_dropdown({.id = "drop", .kind = Kind::dropdown, .label = u"Drop"},
            {{{"d1"}, u"One"}, {{"d2"}, u"Two"}, {{"d3"}, u"Three", false}}).value.value();
        drop.connect_source({[] { return std::optional<OptionValue>{}; }, [](auto) { return [] {}; }});
        drop.connect_immediate([](const DropdownChange&) { return Result{}; });
        std::vector<RadioOption> items;
        for (int i = 1; i <= 40; ++i) items.push_back({{"l" + std::to_string(i)}, u"Item " + std::u16string(1, char16_t(u'0' + i % 10)), i != 3});
        list = ui.add_list({.id = "list", .kind = Kind::list, .label = u"List"}, items).value.value();
        tabs = ui.add_tabs({.id = "tabs", .kind = Kind::tabs, .label = u"Tabs"},
            {{{"t1"}, u"One", true, {}}, {{"t2"}, u"Two", true, {}}, {{"t3"}, u"Three", false, {}}}).value.value();
        nav = ui.add_navigation_list({.id = "nav", .kind = Kind::navigation_list, .label = u"Nav"},
            {{{"n1"}, u"General", true, {}}, {{"n2"}, u"Other", true, {}}, {{"n3"}, u"Locked", false, {}}}, {"n1"}).value.value();
        slider = ui.add_slider({.id = "slider", .kind = Kind::slider, .label = u"Volume"}, {0, 100, 5}).value.value();
        slider.connect_source({[] { return 50; }, [](auto) { return [] {}; }});
        slider.connect_immediate([](const NumericChange&) { return Result{}; });
        field = ui.add_text({.id = "field", .kind = Kind::text, .label = u"Name", .value = u"abc"}).value.value();
        notes = ui.add_text({.id = "notes", .kind = Kind::text, .label = u"Notes", .value = u"one\ntwo", .multiline = true, .visible_lines = 3}).value.value();
        aligned = ui.add_text({.id = "aligned", .kind = Kind::text, .label = u"Right", .value = u"abc"}).value.value();
        check(bool(aligned.set_text_align(TextAlign::right)), "a single-line field takes an alignment");
        aligned_notes = ui.add_text({.id = "aligned_notes", .kind = Kind::text, .label = u"Centered", .value = u"one\ntwo\nthree", .multiline = true, .visible_lines = 3}).value.value();
        check(bool(aligned_notes.set_text_align(TextAlign::center)) && bool(aligned_notes.set_line_spacing(1.5)) && bool(aligned_notes.set_paragraph_spacing(8)),
              "a multi-line field takes alignment, line spacing and paragraph spacing");
        progress = ui.add_progress({.id = "progress", .kind = Kind::progress, .label = u"Progress"}).value.value();
        label = ui.add_label({.id = "label", .kind = Kind::label, .value = u"Hello"}).value.value();
        vertical = ui.add_label({.id = "vertical", .kind = Kind::label, .value = u"Vertical"}).value.value();
        check(bool(vertical.set_writing_mode(WritingMode::vertical)), "a label can be written vertically (#82)");
        dialog = ui.add_dialog("dlg", {ok}, ok, save, [](bool) {}).value.value();
        tip = ui.add_tooltip("save.tip", save, u"Save it", 100).value.value();
        s.window_active(true);
        s.tooltip_time(0);
    }
    layout::Node root() const {
        auto page = layout::scroll(layout::column(list_first ? std::vector<layout::Node>{layout::control(list), layout::control(save), layout::control(box)} : std::vector<layout::Node>{
            layout::control(save), layout::control(off), layout::control(box), layout::control(radio), layout::control(drop),
            layout::control(tabs), layout::control(nav), layout::control(slider), layout::control(field), layout::control(notes), layout::control(aligned), layout::control(aligned_notes), layout::control(progress),
            layout::control(label), layout::control(vertical), layout::control(list)}));
        return layout::modal(std::move(page), layout::control(ok), dialog);
    }
    Frame arrange(const Appearance& look = web_appearance(), int width = 480) {
        return lcars::arrange(root(), ui, look, measure, width, height, 1.f, scroll);
    }
    detail::UiState& state() { return detail::access(ui); }
};
const Placement& placed(const Frame& frame, std::string_view id) {
    const auto* item = detail::find_placement(frame, id);
    check(item != nullptr, "placement exists");
    return *item;
}
std::pair<int, int> middle(Rect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
std::pair<int, int> middle_of(const Frame& frame, std::string_view id) {
    const auto& p = placed(frame, id);
    return middle(intersect(p.bounds, p.clip));
}
std::pair<int, int> item_middle(const Frame& frame, std::string_view id, std::size_t index) {
    const auto& p = placed(frame, id);
    check(index < p.options.size(), "item placed");
    return middle(intersect(p.options[index], p.clip));
}
void move(Page& page, const Frame& frame, std::pair<int, int> at, bool touch = false) {
    page.state().pointer_over(frame, at.first, at.second, touch);
}
std::optional<HoverTarget> target_of(Page& page) {
    auto q = page.ui.hover();
    check(bool(q), "hover query answers");
    return *q;
}
HoverTarget on(std::string id) { return {std::move(id), std::nullopt}; }
HoverTarget on(std::string id, std::string item) { return {std::move(id), OptionValue{std::move(item)}}; }
std::pair<int, int> empty_space() { return {470, 2390}; }

// ---- hover target: real pointer ------------------------------------------------------------

void the_pointer_hovers_components_and_single_items() {
    Page page; auto frame = page.arrange();
    check(!target_of(page), "nothing is hovered at first");
    move(page, frame, middle_of(frame, "save"));
    check(target_of(page) == on("save"), "the button is hovered");
    move(page, frame, middle_of(frame, "box"));
    check(target_of(page) == on("box"), "moving to another component moves the target");
    move(page, frame, middle_of(frame, "slider"));
    check(target_of(page) == on("slider"), "the slider is one target");
    move(page, frame, middle_of(frame, "field"));
    check(target_of(page) == on("field"), "the text field is hovered");
    move(page, frame, item_middle(frame, "list", 1));
    check(target_of(page) == on("list", "l2"), "a list row is its own target");
    move(page, frame, item_middle(frame, "radio", 2));
    check(target_of(page) == on("radio", "c"), "a radio option is its own target");
    move(page, frame, item_middle(frame, "tabs", 1));
    check(target_of(page) == on("tabs", "t2"), "a tab is its own target");
    move(page, frame, item_middle(frame, "nav", 1));
    check(target_of(page) == on("nav", "n2"), "a navigation item is its own target");
    move(page, frame, empty_space());
    check(!target_of(page), "empty space hovers nothing");
}

void disabled_components_and_items_are_never_the_target() {
    Page page; auto frame = page.arrange();
    move(page, frame, middle_of(frame, "off"));
    check(!target_of(page), "a disabled button is not hovered");
    move(page, frame, item_middle(frame, "list", 2));
    check(target_of(page) == on("list"), "a disabled row hovers the list, not the row");
    move(page, frame, item_middle(frame, "radio", 1));
    check(target_of(page) == on("radio"), "a disabled radio option is not hovered");
    move(page, frame, item_middle(frame, "tabs", 2));
    check(target_of(page) == on("tabs"), "a disabled tab is not hovered");
    move(page, frame, item_middle(frame, "nav", 2));
    check(target_of(page) == on("nav"), "a disabled navigation item is not hovered");
    move(page, frame, middle_of(frame, "save"));
    page.save.enable(false);
    check(!target_of(page), "disabling the hovered component ends its hover at once");
}

// Code review 2026-11-12 item 3: Frame::hit also returns labels and progress bars, which ignore the pointer.
void components_that_ignore_the_pointer_are_never_the_hover_target() {
    Page page; auto frame = page.arrange();
    for (const char* id : {"label", "vertical", "progress"}) {
        move(page, frame, middle_of(frame, id));
        check(!target_of(page), (std::string("hovering ") + id + " targets nothing").c_str());
    }
    move(page, frame, middle_of(frame, "save"));
    check(target_of(page) == on("save"), "a button is hovered");
    move(page, frame, middle_of(frame, "label"));
    check(!target_of(page), "moving from a button onto a label ends the hover");
}

void touch_messages_neither_make_nor_change_the_hover() {
    Page page; auto frame = page.arrange();
    move(page, frame, middle_of(frame, "box"), true);
    check(!target_of(page), "a touch message hovers nothing");
    move(page, frame, middle_of(frame, "save"));
    move(page, frame, middle_of(frame, "box"), true);
    check(target_of(page) == on("save"), "a touch message does not move a real hover");
    move(page, frame, empty_space(), true);
    check(target_of(page) == on("save"), "a touch message does not clear a real hover");
}

void leaving_the_window_or_losing_activation_clears_the_hover() {
    Page page; auto frame = page.arrange();
    move(page, frame, middle_of(frame, "save"));
    page.state().pointer_left();
    check(!target_of(page), "WM_MOUSELEAVE clears the hover");
    page.arrange();  // a re-layout must not bring it back: the pointer is gone
    check(!target_of(page), "a frame after the pointer left does not hover");
    move(page, frame, middle_of(frame, "save"));
    page.state().window_active(false);
    check(!target_of(page), "losing activation clears the hover");
    move(page, frame, middle_of(frame, "box"));
    check(!target_of(page), "an inactive window does not hover");
    page.state().window_active(true);
    move(page, frame, middle_of(frame, "box"));
    check(target_of(page) == on("box"), "hover resumes after activation");
}

void opening_a_dialog_clears_the_hover_and_blocks_the_background() {
    Page page; auto frame = page.arrange();
    move(page, frame, middle_of(frame, "save"));
    check(bool(page.dialog.open()), "dialog opens");
    check(!target_of(page), "opening the modal clears the hover");
    frame = page.arrange();
    check(!target_of(page), "the arranged modal frame does not bring it back");
    move(page, frame, middle_of(frame, "ok"));
    check(target_of(page) == on("ok"), "components inside the dialog hover as usual");
    move(page, frame, empty_space());
    check(!target_of(page), "the blocked background hovers nothing");
    check(page.ui.simulate_hover(page.save).status == Status::blocked_by_modal, "simulation on the background is blocked");
    check(bool(page.ui.simulate_hover(page.ok)) && target_of(page) == on("ok"), "simulation inside the dialog works");
}

void dragging_a_slider_keeps_the_hover_on_it() {
    Page page; auto frame = page.arrange();
    auto& s = page.state();
    move(page, frame, middle_of(frame, "slider"));
    check(bool(s.pointer_down("slider")) && bool(s.slider_pointer_down("slider", middle_of(frame, "slider").first, placed(frame, "slider").slider_track)), "slider press");
    move(page, frame, middle_of(frame, "save"));
    check(target_of(page) == on("slider"), "the hover stays on the dragged slider over another button");
    move(page, frame, empty_space());
    check(target_of(page) == on("slider"), "and over empty space");
    s.slider_pointer("slider", 300);
    move(page, frame, middle_of(frame, "save"));
    check(bool(s.pointer_up("slider")), "slider release");
    frame = page.arrange();
    check(target_of(page) == on("save"), "after the release the hover follows the pointer position again");
}

void captured_hover_holds_until_released() {
    Page page; auto frame = page.arrange();
    auto& s = page.state();
    move(page, frame, middle_of(frame, "field"));
    s.hover_capture("field");
    move(page, frame, middle_of(frame, "save"));
    check(target_of(page) == on("field"), "a text selection drag keeps the field hovered");
    s.hover_release();
    frame = page.arrange();
    check(target_of(page) == on("save"), "release re-evaluates at the last pointer position");
    s.hover_capture({});
    move(page, frame, middle_of(frame, "box"));
    check(target_of(page) == on("save"), "capturing without an id freezes the current target (scrollbar thumb drag)");
    s.hover_release();
}

// ---- simulation ----------------------------------------------------------------------------

void simulated_hover_has_the_effect_of_the_pointer() {
    Page page; auto frame = page.arrange();
    check(bool(page.ui.simulate_hover(page.save)), "simulation accepted");
    check(target_of(page) == on("save"), "the simulated target is queryable");
    check(!*page.tip.visible(), "the tooltip waits for its delay");
    page.state().tooltip_time(100);
    check(*page.tip.visible(), "the tooltip appears after the C11 delay");
    move(page, frame, middle_of(frame, "box"));
    check(target_of(page) == on("box") && !*page.tip.visible(), "the real pointer takes over at its first move");
    check(bool(page.ui.simulate_hover(page.list, OptionValue{"l4"})) && target_of(page) == on("list", "l4"), "an item can be simulated");
    check(bool(page.ui.clear_simulated_hover()) && !target_of(page), "the simulation can be cleared");
    check(bool(page.ui.clear_simulated_hover()), "clearing again is harmless");
    check(bool(page.ui.simulate_hover(page.save)), "simulate before clearing a real hover");
    move(page, frame, middle_of(frame, "box"));
    check(bool(page.ui.clear_simulated_hover()) && target_of(page) == on("box"), "clearing leaves a real hover alone");
}

void simulation_is_admitted_like_the_typed_operations() {
    Page page; page.arrange();
    check(page.ui.simulate_hover(page.off).status == Status::disabled, "a disabled component answers disabled");
    check(page.ui.simulate_hover(page.list, OptionValue{"l3"}).status == Status::disabled, "a disabled item answers disabled");
    check(page.ui.simulate_hover(page.list, OptionValue{"nope"}).status == Status::not_found, "a missing item answers not_found");
    check(page.ui.simulate_hover(page.save, OptionValue{"x"}).status == Status::not_found, "an item on an item-less component answers not_found");
    check(page.ui.simulate_hover(ComponentHandle{}).status == Status::not_found, "an empty handle answers not_found");
    check(!target_of(page), "refusals leave no hover");
    ButtonHandle stale;
    { Ui other; stale = other.add_button({.id = "x", .kind = Kind::button}).value.value(); }
    check(page.ui.simulate_hover(stale).status == Status::not_found, "a handle of a dead Ui answers not_found");
    check(page.ui.simulate_hover(page.tabs, OptionValue{"t2"}).status == Status::ok, "tab items can be simulated");
}

void a_simulated_hover_waits_while_a_drag_holds_the_hover() {
    Page page; auto frame = page.arrange();
    auto& s = page.state();
    move(page, frame, middle_of(frame, "slider"));
    s.pointer_down("slider"); s.slider_pointer_down("slider", 100, placed(frame, "slider").slider_track);
    check(page.ui.simulate_hover(page.save).status == Status::busy && target_of(page) == on("slider"), "no simulation during a drag");
    s.pointer_up("slider");
}

// ---- paint seam ----------------------------------------------------------------------------

using detail::PaintRole;
bool has_role(const detail::DrawCommand& c, PaintRole role) { return (static_cast<unsigned>(c.role) & static_cast<unsigned>(role)) != 0; }
struct HoverProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    detail::RecordingCanvas recording{4000, 4000, {0, 0, 0}};
    void role(PaintRole value) override { recording.current_role = value; }
    void fill(Rect r, Color c) override { recording.fill(r, c); }
    void rounded(Rect r, int radius, Color c) override { recording.rounded(r, radius, c); }
    void overlay(Rect r, int radius, Color c, unsigned char alpha) override { recording.overlay(r, radius, c, alpha); }
    void clip(Rect r) override { recording.clip = intersect(recording.clip, r); }
    void text(std::u16string_view, int, int, int, Color) override {}
    std::vector<detail::DrawCommand> commands_with(PaintRole role) const {
        std::vector<detail::DrawCommand> found;
        for (const auto& c : recording.list.commands) if (has_role(c, role)) found.push_back(c);
        return found;
    }
};
HoverProbe paint(Page& page, const Placement& item, const Appearance& look) {
    auto semantics = page.state().describe(item.id);
    check(semantics.has_value(), "placement describes");
    HoverProbe probe(page.state(), item, *semantics, look, 1.f, true);
    detail::component_type(item.kind).paint(probe);
    return probe;
}
std::vector<const Placement*> painted_placements(const Frame& frame) {
    std::vector<const Placement*> all;
    for (const auto& item : frame.items) all.push_back(&item);
    for (const auto& item : frame.overlays) if (!item.tooltip) all.push_back(&item);
    return all;
}
bool inside(Rect inner, Rect outer) { return intersect(inner, outer) == inner; }
Rect rect_of(const detail::DrawCommand& c) { return c.rect; }

// The points a pointer can rest on for one placement: the component, and each of its items.
struct Rest { std::pair<int, int> at; std::string context; };
std::vector<Rest> rests(const Placement& p) {
    std::vector<Rest> points;
    auto visible = intersect(p.bounds, p.clip);
    if (visible.w > 0 && visible.h > 0) points.push_back({middle(visible), p.id + " body"});
    for (std::size_t i = 0; i < p.options.size(); ++i) {
        auto row = intersect(p.options[i], p.clip);
        if (row.w > 0 && row.h > 0) points.push_back({middle(row), p.id + " item " + std::to_string(i)});
    }
    return points;
}

// One driver for every component and item, appearance, variant and density: while the pointer rests
// on a point, exactly the placements the hover target names carry hover commands, inside the
// hovered component or item, in the palette's overlay color.
void hover_commands_follow_the_target_everywhere() {
    std::set<std::string> seen;  // kinds (and the dropdown panel) that showed an overlay in every configuration
    std::set<std::string> seen_ids;
    for (const auto* look : builtins) for (const auto& variant : variants) for (auto density : {Density::standard, Density::compact}) for (bool popup : {false, true}) {
        Page page;
        auto settings = page.ui.presentation(); settings.variant = variant; settings.density = density;
        check(bool(page.ui.set_presentation(settings)), "settings accepted");
        if (popup) check(bool(page.drop.open()), "the dropdown opens so its panel is part of the frame");  // the panel covers what lies below it
        auto frame = page.arrange(*look);
        auto palette = page.state().effective_palette(*look);
        for (const auto* source : painted_placements(frame)) for (const auto& rest : rests(*source)) {
            auto context = label_of(*look, variant) + " " + (density == Density::compact ? "compact " : "standard ") + (popup ? "popup " : "") + rest.context;
            move(page, frame, rest.at);
            auto target = target_of(page);
            for (const auto* q : painted_placements(frame)) {
                auto probe = paint(page, *q, *look);
                auto hovers = probe.commands_with(PaintRole::hover);
                auto kind_clickable = detail::component_type(q->kind).accepts_pointer();
                bool item_based = !q->options.empty();
                bool expected = target && target->id == q->id && kind_clickable &&
                    (item_based ? target->item.has_value() : !target->item.has_value());
                check(expected == !hovers.empty(), (context + ": hover commands for " + q->id + (q->dropdown_panel ? " panel" : "")).c_str());
                if (!expected) continue;
                seen.insert(std::to_string(static_cast<int>(q->kind)) + (q->dropdown_panel ? "p" : ""));
                seen_ids.insert(q->id);
                for (const auto& command : hovers) {
                    check(command.color.r == palette.hover.r && command.color.g == palette.hover.g && command.color.b == palette.hover.b &&
                          command.color.a == palette.hover_alpha, (context + ": overlay uses the palette's hover color").c_str());
                    Rect region = intersect(q->bounds, q->clip);
                    if (target->item) {
                        auto semantics = page.state().describe(q->id);
                        auto index = std::find_if(semantics->options.begin(), semantics->options.end(), [&](const auto& o) { return o.value == *target->item; }) - semantics->options.begin();
                        region = q->options.at(static_cast<std::size_t>(index) - q->option_start);
                    }
                    check(inside(rect_of(command), region) || q->kind == Kind::slider, (context + ": hover stays inside the hovered component or item").c_str());
                }
            }
        }
    }
    // Not vacuous: every clickable kind, and the open dropdown's panel, showed an overlay.
    std::set<std::string> expected_kinds;
    for (auto kind : {Kind::text, Kind::button, Kind::checkbox, Kind::radio_group, Kind::slider, Kind::dropdown, Kind::list, Kind::tabs, Kind::navigation_list})
        expected_kinds.insert(std::to_string(static_cast<int>(kind)));
    expected_kinds.insert(std::to_string(static_cast<int>(Kind::dropdown)) + "p");
    std::string missing;
    for (const auto& k : expected_kinds) if (!seen.contains(k)) missing += " " + k;
    check(missing.empty(), ("the driver saw an overlay for every clickable kind and the dropdown panel; missing:" + missing).c_str());
    // The components merged after #74 branched: aligned and multi-line fields hover like any field; a (vertical) label is not clickable.
    for (const char* id : {"field", "notes", "aligned", "aligned_notes"}) check(seen_ids.contains(id), (std::string("the driver saw a hover overlay on ") + id).c_str());
    for (const char* id : {"label", "vertical"}) check(!seen_ids.contains(id), (std::string("labels are not clickable and never hover: ") + id).c_str());
}

// ---- context menu (#85) --------------------------------------------------------------------

std::string highlighted_item(Page& page) {
    auto menu = page.ui.context_menu();
    if (!menu) return "<closed>";
    for (const auto& item : menu->items) if (item.highlighted) return item.id;
    return "<none>";
}
// A menu row's hover is the menu's own row highlight, not a hover overlay: the host runs update_hover and then
// menu_highlight on every move. One driver over every row of the open menu, appearance, variant and density.
void context_menu_rows_hover_as_their_highlight() {
    int rows_seen = 0;
    for (const auto* look : builtins) for (const auto& variant : variants) for (auto density : {Density::standard, Density::compact}) {
        Page page;
        auto settings = page.ui.presentation(); settings.variant = variant; settings.density = density;
        check(bool(page.ui.set_presentation(settings)), "settings accepted");
        auto frame = page.arrange(*look);
        move(page, frame, middle_of(frame, "save"));
        check(target_of(page) == on("save"), "the button is hovered before the menu opens");
        check(bool(page.ui.open_context_menu(page.field)), "the field's menu opens");
        check(!target_of(page), "an open menu takes the pointer: no component is hovered");
        frame = page.arrange(*look);
        const auto* panel = frame.menu_panel();
        check(panel != nullptr && !panel->menu_rows.empty(), "the menu is placed");
        if (!panel) continue;
        auto context = label_of(*look, variant) + (density == Density::compact ? " compact" : " standard");
        for (const auto& row : panel->menu_rows) {
            auto at = middle(row.bounds);
            check(frame.menu_item_at(at.first, at.second) == row.id, (context + ": the row is under its own middle").c_str());
            auto before = highlighted_item(page);
            move(page, frame, at);  // the host's WM_MOUSEMOVE: hover first, then the menu row
            if (auto id = frame.menu_item_at(at.first, at.second)) page.state().menu_highlight(*id);
            if (row.separator) { check(highlighted_item(page) == before, (context + ": a line keeps the highlight").c_str()); continue; }
            ++rows_seen;
            check(highlighted_item(page) == row.id, (context + ": hovering " + row.id + " highlights it").c_str());
            check(!target_of(page), (context + ": no component hover while the menu is open").c_str());
            for (const auto* q : painted_placements(frame)) {
                if (q->menu) continue;  // the menu panel is painted by the host, with its own row highlight
                check(paint(page, *q, *look).commands_with(PaintRole::hover).empty(), (context + ": no hover overlay under an open menu for " + q->id).c_str());
            }
        }
        // Touch moves do not highlight; the typed simulation is refused while the menu owns the pointer.
        check(page.ui.simulate_hover(page.save).status == Status::blocked_by_modal, "typed simulation answers blocked_by_modal");
        auto& tree = page.ui.semantics();
        const auto menu = menu_node_id("field");
        check(tree.perform({"field"}, Verb::hover).status == Status::blocked_by_modal, "the semantic verb agrees with the typed entry");
        check(tree.perform({menu}, Verb::hover).status == Status::not_supported, "the menu panel itself is not hoverable");
        check(bool(tree.perform({menu, OptionValue{"lcars.select_all"}}, Verb::hover)) && highlighted_item(page) == "lcars.select_all", "the semantic hover on a row highlights it");
        // Closing the menu gives the pointer back at once, to what lies under its resting position.
        check(bool(page.ui.close_context_menu()), "the menu closes");
        frame = page.arrange(*look);
        move(page, frame, middle_of(frame, "box"));
        check(target_of(page) == on("box"), "after the menu the real pointer hovers again");
    }
    check(rows_seen > 0, "the driver visited menu rows");
}

void hovering_the_slider_track_far_from_the_thumb_still_marks_the_thumb() {
    for (const auto* look : builtins) {
        Page page; auto frame = page.arrange(*look);
        const auto& slider = placed(frame, "slider");
        auto track = slider.slider_track;
        move(page, frame, {track.x + track.w - 3, track.y + track.h / 2});
        check(target_of(page) == on("slider"), "the pointer on the far end of the track hovers the slider");
        auto hovers = paint(page, slider, *look).commands_with(PaintRole::hover);
        check(!hovers.empty(), "the thumb carries a hover command");
        int thumb_x = track.x + track.w / 2;  // the value is 50 of 0-100
        bool on_thumb = std::any_of(hovers.begin(), hovers.end(), [&](const auto& c) { return c.rect.x <= thumb_x && thumb_x < c.rect.x + c.rect.w; });
        check(on_thumb, "the hover command sits on the thumb, not under the pointer");
    }
}

void a_hovered_selected_item_keeps_its_selection_and_gains_the_overlay() {
    for (const auto* look : builtins) {
        Page page; auto frame = page.arrange(*look);
        move(page, frame, item_middle(frame, "nav", 0));  // n1 is the selected page
        check(target_of(page) == on("nav", "n1"), "the selected navigation item is hovered");
        auto probe = paint(page, placed(frame, "nav"), *look);
        check(!probe.commands_with(PaintRole::selection).empty() && !probe.commands_with(PaintRole::hover).empty(),
              "selection and hover are both painted for the selected item");
        // Hover goes over the selection color: its command comes after the selection commands.
        const auto& commands = probe.recording.list.commands;
        auto last_selection = std::find_if(commands.rbegin(), commands.rend(), [](const auto& c) { return has_role(c, PaintRole::selection); });
        auto last_hover = std::find_if(commands.rbegin(), commands.rend(), [](const auto& c) { return has_role(c, PaintRole::hover); });
        check(last_hover < last_selection, "hover is painted over the selection");
    }
}

void the_hover_switch_hides_the_overlay_but_not_the_target_or_the_tooltip() {
    Page page; auto frame = page.arrange();
    auto settings = page.ui.presentation(); settings.hover = Hover::off;
    check(bool(page.ui.set_presentation(settings)), "switch off");
    move(page, frame, middle_of(frame, "save"));
    check(target_of(page) == on("save"), "the target is still reported with the switch off");
    for (const auto* q : painted_placements(frame))
        check(paint(page, *q, web_appearance()).commands_with(PaintRole::hover).empty(), "no hover command with the switch off");
    page.state().tooltip_time(100);
    check(*page.tip.visible(), "the tooltip still follows the pointer");
    check(bool(page.ui.simulate_hover(page.box)), "simulation is accepted with the switch off");
    check(paint(page, placed(frame, "box"), web_appearance()).commands_with(PaintRole::hover).empty(), "simulated hover is hidden too");
    settings.hover = Hover::on;
    check(bool(page.ui.set_presentation(settings)), "switch on");
    check(!paint(page, placed(frame, "box"), web_appearance()).commands_with(PaintRole::hover).empty(), "switching back on shows the overlay at once");
}

void hover_overlay_blends_over_the_surface_with_the_palette_alpha() {
    detail::RecordingCanvas canvas(10, 10, {200, 200, 200});
    canvas.current_role = PaintRole::hover;
    canvas.overlay({2, 2, 4, 4}, 0, {0, 0, 0}, 15);
    auto image = detail::render_cpu(canvas.list, 10, 10);
    auto at = [&](int x, int y) { return image.pixels[(static_cast<std::size_t>(y) * 10 + x) * 4]; };
    check(at(0, 0) == 200, "outside the overlay the surface is untouched");
    check(at(3, 3) == 188, "6% black over 200 gray is 188 ((0*15 + 200*240) / 255)");
    check(canvas.list.commands.back().role == PaintRole::hover && canvas.list.commands.back().color.a == 15, "the command carries the hover role and the alpha");
}

// Code review 2026-11-12 item 4: a canvas that only knows fill/rounded (the default overlay path) still learns the
// real alpha of a translucent fill, instead of seeing the hover color painted as if it were opaque.
struct AlphaProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    struct Seen { PaintRole role; unsigned alpha; };
    std::vector<Seen> seen;
    PaintRole current = PaintRole::content;
    void role(PaintRole r) override { current = r; }
    void fill(Rect, Color) override { seen.push_back({current, overlay_alpha()}); }
    void rounded(Rect, int, Color) override { seen.push_back({current, overlay_alpha()}); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color) override {}
};
void the_default_overlay_path_keeps_the_alpha_visible() {
    Page page; auto frame = page.arrange();
    move(page, frame, middle_of(frame, "save"));
    const auto& item = placed(frame, "save");
    auto semantics = page.state().describe("save");
    check(semantics.has_value(), "the button describes");
    AlphaProbe probe(page.state(), item, *semantics, web_appearance(), 1.f, true);
    detail::component_type(item.kind).paint(probe);
    auto palette = page.state().effective_palette(web_appearance());
    int translucent = 0, opaque = 0;
    for (const auto& s : probe.seen) {
        if (s.role == PaintRole::hover) { ++translucent; check(s.alpha == palette.hover_alpha, "the hover fill carries the palette alpha"); }
        else { ++opaque; check(s.alpha == 255, "every other fill is opaque"); }
    }
    check(translucent == 1 && opaque > 0, "one translucent fill among the opaque ones");
}

void hover_never_paints_for_a_zero_alpha_palette() {
    Page page;
    check(bool(page.ui.set_palette_overrides({{name_of(web_appearance()), ThemeVariant::light(), "hover_alpha", 0}})), "alpha 0 accepted");
    auto frame = page.arrange();
    move(page, frame, middle_of(frame, "save"));
    check(paint(page, placed(frame, "save"), web_appearance()).commands_with(PaintRole::hover).empty(), "alpha 0 paints nothing");
}

// ---- re-evaluation after scrolling and reflow -----------------------------------------------

std::optional<std::string> id_under(const Frame& frame, std::pair<int, int> at) {
    const auto* hit = frame.hit(at.first, at.second);
    return hit ? std::optional<std::string>(hit->id) : std::nullopt;
}

void scrolling_under_a_resting_pointer_moves_the_hover_at_once() {
    Page page; page.height = 300;
    auto frame = page.arrange();
    auto at = middle_of(frame, "save");
    move(page, frame, at);
    check(target_of(page) == on("save"), "the pointer rests on the first button");
    check(frame.wheel(page.ui, at.first, at.second, -120), "the wheel scrolls the page");
    auto after = page.arrange();  // what the host does for the repaint the wheel requests
    auto expected = id_under(after, at);
    check(expected && *expected != "save", "after the scroll another component lies under the pointer");
    auto target = target_of(page);
    check(target && target->id == *expected, "the hover is on the new component without a mouse move");
    // Scrolling the list under a resting pointer changes the hovered item.
    Page tall; tall.height = 400; tall.list_first = true;
    auto list_frame = tall.arrange();
    auto row = item_middle(list_frame, "list", 5);
    move(tall, list_frame, row);
    auto before = target_of(tall);
    check(before && before->id == "list" && before->item, "a list row is hovered");
    check(list_frame.wheel(tall.ui, row.first, row.second, -96), "the page scrolls");
    auto scrolled = tall.arrange();
    auto now = target_of(tall);
    check(now && now->id == "list" && now->item && !(now->item == before->item), "another row is now under the pointer and hovered");
    check(scrolled.option_at(placed(scrolled, "list"), row.first, row.second).has_value(), "and that row is the one the frame puts there");
}

void reflow_under_a_resting_pointer_moves_the_hover_at_once() {
    Page page; page.height = 3000;
    auto frame = page.arrange();
    auto at = middle_of(frame, "field");
    move(page, frame, at);
    auto before = target_of(page);
    check(before && before->id == "field", "the pointer rests on the text field");
    auto settings = page.ui.presentation(); settings.text_size = 28;
    check(bool(page.ui.set_presentation(settings)), "a keyboard shortcut enlarges the text");
    auto after = page.arrange();
    auto expected = id_under(after, at);
    auto target = target_of(page);
    check(expected && *expected != "field", "the larger text moved another component under the pointer");
    check(target && target->id == *expected, "the hover follows the reflow without a mouse move");
    settings.density = Density::compact;
    check(bool(page.ui.set_presentation(settings)), "density changes");
    auto compact = page.arrange();
    auto now = id_under(compact, at);
    auto after_density = target_of(page);
    check(now.has_value() ? after_density.has_value() && after_density->id == *now : !after_density, "and so does a density change");
}

void a_frame_does_not_move_a_simulated_hover() {
    Page page; page.height = 300;
    auto frame = page.arrange();
    auto save_at = middle_of(frame, "save");
    move(page, frame, save_at);
    check(bool(page.ui.simulate_hover(page.box)), "simulated hover");
    frame.wheel(page.ui, 40, 20, -120);
    page.arrange();
    check(target_of(page) == on("box"), "a re-layout leaves a simulated hover alone");
    check(bool(page.ui.clear_simulated_hover()), "clear");
    auto fresh = page.arrange();
    auto expected = id_under(fresh, save_at);
    auto target = target_of(page);
    check(expected ? target && target->id == *expected : !target, "after clearing the real pointer position applies again");
}

// ---- semantic layer --------------------------------------------------------------------------

void the_semantic_layer_queries_and_simulates_hover_like_the_typed_entry() {
    Page page; page.arrange();
    auto& tree = page.ui.semantics();
    check(tree.hover() && !*tree.hover(), "nothing hovered");
    check(bool(tree.perform({"save"}, Verb::hover)), "semantic simulation accepted");
    check(tree.hover() && *tree.hover() == NodeRef{"save"} && target_of(page) == on("save"), "the semantic query equals the typed one");
    check(bool(tree.perform({"list", OptionValue{"l2"}}, Verb::hover)), "an item is named by id and value");
    check(tree.hover() && *tree.hover() == (NodeRef{"list", OptionValue{"l2"}}) && target_of(page) == on("list", "l2"), "the item is reported by both");
    check(bool(tree.clear_simulated_hover()) && !target_of(page) && !*tree.hover(), "the semantic layer clears the simulation");
    // Same admission and Status words as the typed entry, target by target.
    struct Case { std::string id; std::optional<std::string> item; ComponentHandle handle; };
    std::vector<Case> cases{
        {"save", std::nullopt, page.save}, {"off", std::nullopt, page.off}, {"list", "l3", page.list}, {"list", "l2", page.list},
        {"list", "nope", page.list}, {"save", "x", page.save}, {"tabs", "t3", page.tabs}, {"nav", "n2", page.nav}};
    for (const auto& c : cases) {
        std::optional<OptionValue> item = c.item ? std::optional<OptionValue>(OptionValue{*c.item}) : std::nullopt;
        auto typed = page.ui.simulate_hover(c.handle, item).status;
        auto semantic = tree.perform({c.id, item}, Verb::hover).status;
        check(typed == semantic, ("typed and semantic statuses agree for " + c.id + (c.item ? "/" + *c.item : "")).c_str());
        page.ui.clear_simulated_hover();
    }
    check(tree.perform({"missing"}, Verb::hover).status == Status::not_found, "an unknown id is not_found");
    check(bool(page.dialog.open()), "dialog opens");
    check(page.ui.simulate_hover(page.save).status == Status::blocked_by_modal &&
          tree.perform({"save"}, Verb::hover).status == Status::blocked_by_modal, "both entries report blocked_by_modal behind a dialog");
    check(tree.perform({"ok"}, Verb::hover).status == Status::ok, "and accept a component inside it");
    check(tree.perform({"dlg"}, Verb::hover).status != Status::ok, "the dialog node itself is not hoverable");
}

void hover_is_invisible_to_the_semantic_tree_and_its_events() {
    Page page; auto frame = page.arrange();
    auto& tree = page.ui.semantics();
    auto snapshot = [&] {
        std::string out;
        for (const char* id : {"save", "box", "radio", "list", "slider", "field", "nav"}) {
            auto node = tree.get({id});
            check(bool(node), "node exists");
            out += std::string(id) + "|" + std::to_string(node->name.size()) + "|" + std::to_string(node->enabled) + std::to_string(node->focused) +
                std::to_string(node->selected) + std::to_string(node->expanded) + std::to_string(node->visible) + "|" + std::to_string(node->verbs.size()) +
                "|" + std::to_string(node->description.size()) + ";";
            auto children = tree.children({id});
            if (!children) continue;
            for (const auto& child : *children) {
                auto item = tree.get(child);
                out += std::to_string(item->selected) + std::to_string(item->enabled) + std::to_string(item->verbs.size());
            }
        }
        return out;
    };
    auto before = snapshot();
    int events = 0;
    auto subscription = tree.subscribe([&](const SemanticEvent&) { ++events; });
    tree.dispatch_events();
    events = 0;
    move(page, frame, middle_of(frame, "save")); move(page, frame, item_middle(frame, "list", 1)); move(page, frame, middle_of(frame, "slider"));
    tree.perform({"box"}, Verb::hover); tree.perform({"nav", OptionValue{"n2"}}, Verb::hover);
    tree.dispatch_events();
    check(events == 0, "hover changes raise no semantic event");
    check(snapshot() == before, "no node property or verb list changes with hover");
}

// ---- palette -------------------------------------------------------------------------------

// Literal proposal values of the ticket: Material overlays the text color at 8% (20/255);
// Minimal Web overlays 6% black on light (15/255) and 8% white on dark (20/255).
void hover_overlay_values_are_the_proposed_ones() {
    auto material_light = material_appearance().palette(ThemeVariant::light());
    auto material_dark = material_appearance().palette(ThemeVariant::dark());
    auto web_light = web_appearance().palette(ThemeVariant::light());
    auto web_dark = web_appearance().palette(ThemeVariant::dark());
    // 8% would leave the muted field label at 4.22:1 on the hovered surface; 10/255 (4%) keeps 4.56:1.
    check(material_light.hover == material_light.text && material_light.hover_alpha == 10, "material light: text color at 4% (the contrast rule caps it below 8%)");
    check(material_dark.hover == material_dark.text && material_dark.hover_alpha == 20, "material dark: text color at 8%");
    check(web_light.hover == Color{0, 0, 0} && web_light.hover_alpha == 15, "web light: 6% black");
    check(web_dark.hover == Color{255, 255, 255} && web_dark.hover_alpha == 20, "web dark: 8% white");
}

void hover_roles_are_named_palette_roles() {
    auto roles = palette_roles();
    check(std::find(roles.begin(), roles.end(), "hover") != roles.end(), "hover is a color role");
    check(std::find(roles.begin(), roles.end(), "hover_alpha") != roles.end(), "hover_alpha is an int role");
    Palette p = web_appearance().palette();
    check(set_palette_value(p, "hover", Color{9, 8, 7}) && p.hover == Color{9, 8, 7}, "hover color is settable");
    check(set_palette_value(p, "hover_alpha", 31) && p.hover_alpha == 31, "hover_alpha is settable");
    check(!set_palette_value(p, "hover_alpha", 256) && !set_palette_value(p, "hover_alpha", -1), "hover_alpha is 0-255");
    check(!set_palette_value(p, "hover_alpha", Color{1, 2, 3}) && !set_palette_value(p, "hover", 5), "hover roles check the value kind");
}

void hover_roles_can_be_overridden_through_the_ui() {
    Ui ui;
    auto result = ui.set_palette_overrides({{name_of(web_appearance()), ThemeVariant::light(), "hover", Color{10, 20, 30}},
                                            {name_of(web_appearance()), ThemeVariant::light(), "hover_alpha", 20}});
    check(bool(result), "hover overrides are accepted");
    auto effective = ui.effective_palette(web_appearance(), ThemeVariant::light());
    check(effective && effective->hover == Color{10, 20, 30} && effective->hover_alpha == 20, "override reaches the effective palette");
    check(ui.effective_palette(web_appearance(), ThemeVariant::dark())->hover == Color{255, 255, 255}, "other variants keep their value");
    // #86: an overlay strong enough to take the outline under 3:1 on the hovered surface is refused, naming the pair.
    auto strong = ui.set_palette_overrides({{name_of(web_appearance()), ThemeVariant::light(), "hover_alpha", 90}});
    check(strong.status == Status::invalid_value && std::u16string_view(strong.message).find(u"outline/hovered") != std::u16string_view::npos, "a hover overlay that drowns the outline is rejected");
}

// ---- contrast ------------------------------------------------------------------------------

Color blended(Color over, unsigned char alpha, Color under) {
    auto mix = [&](unsigned char o, unsigned char u) { return static_cast<unsigned char>((o * alpha + u * (255 - alpha)) / 255); };
    return {mix(over.r, under.r), mix(over.g, under.g), mix(over.b, under.b)};
}
// WCAG 1.4.11 understanding: a hover effect itself needs no 3:1, but it must not make the component lose
// sufficient contrast with its neighbors. Text keeps 4.5:1, the accent and the outline 3:1 on every hovered surface.
void hover_keeps_text_and_accent_readable_on_every_hovered_surface() {
    std::string failures;
    for (const auto* look : builtins) for (const auto& variant : variants) {
        auto p = look->palette(variant);
        struct Surface { const char* name; Color color; };
        for (const auto& surface : {Surface{"background", p.background}, Surface{"panel", p.panel}, Surface{"surface", p.surface}, Surface{"selection", p.selection}}) {
            auto hovered = blended(p.hover, p.hover_alpha, surface.color);
            auto context = label_of(*look, variant) + " " + surface.name;
            auto expect = [&](const char* what, Color foreground, double required) {
                double ratio = contrast_ratio(foreground, hovered);
                if (ratio < required) failures += context + ": " + what + " " + std::to_string(ratio).substr(0, 4) + ":1 (needs " + std::to_string(required).substr(0, 3) + "); ";
            };
            expect("text", p.text, 4.5);
            // #86: the edge that identifies a control keeps 3:1 on the hovered surface it is drawn on.
            if (std::string(surface.name) != "selection") expect("outline", p.outline, 3.0);
            if (std::string(surface.name) == "surface") { expect("muted", p.muted, 4.5); expect("accent", p.accent, 3.0); }
        }
    }
    check(failures.empty(), ("hover must not take away contrast: " + failures).c_str());
}
void the_blend_helper_matches_a_worked_example() {
    auto c = blended({0, 0, 0}, 15, {200, 100, 50});
    check(c == (Color{188, 94, 47}), "6% black over (200,100,50) is (188,94,47): (0*15 + 200*240)/255 = 188 etc.");
}

// ---- presentation switch -------------------------------------------------------------------

void presentation_has_a_hover_switch_on_by_default() {
    Ui ui;
    check(ui.presentation().hover == Hover::on, "hover is on by default");
    auto settings = ui.presentation();
    settings.hover = Hover::off;
    check(bool(ui.set_presentation(settings)) && ui.presentation().hover == Hover::off, "hover can be switched off at run time");
    check(!(settings == PresentationSettings{}), "the switch takes part in operator==");
    int repaints = 0; detail::access(ui).on_invalidate([&] { ++repaints; });
    settings.hover = Hover::on;
    check(bool(ui.set_presentation(settings)) && repaints == 1, "switching back on repaints");
}
}
int main() {
    static const test::Case cases[] = {
        {"hover_overlay_values_are_the_proposed_ones", hover_overlay_values_are_the_proposed_ones},
        {"hover_roles_are_named_palette_roles", hover_roles_are_named_palette_roles},
        {"hover_roles_can_be_overridden_through_the_ui", hover_roles_can_be_overridden_through_the_ui},
        {"presentation_has_a_hover_switch_on_by_default", presentation_has_a_hover_switch_on_by_default},
        {"the_blend_helper_matches_a_worked_example", the_blend_helper_matches_a_worked_example},
        {"hover_keeps_text_and_accent_readable_on_every_hovered_surface", hover_keeps_text_and_accent_readable_on_every_hovered_surface},
        {"the_pointer_hovers_components_and_single_items", the_pointer_hovers_components_and_single_items},
        {"disabled_components_and_items_are_never_the_target", disabled_components_and_items_are_never_the_target},
        {"components_that_ignore_the_pointer_are_never_the_hover_target", components_that_ignore_the_pointer_are_never_the_hover_target},
        {"touch_messages_neither_make_nor_change_the_hover", touch_messages_neither_make_nor_change_the_hover},
        {"leaving_the_window_or_losing_activation_clears_the_hover", leaving_the_window_or_losing_activation_clears_the_hover},
        {"opening_a_dialog_clears_the_hover_and_blocks_the_background", opening_a_dialog_clears_the_hover_and_blocks_the_background},
        {"dragging_a_slider_keeps_the_hover_on_it", dragging_a_slider_keeps_the_hover_on_it},
        {"captured_hover_holds_until_released", captured_hover_holds_until_released},
        {"simulated_hover_has_the_effect_of_the_pointer", simulated_hover_has_the_effect_of_the_pointer},
        {"simulation_is_admitted_like_the_typed_operations", simulation_is_admitted_like_the_typed_operations},
        {"a_simulated_hover_waits_while_a_drag_holds_the_hover", a_simulated_hover_waits_while_a_drag_holds_the_hover},
        {"hover_commands_follow_the_target_everywhere", hover_commands_follow_the_target_everywhere},
        {"context_menu_rows_hover_as_their_highlight", context_menu_rows_hover_as_their_highlight},
        {"hovering_the_slider_track_far_from_the_thumb_still_marks_the_thumb", hovering_the_slider_track_far_from_the_thumb_still_marks_the_thumb},
        {"a_hovered_selected_item_keeps_its_selection_and_gains_the_overlay", a_hovered_selected_item_keeps_its_selection_and_gains_the_overlay},
        {"the_hover_switch_hides_the_overlay_but_not_the_target_or_the_tooltip", the_hover_switch_hides_the_overlay_but_not_the_target_or_the_tooltip},
        {"hover_overlay_blends_over_the_surface_with_the_palette_alpha", hover_overlay_blends_over_the_surface_with_the_palette_alpha},
        {"the_default_overlay_path_keeps_the_alpha_visible", the_default_overlay_path_keeps_the_alpha_visible},
        {"hover_never_paints_for_a_zero_alpha_palette", hover_never_paints_for_a_zero_alpha_palette},
        {"scrolling_under_a_resting_pointer_moves_the_hover_at_once", scrolling_under_a_resting_pointer_moves_the_hover_at_once},
        {"reflow_under_a_resting_pointer_moves_the_hover_at_once", reflow_under_a_resting_pointer_moves_the_hover_at_once},
        {"a_frame_does_not_move_a_simulated_hover", a_frame_does_not_move_a_simulated_hover},
        {"the_semantic_layer_queries_and_simulates_hover_like_the_typed_entry", the_semantic_layer_queries_and_simulates_hover_like_the_typed_entry},
        {"hover_is_invisible_to_the_semantic_tree_and_its_events", hover_is_invisible_to_the_semantic_tree_and_its_events},
    };
    return test::run("hover", cases);
}
