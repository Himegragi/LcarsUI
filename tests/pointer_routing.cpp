// Pointer routing that the Windows and Linux desktop hosts share (src/pointer_routing.hpp): context-menu
// pointer actions, opening a menu at a point and scrollbar thumb presses and drags. Expected values are
// worked out by hand from the fixtures' geometry.
#include "pointer_routing.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"

using namespace isoduo;
using test::check;

namespace {
using detail::PointerAction;

detail::UiState& core(Ui& ui) { return detail::access(ui); }
TextFieldHandle addField(Ui& ui, ComponentConfig config) {
    auto model = std::make_shared<std::u16string>(config.value);
    auto handle = ui.addTextField(std::move(config)).value.value();
    check(bool(handle.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "connect field source");
    check(bool(handle.connectImmediate([model](const Change<TextFieldHandle>& change) { *model = change.value; return Result{}; })), "connect field write");
    return handle;
}
Frame layoutOf(Ui& ui, std::vector<layout::Node> nodes, ScrollState& scroll) {
    test::FakeMeasure measure;
    return arrange(layout::column(std::move(nodes), 8), ui, webAppearance(), measure, 600, 400, 1.f, scroll);
}
std::string highlighted(Ui& ui) {
    auto menu = ui.contextMenu();
    if (!menu) return "<closed>";
    for (const auto& item : menu->items) if (item.highlighted) return item.id;
    return "";
}
Rect rowOf(const Frame& frame, std::string_view id) {
    for (const auto& row : frame.menuPanel()->menuRows) if (row.id == id) return row.bounds;
    return {};
}

void menuTakesEveryPointerActionWhileOpen() {
    Ui ui;
    ScrollState scroll;
    auto field = addField(ui, {.id = "note", .label = u"欄位", .value = u"hello world"});
    auto nodes = [&] { return std::vector<layout::Node>{layout::component(field)}; };
    check(bool(field.focus()) && bool(ui.openContextMenu(field, MenuPosition{100, 60})), "open the text menu at a point");
    auto frame = layoutOf(ui, nodes(), scroll);
    const auto all = rowOf(frame, "isoduo.select_all");
    const auto undo = rowOf(frame, "isoduo.undo");
    check(all.h > 0 && undo.h > 0, "the menu has its rows");
    auto& state = core(ui);
    check(!detail::routeMenuPointer(state, frame, PointerAction::move, all.x + 2, all.y + 2) && highlighted(ui) == "isoduo.select_all",
          "moving over a row highlights it");
    check(!detail::routeMenuPointer(state, frame, PointerAction::press, all.x + 2, all.y + 2) && *ui.contextMenuOpen(),
          "a press on a row keeps the menu open");
    check(!detail::routeMenuPointer(state, frame, PointerAction::release, undo.x + 2, undo.y + 2) && *ui.contextMenuOpen() &&
          core(ui).edit("note")->selection.anchor == core(ui).edit("note")->selection.caret,
          "releasing over another row runs nothing");
    check(!detail::routeMenuPointer(state, frame, PointerAction::press, all.x + 2, all.y + 2) &&
          !detail::routeMenuPointer(state, frame, PointerAction::release, all.x + 2, all.y + 2) && !*ui.contextMenuOpen() &&
          core(ui).edit("note")->selection.anchor == 0 && core(ui).edit("note")->selection.caret == 11,
          "a press and release on one row runs it");
    check(bool(field.select(0, 0)) && bool(ui.openContextMenu(field, MenuPosition{100, 60})), "open again");
    frame = layoutOf(ui, nodes(), scroll);
    check(!detail::routeMenuPointer(state, frame, PointerAction::rightPress, all.x + 2, all.y + 2) && *ui.contextMenuOpen(),
          "a right press inside the menu keeps it open");
    check(!detail::routeMenuPointer(state, frame, PointerAction::rightRelease, all.x + 2, all.y + 2) && !*ui.contextMenuOpen() &&
          core(ui).edit("note")->selection.caret == 11, "a right release on a row runs it at once");
    for (auto action : {PointerAction::press, PointerAction::rightPress}) {
        check(bool(field.select(0, 0)) && bool(ui.openContextMenu(field, MenuPosition{100, 60})), "open again");
        frame = layoutOf(ui, nodes(), scroll);
        check(detail::routeMenuPointer(state, frame, action, 99, 59) && !*ui.contextMenuOpen() &&
              core(ui).edit("note")->selection.caret == 0, "a press outside only closes the menu and asks the host to swallow its release");
    }
}

// A right click on the third line of a multi-line field moves the caret to that line. (Windows used to measure
// the click on the first line: the caret jumped to the same column of line one.)
void rightClickOpensTheMenuAtTheLineUnderThePointer() {
    Ui ui;
    ScrollState scroll;
    auto field = addField(ui, {.id = "lines", .label = u"欄位", .value = u"ab\ncd\nef", .multiline = true, .visibleLines = 3});
    auto frame = layoutOf(ui, {layout::component(field)}, scroll);
    const auto* placed = frame.find(field);
    check(placed && placed->view.rowHeight > 0, "the field is placed with its rows");
    const int x = placed->view.text.x;
    const int y = placed->view.text.y + placed->view.rowHeight * 2 + placed->view.rowHeight / 2;
    const auto target = detail::pointerMenuTarget(core(ui), frame, x, y);
    check(target && target->id == "lines" && target->kind == ComponentKind::textField, "the field under the pointer is the target");
    check(bool(detail::openPointerMenu(core(ui), frame, *target, x, y)) && *ui.contextMenuOpen(), "its menu opens");
    check(core(ui).edit("lines")->selection.caret == 6, "the caret moves to the start of the third line");
    check(!detail::pointerMenuTarget(core(ui), frame, 599, 399), "empty space opens nothing");
}

// A page with one scroll region: the track runs down the right edge, the thumb covers its top third.
struct ThumbFixture {
    Ui ui;
    ScrollState page;
    Frame frame;
    ThumbFixture() {
        frame.ownerState = &core(ui);
        ScrollRegion region;
        region.state = &page;
        region.viewport = region.clip = {0, 0, 400, 300};
        region.track = {390, 0, 10, 300};
        region.thumb = {390, 0, 10, 100};
        region.extent = 900;
        region.maximum = 600;
        frame.regions.push_back(region);
    }
};

void thumbFollowsThePointer() {
    ThumbFixture f;
    auto grab = detail::pressThumb(f.ui, f.frame, nullptr, 395, 50);
    check(grab && grab->state == &f.page && grab->grabOffset == 50 && f.page.offset == 0, "a press on the thumb grabs it where it is");
    // Thumb top 150 - 50 = 100 of a 200 pixel travel: half of 600.
    check(detail::dragThumb(f.ui, f.frame, *grab, 395, 150) && f.page.offset == 300, "dragging moves the thumb with the pointer");
    f.page.offset = 0;
    grab = detail::pressThumb(f.ui, f.frame, nullptr, 395, 250);
    // A track press centers the thumb on the pointer: top 250 - 50 = 200, the end of the travel.
    check(grab && grab->grabOffset == 50 && f.page.offset == 600, "a press on the track centers the thumb on the pointer");
    check(!detail::pressThumb(f.ui, f.frame, nullptr, 200, 100), "a press away from the track grabs nothing");
}

// An open dialog blocks the page's scrollbars. Before the hosts shared this rule, Windows let a press on a
// dropdown inside the dialog reach a blocked page scrollbar underneath: Frame::scrollTo refused to move it,
// but the press started a thumb drag and never reached the dropdown. Linux sent the press to the dropdown.
void anOpenDialogBlocksThePageScrollbarUnderItsComponents() {
    ThumbFixture f;
    f.frame.modal = true;
    Placement onDialog;
    onDialog.id = "choice";
    onDialog.kind = ComponentKind::dropdown;
    onDialog.popup = onDialog.dialog = true;
    onDialog.bounds = onDialog.clip = {300, 100, 100, 30};
    check(!detail::pressThumb(f.ui, f.frame, &onDialog, 395, 110) && f.page.offset == 0,
          "a press on a dialog's dropdown over the page scrollbar grabs nothing, so it reaches the dropdown");
    check(!detail::pressThumb(f.ui, f.frame, nullptr, 395, 110), "a press on the dialog's panel cannot reach the page scrollbar");
    ScrollState dialogList;
    ScrollRegion own = f.frame.regions.front();
    own.state = &dialogList;
    own.modalContent = true;
    own.viewport = own.clip = {300, 100, 100, 100};
    own.track = {390, 100, 10, 100};
    own.thumb = {390, 100, 10, 50};
    own.maximum = 100;
    f.frame.regions.push_back(own);
    auto grab = detail::pressThumb(f.ui, f.frame, nullptr, 395, 120);
    check(grab && grab->state == &dialogList, "the dialog's own scrollbar still takes the press");
}

// An open dropdown panel lies above everything: it takes the press for its own scrollbar and for no other.
void anOpenDropdownPanelTakesThePressOnlyForItsOwnScrollbar() {
    ThumbFixture f;
    auto dropdown = f.ui.addDropdown({.id = "choice", .label = u"選擇"}, {}).value.value();
    Placement panel;
    panel.id = "choice";
    panel.kind = ComponentKind::dropdown;
    panel.popup = panel.dropdownPanel = true;
    panel.component = dropdown;
    panel.bounds = panel.clip = {300, 50, 100, 100};
    check(!detail::pressThumb(f.ui, f.frame, &panel, 395, 60), "the page scrollbar under an open panel grabs nothing");
    ScrollState list;
    ScrollRegion own = f.frame.regions.front();
    own.state = &list;
    own.component = dropdown;
    own.viewport = own.clip = panel.bounds;
    own.track = {390, 50, 10, 100};
    own.thumb = {390, 50, 10, 50};
    own.maximum = 100;
    f.frame.regions.push_back(own);
    f.frame.modal = true;
    auto grab = detail::pressThumb(f.ui, f.frame, &panel, 395, 60);
    check(grab && grab->state == &list, "the panel's own scrollbar takes the press, even above an open dialog");
}
}

int main() {
    const test::Case cases[] = {
        {"the open menu takes every pointer action", menuTakesEveryPointerActionWhileOpen},
        {"a right click opens the menu at the line under the pointer", rightClickOpensTheMenuAtTheLineUnderThePointer},
        {"a held thumb follows the pointer", thumbFollowsThePointer},
        {"an open dialog blocks the page scrollbar under its components", anOpenDialogBlocksThePageScrollbarUnderItsComponents},
        {"an open dropdown panel takes the press only for its own scrollbar", anOpenDropdownPanelTakesThePressOnlyForItsOwnScrollbar},
    };
    return test::run("pointer routing", cases);
}
