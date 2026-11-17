// Pointer hover (#74): palette values, the presentation switch, the hover target state machine,
// the paint seam (PaintRole::hover) and the typed / semantic simulation entries.
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"
#include "isoduo/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "render.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <algorithm>
#include <cmath>
#include <set>
using namespace isoduo;
using test::check;
namespace {
const Appearance* const builtins[] = {&webAppearance(), &materialAppearance()};
const ThemeVariant variants[] = {ThemeVariant::light(), ThemeVariant::dark()};
std::string nameOf(const Appearance& a) { return std::string(a.name()); }
std::string labelOf(const Appearance& a, const ThemeVariant& v) { return nameOf(a) + " " + v.name; }

// ---- fixture: one of every pointer-operable component, plus a dialog ------------------------

struct Page {
    Ui ui; ScrollState scroll; test::FakeMeasure measure;
    ButtonHandle save, off, ok; CheckboxHandle box; RadioGroupHandle radio; DropdownHandle drop; ListHandle list;
    TabsHandle tabs; NavigationListHandle nav; SliderHandle slider; TextFieldHandle field, notes; ProgressHandle progress;
    LabelHandle label, vertical; DialogHandle dialog; TooltipHandle tip;
    TextFieldHandle aligned, alignedNotes;  // #80: aligned, spaced text; #79: multi-line
    int height = 2400;
    bool listFirst = false;  // lets a scroll test reach the list rows inside a short window
    Page() {
        auto& s = detail::access(ui);
        save = ui.addButton({.id = "save", .label = u"Save"}).value.value();
        off = ui.addButton({.id = "off", .label = u"Off", .enabled = false}).value.value();
        ok = ui.addButton({.id = "ok", .label = u"OK"}).value.value();
        box = ui.addCheckbox({.id = "box", .label = u"Box"}).value.value();
        radio = ui.addRadioGroup({.id = "radio", .label = u"Radio"},
            {{{"a"}, u"A"}, {{"b"}, u"B", false}, {{"c"}, u"C"}}).value.value();
        drop = ui.addDropdown({.id = "drop", .label = u"Drop"},
            {{{"d1"}, u"One"}, {{"d2"}, u"Two"}, {{"d3"}, u"Three", false}}).value.value();
        drop.connectSource({[] { return std::optional<OptionValue>{}; }, [](auto) { return [] {}; }});
        drop.connectImmediate([](const Change<DropdownHandle>&) { return Result{}; });
        std::vector<OptionItem> items;
        for (int i = 1; i <= 40; ++i) items.push_back({{"l" + std::to_string(i)}, u"Item " + std::u16string(1, char16_t(u'0' + i % 10)), i != 3});
        list = ui.addList({.id = "list", .label = u"List"}, items).value.value();
        tabs = ui.addTabs({.id = "tabs", .label = u"Tabs"},
            {{{"t1"}, u"One", true, {}}, {{"t2"}, u"Two", true, {}}, {{"t3"}, u"Three", false, {}}}).value.value();
        nav = ui.addNavigationList({.id = "nav", .label = u"Nav"},
            {{{"n1"}, u"General", true, {}}, {{"n2"}, u"Other", true, {}}, {{"n3"}, u"Locked", false, {}}}, {"n1"}).value.value();
        slider = ui.addSlider({.id = "slider", .label = u"Volume"}, {0, 100, 5}).value.value();
        slider.connectSource({[] { return 50; }, [](auto) { return [] {}; }});
        slider.connectImmediate([](const Change<SliderHandle>&) { return Result{}; });
        field = ui.addTextField({.id = "field", .label = u"Name", .value = u"abc"}).value.value();
        notes = ui.addTextField({.id = "notes", .label = u"Notes", .value = u"one\ntwo", .multiline = true, .visibleLines = 3}).value.value();
        aligned = ui.addTextField({.id = "aligned", .label = u"Right", .value = u"abc"}).value.value();
        check(bool(aligned.setTextAlign(TextAlign::right)), "a single-line field takes an alignment");
        alignedNotes = ui.addTextField({.id = "aligned_notes", .label = u"Centered", .value = u"one\ntwo\nthree", .multiline = true, .visibleLines = 3}).value.value();
        check(bool(alignedNotes.setTextAlign(TextAlign::center)) && bool(alignedNotes.setLineSpacing(1.5)) && bool(alignedNotes.setParagraphSpacing(8)),
              "a multi-line field takes alignment, line spacing and paragraph spacing");
        progress = ui.addProgress({.id = "progress", .label = u"Progress"}).value.value();
        label = ui.addLabel({.id = "label", .value = u"Hello"}).value.value();
        vertical = ui.addLabel({.id = "vertical", .value = u"Vertical"}).value.value();
        check(bool(vertical.setWritingMode(WritingMode::vertical)), "a label can be written vertically (#82)");
        dialog = ui.addDialog("dlg", {ok}, ok, save, [](bool) {}).value.value();
        tip = ui.addTooltip("save.tip", save, u"Save it", 100).value.value();
        s.windowActive(true);
        s.tooltipTime(0);
    }
    layout::Node root() const {
        auto page = layout::scroll(layout::column(listFirst ? std::vector<layout::Node>{layout::component(list), layout::component(save), layout::component(box)} : std::vector<layout::Node>{
            layout::component(save), layout::component(off), layout::component(box), layout::component(radio), layout::component(drop),
            layout::component(tabs), layout::component(nav), layout::component(slider), layout::component(field), layout::component(notes), layout::component(aligned), layout::component(alignedNotes), layout::component(progress),
            layout::component(label), layout::component(vertical), layout::component(list)}));
        return layout::modal(std::move(page), layout::component(ok), dialog);
    }
    Frame arrange(const Appearance& look = webAppearance(), int width = 480) {
        return isoduo::arrange(root(), ui, look, measure, width, height, 1.f, scroll);
    }
    detail::UiState& state() { return detail::access(ui); }
};
const Placement& placed(const Frame& frame, std::string_view id) {
    const auto* item = detail::findPlacement(frame, id);
    check(item != nullptr, "placement exists");
    return *item;
}
std::pair<int, int> middle(Rect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
std::pair<int, int> middleOf(const Frame& frame, std::string_view id) {
    const auto& p = placed(frame, id);
    return middle(intersect(p.bounds, p.clip));
}
std::pair<int, int> itemMiddle(const Frame& frame, std::string_view id, std::size_t index) {
    const auto& p = placed(frame, id);
    check(index < p.options.size(), "item placed");
    return middle(intersect(p.options[index], p.clip));
}
void move(Page& page, const Frame& frame, std::pair<int, int> at, bool touch = false) {
    page.state().pointerOver(frame, at.first, at.second, touch);
}
std::optional<HoverTarget> targetOf(Page& page) {
    auto q = page.ui.hover();
    check(bool(q), "hover query answers");
    return *q;
}
HoverTarget on(std::string id) { return {std::move(id), std::nullopt}; }
HoverTarget on(std::string id, std::string item) { return {std::move(id), OptionValue{std::move(item)}}; }
std::pair<int, int> emptySpace() { return {470, 2390}; }

// ---- hover target: real pointer ------------------------------------------------------------

void thePointerHoversComponentsAndSingleItems() {
    Page page; auto frame = page.arrange();
    check(!targetOf(page), "nothing is hovered at first");
    move(page, frame, middleOf(frame, "save"));
    check(targetOf(page) == on("save"), "the button is hovered");
    move(page, frame, middleOf(frame, "box"));
    check(targetOf(page) == on("box"), "moving to another component moves the target");
    move(page, frame, middleOf(frame, "slider"));
    check(targetOf(page) == on("slider"), "the slider is one target");
    move(page, frame, middleOf(frame, "field"));
    check(targetOf(page) == on("field"), "the text field is hovered");
    move(page, frame, itemMiddle(frame, "list", 1));
    check(targetOf(page) == on("list", "l2"), "a list row is its own target");
    move(page, frame, itemMiddle(frame, "radio", 2));
    check(targetOf(page) == on("radio", "c"), "a radio option is its own target");
    move(page, frame, itemMiddle(frame, "tabs", 1));
    check(targetOf(page) == on("tabs", "t2"), "a tab is its own target");
    move(page, frame, itemMiddle(frame, "nav", 1));
    check(targetOf(page) == on("nav", "n2"), "a navigation item is its own target");
    move(page, frame, emptySpace());
    check(!targetOf(page), "empty space hovers nothing");
}

void disabledComponentsAndItemsAreNeverTheTarget() {
    Page page; auto frame = page.arrange();
    move(page, frame, middleOf(frame, "off"));
    check(!targetOf(page), "a disabled button is not hovered");
    move(page, frame, itemMiddle(frame, "list", 2));
    check(targetOf(page) == on("list"), "a disabled row hovers the list, not the row");
    move(page, frame, itemMiddle(frame, "radio", 1));
    check(targetOf(page) == on("radio"), "a disabled radio option is not hovered");
    move(page, frame, itemMiddle(frame, "tabs", 2));
    check(targetOf(page) == on("tabs"), "a disabled tab is not hovered");
    move(page, frame, itemMiddle(frame, "nav", 2));
    check(targetOf(page) == on("nav"), "a disabled navigation item is not hovered");
    move(page, frame, middleOf(frame, "save"));
    page.save.enable(false);
    check(!targetOf(page), "disabling the hovered component ends its hover at once");
}

// Code review 2026-11-12 item 3: Frame::hit also returns labels and progress bars, which ignore the pointer.
void componentsThatIgnoreThePointerAreNeverTheHoverTarget() {
    Page page; auto frame = page.arrange();
    for (const char* id : {"label", "vertical", "progress"}) {
        move(page, frame, middleOf(frame, id));
        check(!targetOf(page), (std::string("hovering ") + id + " targets nothing").c_str());
    }
    move(page, frame, middleOf(frame, "save"));
    check(targetOf(page) == on("save"), "a button is hovered");
    move(page, frame, middleOf(frame, "label"));
    check(!targetOf(page), "moving from a button onto a label ends the hover");
}

void touchMessagesNeitherMakeNorChangeTheHover() {
    Page page; auto frame = page.arrange();
    move(page, frame, middleOf(frame, "box"), true);
    check(!targetOf(page), "a touch message hovers nothing");
    move(page, frame, middleOf(frame, "save"));
    move(page, frame, middleOf(frame, "box"), true);
    check(targetOf(page) == on("save"), "a touch message does not move a real hover");
    move(page, frame, emptySpace(), true);
    check(targetOf(page) == on("save"), "a touch message does not clear a real hover");
}

void leavingTheWindowOrLosingActivationClearsTheHover() {
    Page page; auto frame = page.arrange();
    move(page, frame, middleOf(frame, "save"));
    page.state().pointerLeft();
    check(!targetOf(page), "WM_MOUSELEAVE clears the hover");
    page.arrange();  // a re-layout must not bring it back: the pointer is gone
    check(!targetOf(page), "a frame after the pointer left does not hover");
    move(page, frame, middleOf(frame, "save"));
    page.state().windowActive(false);
    check(!targetOf(page), "losing activation clears the hover");
    move(page, frame, middleOf(frame, "box"));
    check(!targetOf(page), "an inactive window does not hover");
    page.state().windowActive(true);
    move(page, frame, middleOf(frame, "box"));
    check(targetOf(page) == on("box"), "hover resumes after activation");
}

void openingADialogClearsTheHoverAndBlocksTheBackground() {
    Page page; auto frame = page.arrange();
    move(page, frame, middleOf(frame, "save"));
    check(bool(page.dialog.open()), "dialog opens");
    check(!targetOf(page), "opening the modal clears the hover");
    frame = page.arrange();
    check(!targetOf(page), "the arranged modal frame does not bring it back");
    move(page, frame, middleOf(frame, "ok"));
    check(targetOf(page) == on("ok"), "components inside the dialog hover as usual");
    move(page, frame, emptySpace());
    check(!targetOf(page), "the blocked background hovers nothing");
    check(page.ui.simulateHover(page.save).status == Status::blockedByModal, "simulation on the background is blocked");
    check(bool(page.ui.simulateHover(page.ok)) && targetOf(page) == on("ok"), "simulation inside the dialog works");
}

void draggingASliderKeepsTheHoverOnIt() {
    Page page; auto frame = page.arrange();
    auto& s = page.state();
    move(page, frame, middleOf(frame, "slider"));
    check(bool(s.pointerDown("slider")) && bool(s.sliderPointerDown("slider", middleOf(frame, "slider").first, placed(frame, "slider").sliderTrack)), "slider press");
    move(page, frame, middleOf(frame, "save"));
    check(targetOf(page) == on("slider"), "the hover stays on the dragged slider over another button");
    move(page, frame, emptySpace());
    check(targetOf(page) == on("slider"), "and over empty space");
    s.sliderPointer("slider", 300);
    move(page, frame, middleOf(frame, "save"));
    check(bool(s.pointerUp("slider")), "slider release");
    frame = page.arrange();
    check(targetOf(page) == on("save"), "after the release the hover follows the pointer position again");
}

void capturedHoverHoldsUntilReleased() {
    Page page; auto frame = page.arrange();
    auto& s = page.state();
    move(page, frame, middleOf(frame, "field"));
    s.hoverCapture("field");
    move(page, frame, middleOf(frame, "save"));
    check(targetOf(page) == on("field"), "a text selection drag keeps the field hovered");
    s.hoverRelease();
    frame = page.arrange();
    check(targetOf(page) == on("save"), "release re-evaluates at the last pointer position");
    s.hoverCapture({});
    move(page, frame, middleOf(frame, "box"));
    check(targetOf(page) == on("save"), "capturing without an id freezes the current target (scrollbar thumb drag)");
    s.hoverRelease();
}

// ---- simulation ----------------------------------------------------------------------------

void simulatedHoverHasTheEffectOfThePointer() {
    Page page; auto frame = page.arrange();
    check(bool(page.ui.simulateHover(page.save)), "simulation accepted");
    check(targetOf(page) == on("save"), "the simulated target is queryable");
    check(!*page.tip.visible(), "the tooltip waits for its delay");
    page.state().tooltipTime(100);
    check(*page.tip.visible(), "the tooltip appears after the C11 delay");
    move(page, frame, middleOf(frame, "box"));
    check(targetOf(page) == on("box") && !*page.tip.visible(), "the real pointer takes over at its first move");
    check(bool(page.ui.simulateHover(page.list, OptionValue{"l4"})) && targetOf(page) == on("list", "l4"), "an item can be simulated");
    check(bool(page.ui.clearSimulatedHover()) && !targetOf(page), "the simulation can be cleared");
    check(bool(page.ui.clearSimulatedHover()), "clearing again is harmless");
    check(bool(page.ui.simulateHover(page.save)), "simulate before clearing a real hover");
    move(page, frame, middleOf(frame, "box"));
    check(bool(page.ui.clearSimulatedHover()) && targetOf(page) == on("box"), "clearing leaves a real hover alone");
}

void simulationIsAdmittedLikeTheTypedOperations() {
    Page page; page.arrange();
    check(page.ui.simulateHover(page.off).status == Status::disabled, "a disabled component answers disabled");
    check(page.ui.simulateHover(page.list, OptionValue{"l3"}).status == Status::disabled, "a disabled item answers disabled");
    check(page.ui.simulateHover(page.list, OptionValue{"nope"}).status == Status::notFound, "a missing item answers not_found");
    check(page.ui.simulateHover(page.save, OptionValue{"x"}).status == Status::notFound, "an item on an item-less component answers not_found");
    check(page.ui.simulateHover(ComponentHandle{}).status == Status::notFound, "an empty handle answers not_found");
    check(!targetOf(page), "refusals leave no hover");
    ButtonHandle stale;
    { Ui other; stale = other.addButton({.id = "x"}).value.value(); }
    check(page.ui.simulateHover(stale).status == Status::notFound, "a handle of a dead Ui answers not_found");
    check(page.ui.simulateHover(page.tabs, OptionValue{"t2"}).status == Status::ok, "tab items can be simulated");
}

void aSimulatedHoverWaitsWhileADragHoldsTheHover() {
    Page page; auto frame = page.arrange();
    auto& s = page.state();
    move(page, frame, middleOf(frame, "slider"));
    s.pointerDown("slider"); s.sliderPointerDown("slider", 100, placed(frame, "slider").sliderTrack);
    check(page.ui.simulateHover(page.save).status == Status::busy && targetOf(page) == on("slider"), "no simulation during a drag");
    s.pointerUp("slider");
}

// ---- paint seam ----------------------------------------------------------------------------

using detail::PaintRole;
bool hasRole(const detail::DrawCommand& c, PaintRole role) { return (static_cast<unsigned>(c.role) & static_cast<unsigned>(role)) != 0; }
struct HoverProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    detail::RecordingCanvas recording{4000, 4000, {0, 0, 0}};
    void role(PaintRole value) override { recording.currentRole = value; }
    void fill(Rect r, Color c) override { recording.fill(r, c); }
    void rounded(Rect r, int radius, Color c) override { recording.rounded(r, radius, c); }
    void overlay(Rect r, int radius, Color c, unsigned char alpha) override { recording.overlay(r, radius, c, alpha); }
    void clip(Rect r) override { recording.clip = intersect(recording.clip, r); }
    void text(std::u16string_view, int, int, int, Color) override {}
    std::vector<detail::DrawCommand> commandsWith(PaintRole role) const {
        std::vector<detail::DrawCommand> found;
        for (const auto& c : recording.list.commands) if (hasRole(c, role)) found.push_back(c);
        return found;
    }
};
HoverProbe paint(Page& page, const Placement& item, const Appearance& look) {
    auto semantics = page.state().describe(item.id);
    check(semantics.has_value(), "placement describes");
    HoverProbe probe(page.state(), item, *semantics, look, 1.f, true);
    detail::componentType(item.kind).paint(probe);
    return probe;
}
std::vector<const Placement*> paintedPlacements(const Frame& frame) {
    std::vector<const Placement*> all;
    for (const auto& item : frame.items) all.push_back(&item);
    for (const auto& item : frame.overlays) if (!item.tooltip) all.push_back(&item);
    return all;
}
bool inside(Rect inner, Rect outer) { return intersect(inner, outer) == inner; }
Rect rectOf(const detail::DrawCommand& c) { return c.rect; }

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
void hoverCommandsFollowTheTargetEverywhere() {
    std::set<std::string> seen;  // kinds (and the dropdown panel) that showed an overlay in every configuration
    std::set<std::string> seenIds;
    for (const auto* look : builtins) for (const auto& variant : variants) for (auto density : {Density::standard, Density::compact}) for (bool popup : {false, true}) {
        Page page;
        auto settings = page.ui.presentation(); settings.variant = variant; settings.density = density;
        check(bool(page.ui.setPresentation(settings)), "settings accepted");
        if (popup) check(bool(page.drop.open()), "the dropdown opens so its panel is part of the frame");  // the panel covers what lies below it
        auto frame = page.arrange(*look);
        auto palette = page.state().effectivePalette(*look);
        for (const auto* source : paintedPlacements(frame)) for (const auto& rest : rests(*source)) {
            auto context = labelOf(*look, variant) + " " + (density == Density::compact ? "compact " : "standard ") + (popup ? "popup " : "") + rest.context;
            move(page, frame, rest.at);
            auto target = targetOf(page);
            for (const auto* q : paintedPlacements(frame)) {
                auto probe = paint(page, *q, *look);
                auto hovers = probe.commandsWith(PaintRole::hover);
                auto kindClickable = detail::componentType(q->kind).acceptsPointer();
                bool itemBased = !q->options.empty();
                bool expected = target && target->id == q->id && kindClickable &&
                    (itemBased ? target->item.has_value() : !target->item.has_value());
                check(expected == !hovers.empty(), (context + ": hover commands for " + q->id + (q->dropdownPanel ? " panel" : "")).c_str());
                if (!expected) continue;
                seen.insert(std::to_string(static_cast<int>(q->kind)) + (q->dropdownPanel ? "p" : ""));
                seenIds.insert(q->id);
                for (const auto& command : hovers) {
                    check(command.color.r == palette.hover.r && command.color.g == palette.hover.g && command.color.b == palette.hover.b &&
                          command.color.a == palette.hoverAlpha, (context + ": overlay uses the palette's hover color").c_str());
                    Rect region = intersect(q->bounds, q->clip);
                    if (target->item) {
                        auto semantics = page.state().describe(q->id);
                        auto index = std::find_if(semantics->items.begin(), semantics->items.end(), [&](const auto& o) { return o.value == *target->item; }) - semantics->items.begin();
                        region = q->options.at(static_cast<std::size_t>(index) - q->optionStart);
                    }
                    check(inside(rectOf(command), region) || q->kind == ComponentKind::slider, (context + ": hover stays inside the hovered component or item").c_str());
                }
            }
        }
    }
    // Not vacuous: every clickable kind, and the open dropdown's panel, showed an overlay.
    std::set<std::string> expectedKinds;
    for (auto kind : {ComponentKind::textField, ComponentKind::button, ComponentKind::checkbox, ComponentKind::radioGroup, ComponentKind::slider, ComponentKind::dropdown, ComponentKind::list, ComponentKind::tabs, ComponentKind::navigationList})
        expectedKinds.insert(std::to_string(static_cast<int>(kind)));
    expectedKinds.insert(std::to_string(static_cast<int>(ComponentKind::dropdown)) + "p");
    std::string missing;
    for (const auto& k : expectedKinds) if (!seen.contains(k)) missing += " " + k;
    check(missing.empty(), ("the driver saw an overlay for every clickable kind and the dropdown panel; missing:" + missing).c_str());
    // The components merged after #74 branched: aligned and multi-line fields hover like any field; a (vertical) label is not clickable.
    for (const char* id : {"field", "notes", "aligned", "aligned_notes"}) check(seenIds.contains(id), (std::string("the driver saw a hover overlay on ") + id).c_str());
    for (const char* id : {"label", "vertical"}) check(!seenIds.contains(id), (std::string("labels are not clickable and never hover: ") + id).c_str());
}

// ---- context menu (#85) --------------------------------------------------------------------

std::string highlightedItem(Page& page) {
    auto menu = page.ui.contextMenu();
    if (!menu) return "<closed>";
    for (const auto& item : menu->items) if (item.highlighted) return item.id;
    return "<none>";
}
// A menu row's hover is the menu's own row highlight, not a hover overlay: the host runs updateHover and then
// menuHighlight on every move. One driver over every row of the open menu, appearance, variant and density.
void contextMenuRowsHoverAsTheirHighlight() {
    int rowsSeen = 0;
    for (const auto* look : builtins) for (const auto& variant : variants) for (auto density : {Density::standard, Density::compact}) {
        Page page;
        auto settings = page.ui.presentation(); settings.variant = variant; settings.density = density;
        check(bool(page.ui.setPresentation(settings)), "settings accepted");
        auto frame = page.arrange(*look);
        move(page, frame, middleOf(frame, "save"));
        check(targetOf(page) == on("save"), "the button is hovered before the menu opens");
        check(bool(page.ui.openContextMenu(page.field)), "the field's menu opens");
        check(!targetOf(page), "an open menu takes the pointer: no component is hovered");
        frame = page.arrange(*look);
        const auto* panel = frame.menuPanel();
        check(panel != nullptr && !panel->menuRows.empty(), "the menu is placed");
        if (!panel) continue;
        auto context = labelOf(*look, variant) + (density == Density::compact ? " compact" : " standard");
        for (const auto& row : panel->menuRows) {
            auto at = middle(row.bounds);
            check(frame.menuItemAt(at.first, at.second) == row.id, (context + ": the row is under its own middle").c_str());
            auto before = highlightedItem(page);
            move(page, frame, at);  // the host's WM_MOUSEMOVE: hover first, then the menu row
            if (auto id = frame.menuItemAt(at.first, at.second)) page.state().menuHighlight(*id);
            if (row.separator) { check(highlightedItem(page) == before, (context + ": a line keeps the highlight").c_str()); continue; }
            ++rowsSeen;
            check(highlightedItem(page) == row.id, (context + ": hovering " + row.id + " highlights it").c_str());
            check(!targetOf(page), (context + ": no component hover while the menu is open").c_str());
            for (const auto* q : paintedPlacements(frame)) {
                if (q->menu) continue;  // the menu panel is painted by the host, with its own row highlight
                check(paint(page, *q, *look).commandsWith(PaintRole::hover).empty(), (context + ": no hover overlay under an open menu for " + q->id).c_str());
            }
        }
        // Touch moves do not highlight; the typed simulation is refused while the menu owns the pointer.
        check(page.ui.simulateHover(page.save).status == Status::blockedByModal, "typed simulation answers blocked_by_modal");
        auto& tree = page.ui.semantics();
        const auto menu = menuNodeId("field");
        check(tree.perform({"field"}, Verb::hover).status == Status::blockedByModal, "the semantic verb agrees with the typed entry");
        check(tree.perform({menu}, Verb::hover).status == Status::notSupported, "the menu panel itself is not hoverable");
        check(bool(tree.perform({menu, OptionValue{"lcars.select_all"}}, Verb::hover)) && highlightedItem(page) == "lcars.select_all", "the semantic hover on a row highlights it");
        // Closing the menu gives the pointer back at once, to what lies under its resting position.
        check(bool(page.ui.closeContextMenu()), "the menu closes");
        frame = page.arrange(*look);
        move(page, frame, middleOf(frame, "box"));
        check(targetOf(page) == on("box"), "after the menu the real pointer hovers again");
    }
    check(rowsSeen > 0, "the driver visited menu rows");
}

void hoveringTheSliderTrackFarFromTheThumbStillMarksTheThumb() {
    for (const auto* look : builtins) {
        Page page; auto frame = page.arrange(*look);
        const auto& slider = placed(frame, "slider");
        auto track = slider.sliderTrack;
        move(page, frame, {track.x + track.w - 3, track.y + track.h / 2});
        check(targetOf(page) == on("slider"), "the pointer on the far end of the track hovers the slider");
        auto hovers = paint(page, slider, *look).commandsWith(PaintRole::hover);
        check(!hovers.empty(), "the thumb carries a hover command");
        int thumbX = track.x + track.w / 2;  // the value is 50 of 0-100
        bool onThumb = std::any_of(hovers.begin(), hovers.end(), [&](const auto& c) { return c.rect.x <= thumbX && thumbX < c.rect.x + c.rect.w; });
        check(onThumb, "the hover command sits on the thumb, not under the pointer");
    }
}

void aHoveredSelectedItemKeepsItsSelectionAndGainsTheOverlay() {
    for (const auto* look : builtins) {
        Page page; auto frame = page.arrange(*look);
        move(page, frame, itemMiddle(frame, "nav", 0));  // n1 is the selected page
        check(targetOf(page) == on("nav", "n1"), "the selected navigation item is hovered");
        auto probe = paint(page, placed(frame, "nav"), *look);
        check(!probe.commandsWith(PaintRole::selection).empty() && !probe.commandsWith(PaintRole::hover).empty(),
              "selection and hover are both painted for the selected item");
        // Hover goes over the selection color: its command comes after the selection commands.
        const auto& commands = probe.recording.list.commands;
        auto lastSelection = std::find_if(commands.rbegin(), commands.rend(), [](const auto& c) { return hasRole(c, PaintRole::selection); });
        auto lastHover = std::find_if(commands.rbegin(), commands.rend(), [](const auto& c) { return hasRole(c, PaintRole::hover); });
        check(lastHover < lastSelection, "hover is painted over the selection");
    }
}

void theHoverSwitchHidesTheOverlayButNotTheTargetOrTheTooltip() {
    Page page; auto frame = page.arrange();
    auto settings = page.ui.presentation(); settings.hover = Hover::off;
    check(bool(page.ui.setPresentation(settings)), "switch off");
    move(page, frame, middleOf(frame, "save"));
    check(targetOf(page) == on("save"), "the target is still reported with the switch off");
    for (const auto* q : paintedPlacements(frame))
        check(paint(page, *q, webAppearance()).commandsWith(PaintRole::hover).empty(), "no hover command with the switch off");
    page.state().tooltipTime(100);
    check(*page.tip.visible(), "the tooltip still follows the pointer");
    check(bool(page.ui.simulateHover(page.box)), "simulation is accepted with the switch off");
    check(paint(page, placed(frame, "box"), webAppearance()).commandsWith(PaintRole::hover).empty(), "simulated hover is hidden too");
    settings.hover = Hover::on;
    check(bool(page.ui.setPresentation(settings)), "switch on");
    check(!paint(page, placed(frame, "box"), webAppearance()).commandsWith(PaintRole::hover).empty(), "switching back on shows the overlay at once");
}

void hoverOverlayBlendsOverTheSurfaceWithThePaletteAlpha() {
    detail::RecordingCanvas canvas(10, 10, {200, 200, 200});
    canvas.currentRole = PaintRole::hover;
    canvas.overlay({2, 2, 4, 4}, 0, {0, 0, 0}, 15);
    auto image = detail::renderCpu(canvas.list, 10, 10);
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
    void fill(Rect, Color) override { seen.push_back({current, overlayAlpha()}); }
    void rounded(Rect, int, Color) override { seen.push_back({current, overlayAlpha()}); }
    void clip(Rect) override {}
    void text(std::u16string_view, int, int, int, Color) override {}
};
void theDefaultOverlayPathKeepsTheAlphaVisible() {
    Page page; auto frame = page.arrange();
    move(page, frame, middleOf(frame, "save"));
    const auto& item = placed(frame, "save");
    auto semantics = page.state().describe("save");
    check(semantics.has_value(), "the button describes");
    AlphaProbe probe(page.state(), item, *semantics, webAppearance(), 1.f, true);
    detail::componentType(item.kind).paint(probe);
    auto palette = page.state().effectivePalette(webAppearance());
    int translucent = 0, opaque = 0;
    for (const auto& s : probe.seen) {
        if (s.role == PaintRole::hover) { ++translucent; check(s.alpha == palette.hoverAlpha, "the hover fill carries the palette alpha"); }
        else { ++opaque; check(s.alpha == 255, "every other fill is opaque"); }
    }
    check(translucent == 1 && opaque > 0, "one translucent fill among the opaque ones");
}

void hoverNeverPaintsForAZeroAlphaPalette() {
    Page page;
    check(bool(page.ui.setPaletteOverrides({{nameOf(webAppearance()), ThemeVariant::light(), "hover_alpha", 0}})), "alpha 0 accepted");
    auto frame = page.arrange();
    move(page, frame, middleOf(frame, "save"));
    check(paint(page, placed(frame, "save"), webAppearance()).commandsWith(PaintRole::hover).empty(), "alpha 0 paints nothing");
}

// ---- re-evaluation after scrolling and reflow -----------------------------------------------

std::optional<std::string> idUnder(const Frame& frame, std::pair<int, int> at) {
    const auto* hit = frame.hit(at.first, at.second);
    return hit ? std::optional<std::string>(hit->id) : std::nullopt;
}

void scrollingUnderARestingPointerMovesTheHoverAtOnce() {
    Page page; page.height = 300;
    auto frame = page.arrange();
    auto at = middleOf(frame, "save");
    move(page, frame, at);
    check(targetOf(page) == on("save"), "the pointer rests on the first button");
    check(frame.wheel(page.ui, at.first, at.second, -120), "the wheel scrolls the page");
    auto after = page.arrange();  // what the host does for the repaint the wheel requests
    auto expected = idUnder(after, at);
    check(expected && *expected != "save", "after the scroll another component lies under the pointer");
    auto target = targetOf(page);
    check(target && target->id == *expected, "the hover is on the new component without a mouse move");
    // Scrolling the list under a resting pointer changes the hovered item.
    Page tall; tall.height = 400; tall.listFirst = true;
    auto listFrame = tall.arrange();
    auto row = itemMiddle(listFrame, "list", 5);
    move(tall, listFrame, row);
    auto before = targetOf(tall);
    check(before && before->id == "list" && before->item, "a list row is hovered");
    check(listFrame.wheel(tall.ui, row.first, row.second, -96), "the page scrolls");
    auto scrolled = tall.arrange();
    auto now = targetOf(tall);
    check(now && now->id == "list" && now->item && !(now->item == before->item), "another row is now under the pointer and hovered");
    check(scrolled.optionAt(placed(scrolled, "list"), row.first, row.second).has_value(), "and that row is the one the frame puts there");
}

void reflowUnderARestingPointerMovesTheHoverAtOnce() {
    Page page; page.height = 3000;
    auto frame = page.arrange();
    auto at = middleOf(frame, "field");
    move(page, frame, at);
    auto before = targetOf(page);
    check(before && before->id == "field", "the pointer rests on the text field");
    auto settings = page.ui.presentation(); settings.textSize = 28;
    check(bool(page.ui.setPresentation(settings)), "a keyboard shortcut enlarges the text");
    auto after = page.arrange();
    auto expected = idUnder(after, at);
    auto target = targetOf(page);
    check(expected && *expected != "field", "the larger text moved another component under the pointer");
    check(target && target->id == *expected, "the hover follows the reflow without a mouse move");
    settings.density = Density::compact;
    check(bool(page.ui.setPresentation(settings)), "density changes");
    auto compact = page.arrange();
    auto now = idUnder(compact, at);
    auto afterDensity = targetOf(page);
    check(now.has_value() ? afterDensity.has_value() && afterDensity->id == *now : !afterDensity, "and so does a density change");
}

void aFrameDoesNotMoveASimulatedHover() {
    Page page; page.height = 300;
    auto frame = page.arrange();
    auto saveAt = middleOf(frame, "save");
    move(page, frame, saveAt);
    check(bool(page.ui.simulateHover(page.box)), "simulated hover");
    frame.wheel(page.ui, 40, 20, -120);
    page.arrange();
    check(targetOf(page) == on("box"), "a re-layout leaves a simulated hover alone");
    check(bool(page.ui.clearSimulatedHover()), "clear");
    auto fresh = page.arrange();
    auto expected = idUnder(fresh, saveAt);
    auto target = targetOf(page);
    check(expected ? target && target->id == *expected : !target, "after clearing the real pointer position applies again");
}

// ---- semantic layer --------------------------------------------------------------------------

void theSemanticLayerQueriesAndSimulatesHoverLikeTheTypedEntry() {
    Page page; page.arrange();
    auto& tree = page.ui.semantics();
    check(tree.hover() && !*tree.hover(), "nothing hovered");
    check(bool(tree.perform({"save"}, Verb::hover)), "semantic simulation accepted");
    check(tree.hover() && *tree.hover() == NodeRef{"save"} && targetOf(page) == on("save"), "the semantic query equals the typed one");
    check(bool(tree.perform({"list", OptionValue{"l2"}}, Verb::hover)), "an item is named by id and value");
    check(tree.hover() && *tree.hover() == (NodeRef{"list", OptionValue{"l2"}}) && targetOf(page) == on("list", "l2"), "the item is reported by both");
    check(bool(tree.clearSimulatedHover()) && !targetOf(page) && !*tree.hover(), "the semantic layer clears the simulation");
    // Same admission and Status words as the typed entry, target by target.
    struct Case { std::string id; std::optional<std::string> item; ComponentHandle handle; };
    std::vector<Case> cases{
        {"save", std::nullopt, page.save}, {"off", std::nullopt, page.off}, {"list", "l3", page.list}, {"list", "l2", page.list},
        {"list", "nope", page.list}, {"save", "x", page.save}, {"tabs", "t3", page.tabs}, {"nav", "n2", page.nav}};
    for (const auto& c : cases) {
        std::optional<OptionValue> item = c.item ? std::optional<OptionValue>(OptionValue{*c.item}) : std::nullopt;
        auto typed = page.ui.simulateHover(c.handle, item).status;
        auto semantic = tree.perform({c.id, item}, Verb::hover).status;
        check(typed == semantic, ("typed and semantic statuses agree for " + c.id + (c.item ? "/" + *c.item : "")).c_str());
        page.ui.clearSimulatedHover();
    }
    check(tree.perform({"missing"}, Verb::hover).status == Status::notFound, "an unknown id is not_found");
    check(bool(page.dialog.open()), "dialog opens");
    check(page.ui.simulateHover(page.save).status == Status::blockedByModal &&
          tree.perform({"save"}, Verb::hover).status == Status::blockedByModal, "both entries report blocked_by_modal behind a dialog");
    check(tree.perform({"ok"}, Verb::hover).status == Status::ok, "and accept a component inside it");
    check(tree.perform({"dlg"}, Verb::hover).status != Status::ok, "the dialog node itself is not hoverable");
}

void hoverIsInvisibleToTheSemanticTreeAndItsEvents() {
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
    tree.dispatchEvents();
    events = 0;
    move(page, frame, middleOf(frame, "save")); move(page, frame, itemMiddle(frame, "list", 1)); move(page, frame, middleOf(frame, "slider"));
    tree.perform({"box"}, Verb::hover); tree.perform({"nav", OptionValue{"n2"}}, Verb::hover);
    tree.dispatchEvents();
    check(events == 0, "hover changes raise no semantic event");
    check(snapshot() == before, "no node property or verb list changes with hover");
}

// ---- palette -------------------------------------------------------------------------------

// Literal proposal values of the ticket: Material overlays the text color at 8% (20/255);
// Minimal Web overlays 6% black on light (15/255) and 8% white on dark (20/255).
void hoverOverlayValuesAreTheProposedOnes() {
    auto materialLight = materialAppearance().palette(ThemeVariant::light());
    auto materialDark = materialAppearance().palette(ThemeVariant::dark());
    auto webLight = webAppearance().palette(ThemeVariant::light());
    auto webDark = webAppearance().palette(ThemeVariant::dark());
    // 8% would leave the muted field label at 4.22:1 on the hovered surface; 10/255 (4%) keeps 4.56:1.
    check(materialLight.hover == materialLight.text && materialLight.hoverAlpha == 10, "material light: text color at 4% (the contrast rule caps it below 8%)");
    check(materialDark.hover == materialDark.text && materialDark.hoverAlpha == 20, "material dark: text color at 8%");
    check(webLight.hover == Color{0, 0, 0} && webLight.hoverAlpha == 15, "web light: 6% black");
    check(webDark.hover == Color{255, 255, 255} && webDark.hoverAlpha == 20, "web dark: 8% white");
}

void hoverRolesAreNamedPaletteRoles() {
    auto roles = paletteRoles();
    check(std::find(roles.begin(), roles.end(), "hover") != roles.end(), "hover is a color role");
    check(std::find(roles.begin(), roles.end(), "hover_alpha") != roles.end(), "hover_alpha is an int role");
    Palette p = webAppearance().palette();
    check(setPaletteValue(p, "hover", Color{9, 8, 7}) && p.hover == Color{9, 8, 7}, "hover color is settable");
    check(setPaletteValue(p, "hover_alpha", 31) && p.hoverAlpha == 31, "hover_alpha is settable");
    check(!setPaletteValue(p, "hover_alpha", 256) && !setPaletteValue(p, "hover_alpha", -1), "hover_alpha is 0-255");
    check(!setPaletteValue(p, "hover_alpha", Color{1, 2, 3}) && !setPaletteValue(p, "hover", 5), "hover roles check the value kind");
}

void hoverRolesCanBeOverriddenThroughTheUi() {
    Ui ui;
    auto result = ui.setPaletteOverrides({{nameOf(webAppearance()), ThemeVariant::light(), "hover", Color{10, 20, 30}},
                                            {nameOf(webAppearance()), ThemeVariant::light(), "hover_alpha", 20}});
    check(bool(result), "hover overrides are accepted");
    auto effective = ui.effectivePalette(webAppearance(), ThemeVariant::light());
    check(effective && effective->hover == Color{10, 20, 30} && effective->hoverAlpha == 20, "override reaches the effective palette");
    check(ui.effectivePalette(webAppearance(), ThemeVariant::dark())->hover == Color{255, 255, 255}, "other variants keep their value");
    // #86: an overlay strong enough to take the outline under 3:1 on the hovered surface is refused, naming the pair.
    auto strong = ui.setPaletteOverrides({{nameOf(webAppearance()), ThemeVariant::light(), "hover_alpha", 90}});
    check(strong.status == Status::invalidValue && std::u16string_view(strong.message).find(u"outline/hovered") != std::u16string_view::npos, "a hover overlay that drowns the outline is rejected");
}

// ---- contrast ------------------------------------------------------------------------------

Color blended(Color over, unsigned char alpha, Color under) {
    auto mix = [&](unsigned char o, unsigned char u) { return static_cast<unsigned char>((o * alpha + u * (255 - alpha)) / 255); };
    return {mix(over.r, under.r), mix(over.g, under.g), mix(over.b, under.b)};
}
// WCAG 1.4.11 understanding: a hover effect itself needs no 3:1, but it must not make the component lose
// sufficient contrast with its neighbors. Text keeps 4.5:1, the accent and the outline 3:1 on every hovered surface.
void hoverKeepsTextAndAccentReadableOnEveryHoveredSurface() {
    std::string failures;
    for (const auto* look : builtins) for (const auto& variant : variants) {
        auto p = look->palette(variant);
        struct Surface { const char* name; Color color; };
        for (const auto& surface : {Surface{"background", p.background}, Surface{"panel", p.panel}, Surface{"surface", p.surface}, Surface{"selection", p.selection}}) {
            auto hovered = blended(p.hover, p.hoverAlpha, surface.color);
            auto context = labelOf(*look, variant) + " " + surface.name;
            auto expect = [&](const char* what, Color foreground, double required) {
                double ratio = contrastRatio(foreground, hovered);
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
void theBlendHelperMatchesAWorkedExample() {
    auto c = blended({0, 0, 0}, 15, {200, 100, 50});
    check(c == (Color{188, 94, 47}), "6% black over (200,100,50) is (188,94,47): (0*15 + 200*240)/255 = 188 etc.");
}

// ---- presentation switch -------------------------------------------------------------------

void presentationHasAHoverSwitchOnByDefault() {
    Ui ui;
    check(ui.presentation().hover == Hover::on, "hover is on by default");
    auto settings = ui.presentation();
    settings.hover = Hover::off;
    check(bool(ui.setPresentation(settings)) && ui.presentation().hover == Hover::off, "hover can be switched off at run time");
    check(!(settings == PresentationSettings{}), "the switch takes part in operator==");
    int repaints = 0; detail::access(ui).onInvalidate([&] { ++repaints; });
    settings.hover = Hover::on;
    check(bool(ui.setPresentation(settings)) && repaints == 1, "switching back on repaints");
}
}
int main() {
    static const test::Case cases[] = {
        {"hover_overlay_values_are_the_proposed_ones", hoverOverlayValuesAreTheProposedOnes},
        {"hover_roles_are_named_palette_roles", hoverRolesAreNamedPaletteRoles},
        {"hover_roles_can_be_overridden_through_the_ui", hoverRolesCanBeOverriddenThroughTheUi},
        {"presentation_has_a_hover_switch_on_by_default", presentationHasAHoverSwitchOnByDefault},
        {"the_blend_helper_matches_a_worked_example", theBlendHelperMatchesAWorkedExample},
        {"hover_keeps_text_and_accent_readable_on_every_hovered_surface", hoverKeepsTextAndAccentReadableOnEveryHoveredSurface},
        {"the_pointer_hovers_components_and_single_items", thePointerHoversComponentsAndSingleItems},
        {"disabled_components_and_items_are_never_the_target", disabledComponentsAndItemsAreNeverTheTarget},
        {"components_that_ignore_the_pointer_are_never_the_hover_target", componentsThatIgnoreThePointerAreNeverTheHoverTarget},
        {"touch_messages_neither_make_nor_change_the_hover", touchMessagesNeitherMakeNorChangeTheHover},
        {"leaving_the_window_or_losing_activation_clears_the_hover", leavingTheWindowOrLosingActivationClearsTheHover},
        {"opening_a_dialog_clears_the_hover_and_blocks_the_background", openingADialogClearsTheHoverAndBlocksTheBackground},
        {"dragging_a_slider_keeps_the_hover_on_it", draggingASliderKeepsTheHoverOnIt},
        {"captured_hover_holds_until_released", capturedHoverHoldsUntilReleased},
        {"simulated_hover_has_the_effect_of_the_pointer", simulatedHoverHasTheEffectOfThePointer},
        {"simulation_is_admitted_like_the_typed_operations", simulationIsAdmittedLikeTheTypedOperations},
        {"a_simulated_hover_waits_while_a_drag_holds_the_hover", aSimulatedHoverWaitsWhileADragHoldsTheHover},
        {"hover_commands_follow_the_target_everywhere", hoverCommandsFollowTheTargetEverywhere},
        {"context_menu_rows_hover_as_their_highlight", contextMenuRowsHoverAsTheirHighlight},
        {"hovering_the_slider_track_far_from_the_thumb_still_marks_the_thumb", hoveringTheSliderTrackFarFromTheThumbStillMarksTheThumb},
        {"a_hovered_selected_item_keeps_its_selection_and_gains_the_overlay", aHoveredSelectedItemKeepsItsSelectionAndGainsTheOverlay},
        {"the_hover_switch_hides_the_overlay_but_not_the_target_or_the_tooltip", theHoverSwitchHidesTheOverlayButNotTheTargetOrTheTooltip},
        {"hover_overlay_blends_over_the_surface_with_the_palette_alpha", hoverOverlayBlendsOverTheSurfaceWithThePaletteAlpha},
        {"the_default_overlay_path_keeps_the_alpha_visible", theDefaultOverlayPathKeepsTheAlphaVisible},
        {"hover_never_paints_for_a_zero_alpha_palette", hoverNeverPaintsForAZeroAlphaPalette},
        {"scrolling_under_a_resting_pointer_moves_the_hover_at_once", scrollingUnderARestingPointerMovesTheHoverAtOnce},
        {"reflow_under_a_resting_pointer_moves_the_hover_at_once", reflowUnderARestingPointerMovesTheHoverAtOnce},
        {"a_frame_does_not_move_a_simulated_hover", aFrameDoesNotMoveASimulatedHover},
        {"the_semantic_layer_queries_and_simulates_hover_like_the_typed_entry", theSemanticLayerQueriesAndSimulatesHoverLikeTheTypedEntry},
        {"hover_is_invisible_to_the_semantic_tree_and_its_events", hoverIsInvisibleToTheSemanticTreeAndItsEvents},
    };
    return test::run("hover", cases);
}
