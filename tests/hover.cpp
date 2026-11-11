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
using namespace lcars;
using test::check;
namespace {
const Appearance* const builtins[] = {&web_appearance(), &material_appearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string name_of(const Appearance& a) { return std::string(a.name()); }

// ---- fixture: one of every pointer-operable component, plus a dialog ------------------------

struct Page {
    Ui ui; ScrollState scroll; test::FakeMeasure measure;
    ButtonHandle save, off, ok; CheckboxHandle box; RadioGroupHandle radio; DropdownHandle drop; ListHandle list;
    TabsHandle tabs; NavigationListHandle nav; SliderHandle slider; TextHandle field; ProgressHandle progress;
    LabelHandle label; DialogHandle dialog; TooltipHandle tip;
    int height = 2400;
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
        progress = ui.add_progress({.id = "progress", .kind = Kind::progress, .label = u"Progress"}).value.value();
        label = ui.add_label({.id = "label", .kind = Kind::label, .value = u"Hello"}).value.value();
        dialog = ui.add_dialog("dlg", {ok}, ok, save, [](bool) {}).value.value();
        tip = ui.add_tooltip("save.tip", save, u"Save it", 100).value.value();
        s.window_active(true);
        s.tooltip_time(0);
    }
    layout::Node root() const {
        auto page = layout::scroll(layout::column({
            layout::control(save), layout::control(off), layout::control(box), layout::control(radio), layout::control(drop),
            layout::control(tabs), layout::control(nav), layout::control(slider), layout::control(field), layout::control(progress),
            layout::control(label), layout::control(list)}));
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

// ---- palette -------------------------------------------------------------------------------

// Literal proposal values of the ticket: Material overlays the text color at 8% (20/255);
// Minimal Web overlays 6% black on light (15/255) and 8% white on dark (20/255).
void hover_overlay_values_are_the_proposed_ones() {
    auto material_light = material_appearance().palette(ThemeVariant::light());
    auto material_dark = material_appearance().palette(ThemeVariant::dark());
    auto web_light = web_appearance().palette(ThemeVariant::light());
    auto web_dark = web_appearance().palette(ThemeVariant::dark());
    check(material_light.hover == material_light.text && material_light.hover_alpha == 20, "material light: text color at 8%");
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
                                            {name_of(web_appearance()), ThemeVariant::light(), "hover_alpha", 40}});
    check(bool(result), "hover overrides are accepted");
    auto effective = ui.effective_palette(web_appearance(), ThemeVariant::light());
    check(effective && effective->hover == Color{10, 20, 30} && effective->hover_alpha == 40, "override reaches the effective palette");
    check(ui.effective_palette(web_appearance(), ThemeVariant::dark())->hover == Color{255, 255, 255}, "other variants keep their value");
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
        {"the_pointer_hovers_components_and_single_items", the_pointer_hovers_components_and_single_items},
        {"disabled_components_and_items_are_never_the_target", disabled_components_and_items_are_never_the_target},
        {"touch_messages_neither_make_nor_change_the_hover", touch_messages_neither_make_nor_change_the_hover},
        {"leaving_the_window_or_losing_activation_clears_the_hover", leaving_the_window_or_losing_activation_clears_the_hover},
        {"opening_a_dialog_clears_the_hover_and_blocks_the_background", opening_a_dialog_clears_the_hover_and_blocks_the_background},
        {"dragging_a_slider_keeps_the_hover_on_it", dragging_a_slider_keeps_the_hover_on_it},
        {"captured_hover_holds_until_released", captured_hover_holds_until_released},
        {"simulated_hover_has_the_effect_of_the_pointer", simulated_hover_has_the_effect_of_the_pointer},
        {"simulation_is_admitted_like_the_typed_operations", simulation_is_admitted_like_the_typed_operations},
        {"a_simulated_hover_waits_while_a_drag_holds_the_hover", a_simulated_hover_waits_while_a_drag_holds_the_hover},
    };
    return test::run("hover", cases);
}
