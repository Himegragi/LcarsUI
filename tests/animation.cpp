// #75 shared animation clock and the slider thumb transition, observed through the same paint
// entry the host uses and a fake time source. Seams: UiState::animationTime (fake clock),
// UiState::setSystemAnimations (simulated Windows setting), Ui::animating / SemanticTree::animating.
#include "core_internal.hpp"
#include "component_type.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include "focus_marks.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
using namespace lcars;
using test::check;

namespace {
// What the slider paints at the fake clock's current time.
struct ThumbProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::vector<Rect> thumbs, dots; std::vector<std::u16string> texts;  // dots: the focus ring (#90: two rounded fills, outer then inner), not the thumb
    detail::PaintRole current = detail::PaintRole::content;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect, Color) override {}
    void rounded(Rect rect, int, Color) override {
        if (current == detail::PaintRole::focusIndicator) dots.push_back(rect);
        else if (focusMarks::isThumb(rect)) thumbs.push_back(rect);
    }
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color) override { texts.emplace_back(value); }
    int center() const { return thumbs.empty() ? -1 : thumbs.back().x + thumbs.back().w / 2; }
    bool titled(std::u16string_view title) const { return std::find(texts.begin(), texts.end(), std::u16string(title)) != texts.end(); }
};
// A volume slider 0..100 step 5 at 45, written straight through to its model.
struct Volume {
    Ui ui;
    SliderHandle slider;
    int model = 45;
    std::function<void()> changed;  // the data source's change notification
    std::uint64_t now = 0;
    const Appearance& appearance;
    Rect track{};
    int width = 400;  // the window the slider is arranged in; a test narrows it to move the track under the pointer
    explicit Volume(const Appearance& look = webAppearance()) : appearance(look) {
        slider = ui.addSlider({.id = "volume", .label = u"Volume"}, {0, 100, 5}).value.value();
        slider.connectSource({[this] { return model; }, [this](std::function<void()> notify) { changed = std::move(notify); return [] {}; }});
        slider.connectImmediate([this](const Change<SliderHandle>& change) { model = change.value; if (changed) changed(); return Result{}; });
        detail::access(ui).animationTime([this] { return now; });
        slider.focus();
        paint();
    }
    detail::UiState& state() { return detail::access(ui); }
    ThumbProbe paint() {
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::component(slider), ui, appearance, measure, width, 200, 1.f, scroll);
        const auto* item = frame.find(slider);
        check(item != nullptr, "slider is arranged");
        track = item->sliderTrack;
        auto semantic = state().describe(item->id);
        ThumbProbe probe(state(), *item, *semantic, appearance, 1.f, true);
        detail::componentType(ComponentKind::slider).paint(probe);
        return probe;
    }
    int tick(int value) const { return track.x + static_cast<int>(std::lround(track.w * value / 100.0)); }
    Result press(Key key) { return state().keyDown({.key = key}); }
};
bool nearPx(int a, int b) { return std::abs(a - b) <= 1; }

void keyboardStepSlidesTheThumb() {
    Volume v;
    check(bool(v.press(Key::right)), "right arrow accepted");
    auto start = v.paint();
    check(*v.slider.draft() == 50 && v.model == 50, "value is 50 at once");
    check(v.ui.semantics().get({"volume"})->value == u"50", "semantic value is 50 at t=0");
    check(start.titled(u"Volume: 50"), "title shows 50 at t=0");
    check(nearPx(start.center(), v.tick(45)), "thumb is still on the 45 tick at t=0");
    check(v.ui.animating().value.value(), "animating at t=0");
    v.now = 75;
    int middle = v.paint().center();
    // Ease-out is past half way at half the time: 45 + 5 * 0.875 for a cubic curve.
    check(middle > (v.tick(45) + v.tick(50)) / 2 && middle < v.tick(50), "t=75ms is between the ticks and past half way");
    check(v.ui.animating().value.value(), "still animating at t=75");
    v.now = 150;
    check(nearPx(v.paint().center(), v.tick(50)), "t=150ms is on the 50 tick");
    check(!v.ui.animating().value.value(), "not animating at t=150");
}

// #72 x #75: the focus dot beside the title and the enlarged thumb belong to the focus-visible state, not to the
// thumb's position, so they hold while the thumb slides and follow the indicator when it hides and shows again.
void focusDotAndThumbSizeHoldWhileTheThumbSlides() {
    for (const auto* look : {&webAppearance(), &materialAppearance()}) {
        Volume v(*look);
        const int enlarged = focusMarks::enlargedThumb(*look), plain = focusMarks::plainThumb(*look);
        check(enlarged > plain, "the focus-visible thumb is larger than the plain one");
        check(bool(v.press(Key::right)) && v.state().focusIndicatorVisible(), "the arrow key shows the indicator and starts a slide");
        auto start = v.paint();
        check(v.ui.animating().value.value() && nearPx(start.center(), v.tick(45)), "sliding, thumb still on the 45 tick at t=0");
        check(start.thumbs.size() == 1 && focusMarks::isOneRing(static_cast<int>(start.dots.size())), "one thumb and one focus ring (two fills) at t=0");
        check(start.thumbs.back().w == enlarged, "the thumb is enlarged while focus-visible at t=0");
        v.now = 75;
        auto middle = v.paint();
        check(v.ui.animating().value.value() && middle.center() > v.tick(45) && middle.center() < v.tick(50), "mid-slide");
        check(focusMarks::isOneRing(static_cast<int>(middle.dots.size())) && middle.dots.back().x == start.dots.back().x && middle.dots.back().y == start.dots.back().y &&
              middle.dots.back().w == start.dots.back().w, "the focus ring stays put while the thumb moves");
        check(middle.thumbs.back().w == enlarged, "the thumb stays enlarged mid-slide");
        // A pointer press elsewhere hides the indicator: dot and enlargement go, the slide goes on.
        v.state().pointerPressed();
        auto hidden = v.paint();
        check(v.ui.animating().value.value() && nearPx(hidden.center(), middle.center()), "hiding the indicator does not restart or stop the slide");
        check(hidden.dots.empty() && hidden.thumbs.back().w == plain, "no focus ring and a plain thumb mid-slide once hidden");
        check(bool(v.press(Key::right)) && v.state().focusIndicatorVisible(), "a key shows the indicator again, retargeting the slide");
        auto again = v.paint();
        check(focusMarks::isOneRing(static_cast<int>(again.dots.size())) && again.thumbs.back().w == enlarged && again.dots.back().x == start.dots.back().x, "ring and enlarged thumb are back mid-slide");
        v.now = 75 + 150;
        auto done = v.paint();
        check(!v.ui.animating().value.value() && nearPx(done.center(), v.tick(55)), "the slide ends on 55");
        check(focusMarks::isOneRing(static_cast<int>(done.dots.size())) && done.thumbs.back().w == enlarged && done.dots.back().x == start.dots.back().x, "ring and enlarged thumb hold when the slide ended");
    }
}

void heldKeyTurnsFromTheDisplayedPosition() {
    Volume v;
    std::vector<int> centers;
    for (int i = 0; i < 6; ++i) {
        v.now = i * 30;  // key repeat, faster than one transition
        check(bool(v.press(Key::right)), "repeat accepted");
        centers.push_back(v.paint().center());
    }
    check(*v.slider.draft() == 75, "six steps right: 45 -> 75");
    for (std::size_t i = 1; i < centers.size(); ++i) check(centers[i] >= centers[i - 1], "the thumb never moves backwards");
    check(centers.front() == v.tick(45), "the first frame has not left the old tick");
    check(centers.back() < v.tick(75), "the thumb has not jumped to the final target");
    // Each retarget restarts the timing from where the thumb is shown: after the last repeat the
    // thumb starts from its displayed position, not from the old target.
    check(centers.back() <= v.tick(70), "the last repeat starts from the displayed position, behind the previous target");
    v.now = 5 * 30 + 150;
    check(nearPx(v.paint().center(), v.tick(75)), "150 ms after the last repeat the thumb is on 75");
    check(!v.ui.animating().value.value(), "settled");
}
void homeAndEndSlide() {
    Volume v;
    check(bool(v.press(Key::home)), "Home");
    check(*v.slider.draft() == 0, "Home sets the minimum at once");
    check(nearPx(v.paint().center(), v.tick(45)), "Home: the thumb is still on 45 at t=0");
    v.now = 75; int mid = v.paint().center();
    check(mid < v.tick(45) && mid > v.tick(0), "Home: the thumb is between the ticks at t=75");
    v.now = 150; check(nearPx(v.paint().center(), v.tick(0)), "Home: the thumb lands on 0");
    v.now = 1000;
    check(bool(v.press(Key::end)) && *v.slider.draft() == 100, "End sets the maximum at once");
    check(nearPx(v.paint().center(), v.tick(0)), "End: the thumb starts at 0");
    v.now = 1150; check(nearPx(v.paint().center(), v.tick(100)), "End: the thumb lands on 100");
}
}

// Press, move and release the pointer on the slider the way the host does.
struct Pointer {
    Volume& v;
    float scale;
    explicit Pointer(Volume& volume, float dpiScale = 1.f) : v(volume), scale(dpiScale) {}
    int at(double share) const { return v.track.x + static_cast<int>(v.track.w * share); }
    void press(double share) { pressAt(at(share)); }
    void pressAt(int x) {
        check(bool(v.state().pointerDown("volume")), "pointer down");
        v.state().sliderPointerDown("volume", x, v.track, scale);
    }
    void moveTo(int x) { v.state().sliderPointer("volume", x); }
    void move(double share) { v.state().sliderPointer("volume", at(share)); }
    void release() { v.state().pointerUp("volume"); }
};
void trackClickSlidesAndReleaseKeepsTheSlide() {
    Volume v; Pointer p(v);
    v.now = 1000;
    p.press(0.80);
    check(*v.slider.draft() == 80 && v.model == 80, "a track click sets 80 at once");
    check(nearPx(v.paint().center(), v.tick(45)), "t=0: the thumb has not left 45");
    v.now = 1040; p.release();
    int released = v.paint().center();
    check(released > v.tick(45) && released < v.tick(80), "releasing mid-slide does not jump");
    v.now = 1075; int mid = v.paint().center();
    check(mid > (v.tick(45) + v.tick(80)) / 2 && mid < v.tick(80), "t=75ms: past half way, not there yet");
    v.now = 1150;
    check(nearPx(v.paint().center(), v.tick(80)), "150 ms after the press the thumb is on 80 (release did not restart the timing)");
    check(!v.ui.animating().value.value(), "the click transition has ended");
}
void dragFollowsTheHandAndReleaseSlidesToTheTick() {
    Volume v; Pointer p(v);
    v.now = 1000;
    p.press(0.47);
    check(*v.slider.draft() == 45 && !v.ui.animating().value.value(), "pressing on the thumb changes nothing and plays nothing");
    p.move(0.72);
    check(*v.slider.draft() == 70, "dragging sets 70");
    check(nearPx(v.paint().center(), p.at(0.72)), "while dragging the thumb is under the pointer, no animation");
    check(!v.ui.animating().value.value(), "dragging is not animating");
    v.now = 1010; p.move(0.52);
    check(nearPx(v.paint().center(), p.at(0.52)), "the thumb keeps following the hand");
    p.move(0.72);
    v.now = 2000; p.release();
    check(nearPx(v.paint().center(), p.at(0.72)), "t=0 after release: still where the pointer left it");
    check(v.ui.animating().value.value(), "release starts the slide");
    v.now = 2075; int mid = v.paint().center();
    check(mid < p.at(0.72) && mid > v.tick(70), "t=75ms after release: between the pointer and the tick");
    v.now = 2150;
    check(nearPx(v.paint().center(), v.tick(70)), "t=150ms after release: on the 70 tick");
    check(!v.ui.animating().value.value(), "settled after release");
}
void pressThenDragAwayFollowsTheHand() {
    Volume v; Pointer p(v);
    v.now = 1000; p.press(0.80);          // starts a click slide toward 80
    v.now = 1030; p.move(0.30);           // far enough to be a drag
    check(nearPx(v.paint().center(), p.at(0.30)), "once it is a drag the thumb is under the pointer");
    check(!v.ui.animating().value.value(), "the click slide was dropped");
    check(*v.slider.draft() == 30, "value follows the drag");
}
// Code review 2026-11-12 item 6: only the release of a drag slides; an interruption returns at once.
void interruptedDragReturnsToTheTickAtOnce() {
    Volume v; Pointer p(v);
    v.now = 1000; p.press(0.47); p.move(0.72);
    v.state().cancelInput();
    check(!v.ui.animating().value.value(), "an interrupted drag does not slide");
    check(nearPx(v.paint().center(), v.tick(70)), "the thumb is on the tick at once");
    v.now = 1150;
    check(nearPx(v.paint().center(), v.tick(70)) && !v.ui.animating().value.value(), "and stays there");
}
// Item 5: the held thumb is drawn on the CURRENT track, so the release slide starts from that pixel.
void releaseAfterTheTrackMovedSlidesFromTheDrawnThumb() {
    Volume v; Pointer p(v);
    v.now = 1000; p.press(0.47); p.move(0.72);
    v.width = 300;  // the layout shifts under the held thumb
    const int drawn = v.paint().center();
    check(drawn >= v.track.x && drawn <= v.track.x + v.track.w, "the held thumb sits on the current track");
    v.now = 2000; p.release();
    check(v.ui.animating().value.value(), "release starts the slide");
    check(nearPx(v.paint().center(), drawn), "t=0 after release: no jump from the drawn thumb");
    v.now = 2075; int mid = v.paint().center();
    check((mid < drawn && mid > v.tick(70)) || (mid > drawn && mid < v.tick(70)), "t=75ms: between the drawn thumb and the tick");
    v.now = 2150;
    check(nearPx(v.paint().center(), v.tick(70)) && !v.ui.animating().value.value(), "t=150ms: on the tick of the current track");
}
// Item 7: the 4 px threshold is 4 device-independent pixels, so it grows with the display scale.
void theClickDragThresholdScalesWithDpi() {
    for (float scale : {1.f, 1.5f, 2.f}) {
        const int threshold = static_cast<int>(std::lround(4 * scale));
        for (int extra : {0, 1}) {
            Volume v; Pointer p(v, scale);
            v.now = 1000;
            const int start = v.track.x + static_cast<int>(v.track.w * 0.80);
            p.pressAt(start);
            check(*v.slider.draft() == 80 && v.ui.animating().value.value(), "a track click slides toward 80");
            p.moveTo(start + threshold + extra);
            const std::string name = "scale " + std::to_string(scale) + ", " + std::to_string(threshold + extra) + " px";
            if (extra == 0) check(v.ui.animating().value.value(), (name + ": still a click").c_str());
            else check(!v.ui.animating().value.value() && nearPx(v.paint().center(), start + threshold + extra), (name + ": a drag follows the hand").c_str());
        }
    }
}

// Every way the value changes that is not the user's own input on this slider lands at once,
// even in the middle of a running slide.
void otherValueChangesLandAtOnce() {
    struct Change { const char* name; void (*apply)(Volume&, int); };
    const Change changes[] = {
        {"application code", [](Volume& v, int value) { check(bool(v.slider.setValue(value)), "set_value"); }},
        {"semantic set_value", [](Volume& v, int value) { check(bool(v.ui.semantics().perform({"volume"}, Verb::setValue, value)), "semantic set_value"); }},
        // The UIA RangeValue provider receives a double and performs the same semantic verb with its int.
        {"UIA SetValue", [](Volume& v, int value) { check(bool(v.ui.semantics().perform({"volume"}, Verb::setValue, static_cast<int>(static_cast<double>(value)))), "UIA SetValue"); }},
        {"data source", [](Volume& v, int value) { v.model = value; v.changed(); }},
    };
    for (const auto& change : changes) {
        for (bool running : {false, true}) {
            Volume v;
            if (running) { check(bool(v.press(Key::right)), "start a slide"); v.now = 40; }
            change.apply(v, 10);
            check(*v.slider.draft() == 10, change.name);
            check(nearPx(v.paint().center(), v.tick(10)), change.name);
            check(!v.ui.animating().value.value(), change.name);
        }
        // Back to the value a still-live slide was heading to: that is an external change too, so it lands at once.
        Volume v;
        check(bool(v.press(Key::right)), "start a slide toward 50"); v.now = 40;
        change.apply(v, 10); v.now = 80;
        change.apply(v, 50);
        check(*v.slider.draft() == 50, change.name);
        check(nearPx(v.paint().center(), v.tick(50)), (std::string(change.name) + ": back on the old target lands at once").c_str());
        check(!v.ui.animating().value.value(), (std::string(change.name) + ": the old slide does not resume").c_str());
    }
}
// Sliders have no edit history: Ctrl+Z / Ctrl+Y change nothing and leave a running slide alone.
void undoAndRedoKeysDoNotTouchASlider() {
    for (char16_t letter : {u'Z', u'Y'}) {
        Volume v;
        check(bool(v.press(Key::right)), "start a slide"); v.now = 40;
        const int before = v.paint().center();
        KeyPress chord; chord.key = Key::other; chord.ctrl = true; chord.letter = letter;
        v.state().keyDown(chord);
        check(*v.slider.draft() == 50 && v.ui.animating().value.value() && nearPx(v.paint().center(), before), "Ctrl+Z/Y neither change the value nor disturb the slide");
    }
}
void motionOffAndSystemSettingPlayNothing() {
    for (int source = 0; source < 2; ++source) {
        Volume v;
        auto switchOff = [&] {
            if (source == 0) { auto s = v.ui.presentation(); s.motion = Motion::off; check(bool(v.ui.setPresentation(s)), "motion off accepted"); }
            else v.state().setSystemAnimations(false);
        };
        auto switchOn = [&] {
            if (source == 0) { auto s = v.ui.presentation(); s.motion = Motion::automatic; check(bool(v.ui.setPresentation(s)), "motion automatic accepted"); }
            else v.state().setSystemAnimations(true);
        };
        switchOff();
        check(bool(v.press(Key::right)), "step with motion off");
        check(nearPx(v.paint().center(), v.tick(50)) && !v.ui.animating().value.value(), "motion off: the thumb is on 50 at once");
        Pointer p(v); p.press(0.47); p.move(0.72);
        check(nearPx(v.paint().center(), p.at(0.72)), "motion off: the drag follows the hand");
        p.release();
        check(nearPx(v.paint().center(), v.tick(70)) && !v.ui.animating().value.value(), "motion off: release snaps at once (#73)");
        switchOn();
        v.now = 500;
        check(bool(v.press(Key::left)), "step with motion back on");
        check(nearPx(v.paint().center(), v.tick(70)) && v.ui.animating().value.value(), "motion on again: the slide plays");
        // Switching off mid-slide lands the thumb at once.
        v.now = 540;
        switchOff();
        check(nearPx(v.paint().center(), v.tick(65)) && !v.ui.animating().value.value(), "switching off ends the running slide at its end");
    }
}
void presentationMotionIsValidatedAndCompared() {
    Ui ui;
    PresentationSettings settings = ui.presentation();
    check(settings.motion == Motion::automatic, "the default follows the system");
    settings.motion = static_cast<Motion>(7);
    check(ui.setPresentation(settings).status == Status::invalidValue, "an unknown motion value is refused");
    check(ui.presentation().motion == Motion::automatic, "and nothing changed");
    settings.motion = Motion::off;
    check(!(settings == ui.presentation()), "motion takes part in settings equality");
    check(bool(ui.setPresentation(settings)) && ui.presentation().motion == Motion::off, "off is accepted");
}
void switchingOffAsksForAFinalFrame() {
    Volume v; int invalidations = 0;
    v.state().onInvalidate([&] { ++invalidations; });
    check(bool(v.press(Key::right)), "start");
    auto s = v.ui.presentation(); s.motion = Motion::off;
    v.ui.setPresentation(s);
    check(invalidations > 0, "the host is asked to draw the landed frame");
}
void hiddenWindowEndsAndPreventsSlides() {
    Volume v;
    check(bool(v.press(Key::right)), "start");
    v.now = 40;
    v.state().setWindowHidden(true);
    check(!v.ui.animating().value.value(), "minimized: no animation is running");
    check(nearPx(v.paint().center(), v.tick(50)), "minimized: the running slide jumped to its end");
    check(bool(v.press(Key::right)) && !v.ui.animating().value.value(), "no new slide starts while hidden");
    v.state().setWindowHidden(false);
    v.now = 100;
    check(bool(v.press(Key::right)) && v.ui.animating().value.value(), "slides play again once visible");
}
void withoutATimeSourceChangesAreInstant() {
    Ui ui;
    auto slider = ui.addSlider({.id = "v", .label = u"V"}, {0, 100, 5}).value.value();
    int model = 45; std::function<void()> changed;
    slider.connectSource({[&] { return model; }, [&](std::function<void()> n) { changed = std::move(n); return [] {}; }});
    slider.connectImmediate([&](const Change<SliderHandle>& c) { model = c.value; if (changed) changed(); return Result{}; });
    slider.focus();
    check(bool(detail::access(ui).keyDown({.key = Key::right})) && model == 50, "step");
    check(!ui.animating().value.value(), "a core-only host that gave no clock never animates");
}
void animatingQueryAgreesBetweenEntries() {
    Volume v;
    check(v.ui.animating().status == Status::ok && v.ui.semantics().animating().status == Status::ok, "both entries answer");
    check(!v.ui.animating().value.value() && !v.ui.semantics().animating().value.value(), "idle: false from both");
    check(bool(v.press(Key::right)), "start");
    check(v.ui.animating().value.value() && v.ui.semantics().animating().value.value(), "running: true from both");
    v.now = 150;
    check(!v.ui.animating().value.value() && !v.ui.semantics().animating().value.value(), "ended: false from both");
    check(bool(v.press(Key::right)), "start again");
    Status typed{}, semantic{};
    std::thread([&] { typed = v.ui.animating().status; semantic = v.ui.semantics().animating().status; }).join();
    check(typed == Status::wrongThread && semantic == Status::wrongThread, "the same wrong_thread admission from both");
}
void clockEaseOutAndRetarget() {
    detail::AnimationClock clock; std::uint64_t now = 0;
    clock.timeSource([&] { return now; });
    clock.start("a", 10, 20);
    check(clock.value("a").value() == 10, "starts at the from value");
    now = 75; double half = *clock.value("a");
    check(std::abs(half - (10 + 10 * 0.875)) < 1e-9, "ease-out: 87.5% of the distance at half the time");
    clock.start("a", half, 30);  // retarget from where it is shown
    check(std::abs(*clock.value("a") - half) < 1e-9, "a retarget starts from the displayed value, without a jump");
    now = 225; check(!clock.value("a") && !clock.active("a"), "finished after the duration");
    clock.start("b", 0, 1); clock.start("c", 0, 1);
    clock.cancel("b"); check(!clock.active("b") && clock.active("c"), "cancel drops one track");
    clock.finishAll(); check(!clock.active("c"), "finish_all drops every track");
}

int main() {
    const test::Case cases[] = {
        {"keyboard step slides the thumb", keyboardStepSlidesTheThumb},
        {"focus dot and thumb size hold while sliding", focusDotAndThumbSizeHoldWhileTheThumbSlides},
        {"held key turns from the displayed position", heldKeyTurnsFromTheDisplayedPosition},
        {"Home and End slide", homeAndEndSlide},
        {"track click slides, release keeps the slide", trackClickSlidesAndReleaseKeepsTheSlide},
        {"drag follows the hand, release slides", dragFollowsTheHandAndReleaseSlidesToTheTick},
        {"press then drag away follows the hand", pressThenDragAwayFollowsTheHand},
        {"interrupted drag returns at once", interruptedDragReturnsToTheTickAtOnce},
        {"release after the track moved starts from the drawn thumb", releaseAfterTheTrackMovedSlidesFromTheDrawnThumb},
        {"click-drag threshold scales with dpi", theClickDragThresholdScalesWithDpi},
        {"undo and redo keys do not touch a slider", undoAndRedoKeysDoNotTouchASlider},
        {"other value changes land at once", otherValueChangesLandAtOnce},
        {"motion off and system setting play nothing", motionOffAndSystemSettingPlayNothing},
        {"presentation motion is validated", presentationMotionIsValidatedAndCompared},
        {"switching off asks for a final frame", switchingOffAsksForAFinalFrame},
        {"hidden window ends and prevents slides", hiddenWindowEndsAndPreventsSlides},
        {"no time source means instant", withoutATimeSourceChangesAreInstant},
        {"animating query agrees between entries", animatingQueryAgreesBetweenEntries},
        {"clock ease-out and retarget", clockEaseOutAndRetarget},
    };
    return test::run("animation", cases);
}
