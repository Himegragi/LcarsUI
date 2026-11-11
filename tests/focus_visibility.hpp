#pragma once
// Issue #72: the focus indicator is drawn only after keyboard input (or when forced), never after a
// pointer press. Included by edit_form.cpp after PaintRoleProbe so every case runs under both appearances.
//
// Seams under test (confirmed by the ticket's Agent Brief):
//   - Ui::set_presentation / PresentationSettings::focus_indicator  (always-show switch)
//   - Ui::focus_indicator_visible()                                  (read-only query)
//   - detail::UiState::key_down / pointer_down / pointer_pressed     (the host's real-input entries)
//   - detail::UiState::focus() and the handles' focus()              (programmatic focus must not change the state)
//   - detail::UiState::set_system_keyboard_cues(bool)                (SPI_GETKEYBOARDCUES test seam)
//   - detail::component_type(kind).paint(...) observed through PaintRoleProbe
namespace focus_visibility {
struct Fixture {
    Ui ui;
    ButtonHandle first, second;
    TextHandle edit, readonly;
    SliderHandle slider;
    ScrollState scroll;
    test::FakeMeasure measure;
    Fixture() {
        first = ui.add_button({.id = "first", .kind = Kind::button, .label = u"First"}).value.value();
        second = ui.add_button({.id = "second", .kind = Kind::button, .label = u"Second"}).value.value();
        edit = ui.add_text({.id = "edit", .kind = Kind::text, .value = u"abc"}).value.value();
        readonly = ui.add_text({.id = "readonly", .kind = Kind::text, .value = u"abc", .read_only = true}).value.value();
        slider = ui.add_slider({.id = "volume", .kind = Kind::slider, .label = u"Volume"}, SliderRange{0, 100, 1}).value.value();
        detail::access(ui).window_active(true);
    }
    detail::UiState& state() { return detail::access(ui); }
    Frame frame() {
        auto root = layout::column({layout::control(first), layout::control(second), layout::control(edit),
                                    layout::control(readonly), layout::control(slider)}, 0, 8);
        return arrange(root, ui, *appearance, measure, 640, 480, 1.f, scroll);
    }
    const Placement* place(const Frame& laid, std::string_view id) {
        for (const auto& placement : laid.items) if (placement.id == id) return &placement;
        return nullptr;
    }
    // Paint count of focus_indicator marks for one component, drawn from a fresh frame.
    int indicator(std::string_view id) {
        auto laid = frame();
        auto semantic = state().describe(id);
        const Placement* item = place(laid, id);
        check(item && semantic, "focus visibility fixture finds the placement");
        PaintRoleProbe probe(state(), *item, *semantic, *appearance, 1.f, true);
        detail::component_type(item->kind).paint(probe);
        return probe.count(detail::PaintRole::focus_indicator);
    }
};
void nothing_is_drawn_before_any_input() {
    Fixture f;
    check(bool(f.first.focus()) && f.first.describe()->focused, "focus moves programmatically");
    check(f.indicator("first") == 0, "Startup: a focused button draws no focus indicator");
}
void tab_after_pointer_draws_it() {
    Fixture f;
    f.state().pointer_down("first"); f.state().pointer_up("first");
    check(f.first.describe()->focused && f.indicator("first") == 0, "Pointer press: focused button has no indicator");
    f.state().key_down({Key::tab});
    check(f.second.describe()->focused && f.indicator("second") == 1, "Tab: the new focus has an indicator");
}
void every_real_key_opens_but_modifier_chords_do_not() {
    for (auto key : {Key::tab, Key::up, Key::down, Key::left, Key::right, Key::enter, Key::space, Key::escape,
                     Key::home, Key::end, Key::backspace, Key::del}) {
        Fixture f; f.first.focus();
        check(!f.ui.focus_indicator_visible(), "A fresh UI hides the indicator");
        f.state().key_down({key});
        check(f.ui.focus_indicator_visible(), "A plain key shows the indicator");
    }
    Fixture f; f.first.focus();
    KeyPress save; save.key = Key::other; save.ctrl = true; save.letter = u'S';
    KeyPress undo = save; undo.letter = u'Z';
    f.state().key_down(save); f.state().key_down(undo);
    check(!f.ui.focus_indicator_visible() && f.indicator("first") == 0, "Ctrl+S and Ctrl+Z do not show the indicator");
    f.state().key_down({Key::enter});
    check(f.ui.focus_indicator_visible(), "Enter shows it");
    f.state().key_down(save);
    check(f.ui.focus_indicator_visible(), "A Ctrl chord does not hide it either");
}
void pointer_hides_everywhere_but_wheel_does_not() {
    Fixture f; f.first.focus();
    f.state().key_down({Key::right});
    auto laid = f.frame();
    check(f.ui.focus_indicator_visible(), "Keyboard use shows the indicator");
    laid.wheel(f.ui, 1, 1, 48);
    check(f.ui.focus_indicator_visible(), "The wheel does not change the state");
    f.state().pointer_pressed();  // empty space or a component that ignores the pointer
    check(!f.ui.focus_indicator_visible() && f.indicator("first") == 0, "A press on nothing hides it");
    f.state().key_down({Key::tab});
    check(f.ui.focus_indicator_visible(), "A key shows it again");
    check(f.state().pointer_down("missing").status != Status::ok && !f.ui.focus_indicator_visible(), "A refused pointer press still hides it");
}
void programmatic_focus_keeps_the_current_state() {
    Fixture f;
    f.state().pointer_down("first"); f.state().pointer_up("first");
    check(bool(f.second.focus()) && f.second.describe()->focused && f.indicator("second") == 0, "focus() after the pointer: still hidden");
    check(bool(f.ui.semantics().perform({"first"}, Verb::focus)) && f.first.describe()->focused && f.indicator("first") == 0,
          "Semantic focus after the pointer: still hidden");
    check(f.ui.semantics().get({"first"})->focused, "Semantics report the new focus while hidden");
    f.state().key_down({Key::space}); f.state().key_up(Key::space);
    check(bool(f.second.focus()) && f.second.describe()->focused && f.indicator("second") == 1, "focus() after a key: still drawn");
    check(bool(f.ui.semantics().perform({"first"}, Verb::focus)) && f.indicator("first") == 1, "Semantic focus after a key: still drawn");
}
void editable_text_always_draws_but_read_only_follows_the_rule() {
    Fixture f;
    f.state().pointer_down("edit"); f.state().pointer_up("edit");
    check(f.edit.describe()->focused && !f.ui.focus_indicator_visible() && f.indicator("edit") > 0, "Clicked editable text draws the indicator");
    f.state().pointer_down("readonly"); f.state().pointer_up("readonly");
    check(f.readonly.describe()->focused && f.indicator("readonly") == 0, "Clicked read-only text draws none");
    check(bool(f.edit.focus()) && f.indicator("edit") > 0, "Programmatic focus on editable text draws it too");
    f.state().key_down({Key::tab});
    check(f.readonly.describe()->focused && f.indicator("readonly") > 0, "After a key, read-only text draws it by the normal rule");
}
void system_setting_forces_display_and_follows_changes() {
    Fixture f;
    f.state().set_system_keyboard_cues(true);
    f.state().pointer_down("first"); f.state().pointer_up("first");
    check(f.ui.focus_indicator_visible() && f.indicator("first") > 0, "System keyboard cues on: a click still draws");
    f.state().set_system_keyboard_cues(false);
    check(!f.ui.focus_indicator_visible() && f.indicator("first") == 0, "System setting turned off: the next paint follows the normal rule");
    f.state().key_down({Key::right});
    f.state().set_system_keyboard_cues(true); f.state().pointer_pressed(); f.state().set_system_keyboard_cues(false);
    check(!f.ui.focus_indicator_visible(), "The recorded state is not rewritten by the system setting");
}
void always_mode_forces_display_and_reverts_live() {
    Fixture f;
    f.state().pointer_down("first"); f.state().pointer_up("first");
    int invalidations = 0; f.state().on_invalidate([&] { ++invalidations; });
    auto settings = f.ui.presentation(); settings.focus_indicator = FocusIndicatorMode::always;
    check(bool(f.ui.set_presentation(settings)) && invalidations > 0, "Switching the mode repaints");
    check(f.ui.focus_indicator_visible() && f.indicator("first") > 0, "Always: a click still draws");
    check(f.ui.presentation() == settings && !(f.ui.presentation() == PresentationSettings{}), "operator== covers the mode");
    settings.focus_indicator = FocusIndicatorMode::automatic;
    check(bool(f.ui.set_presentation(settings)) && !f.ui.focus_indicator_visible() && f.indicator("first") == 0, "Back to automatic follows the rule");
    settings.focus_indicator = static_cast<FocusIndicatorMode>(7);
    check(f.ui.set_presentation(settings).status == Status::invalid_value, "Unknown modes are rejected");
}
void tooltip_waits_for_keyboard_after_a_click() {
    Ui ui;
    auto source = ui.add_button({.id = "source", .kind = Kind::button, .label = u"Source"}).value.value();
    auto tip = ui.add_tooltip("source.tip", source, u"Help", 100).value.value();
    auto& host = detail::access(ui);
    host.tooltip_time(0); host.window_active(true);
    host.pointer_down("source"); host.pointer_up("source");
    host.tooltip_time(1000);
    check(source.describe()->focused && !*tip.visible(), "A click does not make the focus tooltip appear");
    host.key_down({Key::right});
    host.tooltip_time(1099);
    check(!*tip.visible(), "After a key the C11 delay starts");
    host.tooltip_time(1100);
    check(*tip.visible(), "The tooltip appears after the delay");
    host.key_down({Key::escape});
    check(!*tip.visible(), "Escape still closes it");
    host.pointer_pressed();
    host.tooltip_time(5000);
    check(!*tip.visible(), "A pointer press ends the keyboard eligibility");
}
struct SliderPaint { std::vector<Rect> dots; int title_x = -1; int thumb = 0; };
SliderPaint paint_slider(Fixture& f) {
    auto laid = f.frame();
    auto semantic = f.state().describe("volume");
    const Placement* item = f.place(laid, "volume");
    check(item && semantic, "slider placement exists");
    PaintRoleProbe probe(f.state(), *item, *semantic, *appearance, 1.f, true);
    detail::component_type(item->kind).paint(probe);
    SliderPaint out;
    for (const auto& [role, rect] : probe.emitted) {
        if (static_cast<unsigned>(role) & static_cast<unsigned>(detail::PaintRole::focus_indicator)) out.dots.push_back(rect);
        else if (out.title_x < 0) out.title_x = rect.x;
    }
    for (const auto& command : probe.recording.list.commands)
        if (command.type == detail::DrawCommand::Type::rounded && command.role == detail::PaintRole::content) out.thumb = command.rect.w;
    return out;
}
void slider_focus_is_a_dot_left_of_the_title() {
    int previous = 0;
    for (int size : {12, 16, 28}) {
        Fixture f;
        auto settings = f.ui.presentation(); settings.text_size = size;
        check(bool(f.ui.set_presentation(settings)), "text size accepted");
        f.slider.focus();
        auto hidden = paint_slider(f);
        check(hidden.dots.empty(), "Hidden: no dot and no underline");
        f.state().key_down({Key::tab}); f.state().key_down({Key::tab, true});  // visible; focus returns to the slider
        check(f.slider.describe()->focused, "slider keeps focus after Tab, Shift+Tab");
        auto shown = paint_slider(f);
        check(shown.dots.size() == 1, "Shown: exactly one focus_indicator mark (the dot, no underline)");
        const Rect dot = shown.dots[0];
        check(dot.w == dot.h && dot.w > previous, "Dot diameter grows with the text size");
        previous = dot.w;
        check(dot.x + dot.w <= shown.title_x && dot.x >= 0, "The dot sits left of the title text");
        check(shown.title_x == hidden.title_x, "Title x is the same with and without the indicator");
        check(shown.thumb > hidden.thumb, "The thumb grows only while the indicator is drawn");
    }
}
void slider_thumb_stays_normal_after_pointer_use() {
    Fixture f;
    auto laid = f.frame();
    const Placement* item = f.place(laid, "volume");
    check(item != nullptr, "slider placed");
    auto normal = paint_slider(f).thumb;
    f.state().pointer_down("volume");
    const int middle = item->slider_track.x + item->slider_track.w / 2;
    f.state().slider_pointer_down("volume", middle, item->slider_track);
    f.state().slider_pointer("volume", middle + 3);
    auto dragging = paint_slider(f);
    check(dragging.thumb == normal && dragging.dots.empty(), "Dragging with the mouse keeps the normal thumb and no dot");
    f.state().pointer_up("volume");
    auto released = paint_slider(f);
    check(released.thumb == normal && released.dots.empty(), "After the drag the thumb is still normal");
    f.state().key_down({Key::tab}); f.state().key_down({Key::tab, true});
    check(paint_slider(f).thumb > normal, "Tab into the slider enlarges the thumb");
}
}
