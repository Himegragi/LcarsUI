#pragma once
// Issue #72: the focus indicator is drawn only after keyboard input (or when forced), never after a
// pointer press. Included by edit_form.cpp after PaintRoleProbe so every case runs under both appearances.
//
// Seams under test (confirmed by the ticket's Agent Brief):
//   - Ui::setPresentation / PresentationSettings::focusIndicator  (always-show switch)
//   - Ui::focusIndicatorVisible()                                  (read-only query)
//   - detail::UiState::keyDown / pointerDown / pointerPressed     (the host's real-input entries)
//   - detail::UiState::focus() and the handles' focus()              (programmatic focus must not change the state)
//   - detail::UiState::setSystemKeyboardCues(bool)                (SPI_GETKEYBOARDCUES test seam)
//   - detail::componentType(kind).paint(...) observed through PaintRoleProbe
#include "focus_marks.hpp"
namespace focusVisibility {
struct Fixture {
    Ui ui;
    ButtonHandle first, second;
    TextFieldHandle edit, readonly, note, noteReadonly;
    SliderHandle slider;
    ScrollState scroll;
    test::FakeMeasure measure;
    Fixture() {
        first = ui.addButton({.id = "first", .label = u"First"}).value.value();
        second = ui.addButton({.id = "second", .label = u"Second"}).value.value();
        edit = ui.addTextField({.id = "edit", .value = u"abc"}).value.value();
        readonly = ui.addTextField({.id = "readonly", .value = u"abc", .readOnly = true}).value.value();
        note = ui.addTextField({.id = "note", .value = u"a\nb\nc", .multiline = true, .visibleLines = 3}).value.value();
        noteReadonly = ui.addTextField({.id = "note_readonly", .value = u"a\nb\nc", .readOnly = true, .multiline = true, .visibleLines = 3}).value.value();
        slider = ui.addSlider({.id = "volume", .label = u"Volume"}, SliderRange{0, 100, 1}).value.value();
        detail::access(ui).windowActive(true);
    }
    detail::UiState& state() { return detail::access(ui); }
    Frame frame() {
        auto root = layout::column({layout::component(first), layout::component(second), layout::component(edit),
                                    layout::component(readonly), layout::component(note), layout::component(noteReadonly),
                                    layout::component(slider)}, 0, 8);
        return arrange(root, ui, *appearance, measure, 640, 480, 1.f, scroll);
    }
    const Placement* place(const Frame& laid, std::string_view id) {
        for (const auto& placement : laid.items) if (placement.id == id) return &placement;
        return nullptr;
    }
    // How many focus rings (#90) one component draws, from a fresh frame. A ring is a pair of rounded fills marked
    // focusIndicator (the band in the ring color, then what it frames), so the marks always come in twos.
    int indicator(std::string_view id) {
        auto laid = frame();
        auto semantic = state().describe(id);
        const Placement* item = place(laid, id);
        check(item && semantic, "focus visibility fixture finds the placement");
        PaintRoleProbe probe(state(), *item, *semantic, *appearance, 1.f, true);
        detail::componentType(item->kind).paint(probe);
        const int rings = focusMarks::rings(probe.count(detail::PaintRole::focusIndicator));
        check(rings >= 0, "focus indicator marks come as ring pairs (no strip, bar or dot)");
        return rings;
    }
};
void nothingIsDrawnBeforeAnyInput() {
    Fixture f;
    check(bool(f.first.focus()) && f.first.describe()->focused, "focus moves programmatically");
    check(f.indicator("first") == 0, "Startup: a focused button draws no focus indicator");
}
void tabAfterPointerDrawsIt() {
    Fixture f;
    f.state().pointerDown("first"); f.state().pointerUp("first");
    check(f.first.describe()->focused && f.indicator("first") == 0, "Pointer press: focused button has no indicator");
    f.state().keyDown({Key::tab});
    check(f.second.describe()->focused && f.indicator("second") == 1, "Tab: the new focus has an indicator");
}
void everyRealKeyOpensButModifierChordsDoNot() {
    for (auto key : {Key::tab, Key::up, Key::down, Key::left, Key::right, Key::enter, Key::space, Key::escape,
                     Key::home, Key::end, Key::backspace, Key::del}) {
        Fixture f; f.first.focus();
        check(!f.ui.focusIndicatorVisible(), "A fresh UI hides the indicator");
        f.state().keyDown({key});
        check(f.ui.focusIndicatorVisible(), "A plain key shows the indicator");
    }
    Fixture f; f.first.focus();
    KeyPress save; save.key = Key::other; save.ctrl = true; save.letter = u'S';
    KeyPress undo = save; undo.letter = u'Z';
    f.state().keyDown(save); f.state().keyDown(undo);
    check(!f.ui.focusIndicatorVisible() && f.indicator("first") == 0, "Ctrl+S and Ctrl+Z do not show the indicator");
    f.state().keyDown({Key::enter});
    check(f.ui.focusIndicatorVisible(), "Enter shows it");
    f.state().keyDown(save);
    check(f.ui.focusIndicatorVisible(), "A Ctrl chord does not hide it either");
}
// Code review 2026-11-12 item 2: a modifier on its own changes nothing. Alt closes menus and F10 does too when a menu is open
// (the host forwards both to the core); neither is a key that uses the keyboard, so neither shows the indicator.
void aLoneAltOrAMenuClosingF10DoesNotShowTheIndicator() {
    Fixture f; f.first.focus();
    auto menuOpen = [&] { return f.ui.contextMenuOpen().value.value_or(false); };
    f.state().keyDown({.key = Key::alt});
    check(!f.ui.focusIndicatorVisible() && f.indicator("first") == 0, "A lone Alt does not show it");
    f.state().keyDown({.key = Key::right});
    check(f.ui.focusIndicatorVisible(), "A real key shows it");
    f.state().keyDown({.key = Key::alt});
    check(f.ui.focusIndicatorVisible(), "And a lone Alt does not hide it either");
    f.state().pointerPressed();
    check(!f.ui.focusIndicatorVisible(), "A press hides it");
    check(bool(f.ui.openContextMenu(f.edit)) && menuOpen(), "A menu opens on the editable field");
    check(!f.ui.focusIndicatorVisible(), "Opening it through the typed API leaves the indicator hidden");
    f.state().keyDown({.key = Key::f10});
    check(!menuOpen() && !f.ui.focusIndicatorVisible(), "F10 closes the menu without showing the indicator");
    f.state().keyDown({.key = Key::f10, .shift = true});
    check(menuOpen() && f.ui.focusIndicatorVisible(), "Shift+F10 opens a menu from the keyboard: that is keyboard use");
}
void pointerHidesEverywhereButWheelDoesNot() {
    Fixture f; f.first.focus();
    f.state().keyDown({Key::right});
    auto laid = f.frame();
    check(f.ui.focusIndicatorVisible(), "Keyboard use shows the indicator");
    laid.wheel(f.ui, 1, 1, 48);
    check(f.ui.focusIndicatorVisible(), "The wheel does not change the state");
    f.state().pointerPressed();  // empty space or a component that ignores the pointer
    check(!f.ui.focusIndicatorVisible() && f.indicator("first") == 0, "A press on nothing hides it");
    f.state().keyDown({Key::tab});
    check(f.ui.focusIndicatorVisible(), "A key shows it again");
    check(f.state().pointerDown("missing").status != Status::ok && !f.ui.focusIndicatorVisible(), "A refused pointer press still hides it");
}
void programmaticFocusKeepsTheCurrentState() {
    Fixture f;
    f.state().pointerDown("first"); f.state().pointerUp("first");
    check(bool(f.second.focus()) && f.second.describe()->focused && f.indicator("second") == 0, "focus() after the pointer: still hidden");
    check(bool(f.ui.semantics().perform({"first"}, Verb::focus)) && f.first.describe()->focused && f.indicator("first") == 0,
          "Semantic focus after the pointer: still hidden");
    check(f.ui.semantics().get({"first"})->focused, "Semantics report the new focus while hidden");
    f.state().keyDown({Key::space}); f.state().keyUp(Key::space);
    check(bool(f.second.focus()) && f.second.describe()->focused && f.indicator("second") == 1, "focus() after a key: still drawn");
    check(bool(f.ui.semantics().perform({"first"}, Verb::focus)) && f.indicator("first") == 1, "Semantic focus after a key: still drawn");
}
void editableTextAlwaysDrawsButReadOnlyFollowsTheRule() {
    Fixture f;
    f.state().pointerDown("edit"); f.state().pointerUp("edit");
    check(f.edit.describe()->focused && !f.ui.focusIndicatorVisible() && f.indicator("edit") > 0, "Clicked editable text draws the indicator");
    f.state().pointerDown("readonly"); f.state().pointerUp("readonly");
    check(f.readonly.describe()->focused && f.indicator("readonly") == 0, "Clicked read-only text draws none");
    check(bool(f.edit.focus()) && f.indicator("edit") > 0, "Programmatic focus on editable text draws it too");
    f.state().keyDown({Key::tab});
    check(f.readonly.describe()->focused && f.indicator("readonly") > 0, "After a key, read-only text draws it by the normal rule");
}
// #79's multi-line field paints through the same box code as the single-line one; its indicator must come
// from the per-component "draws focus indicator now" value, not from `focused`.
void multilineTextFollowsTheTextFieldRule() {
    Fixture f;
    f.state().pointerDown("note"); f.state().pointerUp("note");
    check(f.note.describe()->focused && !f.ui.focusIndicatorVisible() && f.indicator("note") > 0,
          "Clicked editable multi-line text draws the indicator");
    f.state().pointerDown("note_readonly"); f.state().pointerUp("note_readonly");
    check(f.noteReadonly.describe()->focused && f.indicator("note_readonly") == 0, "Clicked read-only multi-line text draws none");
    check(f.note.describe() && !f.note.describe()->focused && f.indicator("note") == 0, "An unfocused multi-line field draws none");
    check(bool(f.note.focus()) && f.indicator("note") > 0, "Programmatic focus on editable multi-line text draws it too");
    f.state().keyDown({Key::tab});
    check(f.ui.focusIndicatorVisible(), "A key shows indicators");
    check(bool(f.noteReadonly.focus()) && f.indicator("note_readonly") > 0, "After a key, read-only multi-line text draws it by the normal rule");
}
void systemSettingForcesDisplayAndFollowsChanges() {
    Fixture f;
    f.state().setSystemKeyboardCues(true);
    f.state().pointerDown("first"); f.state().pointerUp("first");
    check(f.ui.focusIndicatorVisible() && f.indicator("first") > 0, "System keyboard cues on: a click still draws");
    f.state().setSystemKeyboardCues(false);
    check(!f.ui.focusIndicatorVisible() && f.indicator("first") == 0, "System setting turned off: the next paint follows the normal rule");
    f.state().keyDown({Key::right});
    f.state().setSystemKeyboardCues(true); f.state().pointerPressed(); f.state().setSystemKeyboardCues(false);
    check(!f.ui.focusIndicatorVisible(), "The recorded state is not rewritten by the system setting");
}
void alwaysModeForcesDisplayAndRevertsLive() {
    Fixture f;
    f.state().pointerDown("first"); f.state().pointerUp("first");
    int invalidations = 0; f.state().onInvalidate([&] { ++invalidations; });
    auto settings = f.ui.presentation(); settings.focusIndicator = FocusIndicatorMode::always;
    check(bool(f.ui.setPresentation(settings)) && invalidations > 0, "Switching the mode repaints");
    check(f.ui.focusIndicatorVisible() && f.indicator("first") > 0, "Always: a click still draws");
    check(f.ui.presentation() == settings && !(f.ui.presentation() == PresentationSettings{}), "operator== covers the mode");
    settings.focusIndicator = FocusIndicatorMode::automatic;
    check(bool(f.ui.setPresentation(settings)) && !f.ui.focusIndicatorVisible() && f.indicator("first") == 0, "Back to automatic follows the rule");
    settings.focusIndicator = static_cast<FocusIndicatorMode>(7);
    check(f.ui.setPresentation(settings).status == Status::invalidValue, "Unknown modes are rejected");
}
void tooltipWaitsForKeyboardAfterAClick() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"Source"}).value.value();
    auto tip = ui.addTooltip("source.tip", source, u"Help", 100).value.value();
    auto& host = detail::access(ui);
    host.tooltipTime(0); host.windowActive(true);
    host.pointerDown("source"); host.pointerUp("source");
    host.tooltipTime(1000);
    check(source.describe()->focused && !*tip.visible(), "A click does not make the focus tooltip appear");
    host.keyDown({Key::right});
    host.tooltipTime(1099);
    check(!*tip.visible(), "After a key the C11 delay starts");
    host.tooltipTime(1100);
    check(*tip.visible(), "The tooltip appears after the delay");
    host.keyDown({Key::escape});
    check(!*tip.visible(), "Escape still closes it");
    host.pointerPressed();
    host.tooltipTime(5000);
    check(!*tip.visible(), "A pointer press ends the keyboard eligibility");
}
struct SliderPaint { std::vector<Rect> ring; int titleX = -1; int thumb = 0; };
SliderPaint paintSlider(Fixture& f) {
    auto laid = f.frame();
    auto semantic = f.state().describe("volume");
    const Placement* item = f.place(laid, "volume");
    check(item && semantic, "slider placement exists");
    PaintRoleProbe probe(f.state(), *item, *semantic, *appearance, 1.f, true);
    detail::componentType(item->kind).paint(probe);
    SliderPaint out;
    for (const auto& [role, rect] : probe.emitted) {
        if (static_cast<unsigned>(role) & static_cast<unsigned>(detail::PaintRole::focusIndicator)) out.ring.push_back(rect);
        else if (out.titleX < 0) out.titleX = rect.x;
    }
    for (const auto& command : probe.recording.list.commands)
        if (command.type == detail::DrawCommand::Type::rounded && command.role == detail::PaintRole::content && focusMarks::isThumb(command.rect)) out.thumb = command.rect.w;
    return out;
}
// #90: the slider's indicator is the ring around the title and the track (no dot any more); the thumb still grows while it shows.
void sliderFocusIsARingAroundTheTitleAndTheTrack() {
    for (int size : {12, 16, 28}) {
        Fixture f;
        auto settings = f.ui.presentation(); settings.textSize = size;
        check(bool(f.ui.setPresentation(settings)), "text size accepted");
        f.slider.focus();
        auto hidden = paintSlider(f);
        check(hidden.ring.empty(), "Hidden: no ring");
        f.state().keyDown({Key::tab}); f.state().keyDown({Key::tab, true});  // visible; focus returns to the slider
        check(f.slider.describe()->focused, "slider keeps focus after Tab, Shift+Tab");
        auto shown = paintSlider(f);
        check(focusMarks::isOneRing(static_cast<int>(shown.ring.size())), "Shown: exactly one ring (two rounded fills), no dot or strip");
        const Rect outer = shown.ring[0], inner = shown.ring[1];
        check(inner.x - outer.x == 3 && inner.y - outer.y == 3 && outer.x + outer.w - (inner.x + inner.w) == 3 && outer.y + outer.h - (inner.y + inner.h) == 3,
              "The ring is 3 DIP thick on every side at every text size");
        check(shown.titleX >= inner.x && shown.titleX < inner.x + inner.w, "The title sits inside the ring");
        check(shown.titleX == hidden.titleX, "Title x is the same with and without the indicator");
        check(hidden.thumb == focusMarks::plainThumb(*appearance) && shown.thumb == focusMarks::enlargedThumb(*appearance), "The thumb grows only while the indicator is drawn");
    }
}
void sliderThumbStaysNormalAfterPointerUse() {
    Fixture f;
    auto laid = f.frame();
    const Placement* item = f.place(laid, "volume");
    check(item != nullptr, "slider placed");
    auto normal = paintSlider(f).thumb;
    f.state().pointerDown("volume");
    const int middle = item->sliderTrack.x + item->sliderTrack.w / 2;
    f.state().sliderPointerDown("volume", middle, item->sliderTrack);
    f.state().sliderPointer("volume", middle + 3);
    auto dragging = paintSlider(f);
    check(dragging.thumb == normal && dragging.ring.empty(), "Dragging with the mouse keeps the normal thumb and no ring");
    f.state().pointerUp("volume");
    auto released = paintSlider(f);
    check(released.thumb == normal && released.ring.empty(), "After the drag the thumb is still normal");
    f.state().keyDown({Key::tab}); f.state().keyDown({Key::tab, true});
    check(normal == focusMarks::plainThumb(*appearance) && paintSlider(f).thumb == focusMarks::enlargedThumb(*appearance), "Tab into the slider enlarges the thumb");
}
}
