#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <type_traits>

using namespace lcars;
using test::check;
namespace {
const std::vector<OptionItem> choices{{{"a"}, u"Alpha"}, {{"b"}, u"Beta"}};
ComponentHandle make(Ui& ui, int kind, std::string id = "removed") {
    ComponentConfig c{.id = std::move(id), .label = u"Fallback"};
    switch (kind) {
    case 0: return ui.addLabel(c).value.value();
    case 1: return ui.addTextField(c).value.value();
    case 2: return ui.addCheckbox(c).value.value();
    case 3: return ui.addRadioGroup(c, choices).value.value();
    case 4: return ui.addSlider(c, {0, 100, 5}).value.value();
    case 5: return ui.addProgress(c).value.value();
    case 6: return ui.addDropdown(c, choices).value.value();
    case 7: return ui.addList(c, choices).value.value();
    default: return ui.addButton(c).value.value();
    }
}
Result remove(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Result {
        if constexpr (requires { h.remove(); }) return h.remove();
        else return {Status::notSupported, {}};
    }, handle);
}
Query<ComponentSnapshot> describe(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Query<ComponentSnapshot> {
        if constexpr (requires { h.describe(); }) return h.describe();
        else return {Status::notSupported, {}, {}};
    }, handle);
}
using Subscribe = std::function<std::function<void()>(std::function<void()>)>;
Result connect(const ComponentHandle& handle, bool& changed, int& reads, Subscribe subscribe) {
    return std::visit([&](const auto& h) -> Result {
        using H = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<H, LabelHandle> || std::is_same_v<H, TextFieldHandle>)
            return h.connectSource(Source<std::u16string>{[&] { ++reads; return std::u16string(changed ? u"Changed" : u"Initial"); }, subscribe});
        else if constexpr (std::is_same_v<H, CheckboxHandle>)
            return h.connectSource(Source<bool>{[&] { ++reads; return changed; }, subscribe});
        else if constexpr (std::is_same_v<H, SliderHandle> || std::is_same_v<H, ProgressHandle>)
            return h.connectSource(Source<int>{[&] { ++reads; return changed ? 80 : 20; }, subscribe});
        else if constexpr (std::is_same_v<H, RadioGroupHandle> || std::is_same_v<H, DropdownHandle> || std::is_same_v<H, ListHandle>)
            return h.connectSource(Source<std::optional<OptionValue>>{[&] { ++reads; return std::optional<OptionValue>{{changed ? "b" : "a"}}; }, subscribe});
        else return {Status::notSupported, {}};
    }, handle);
}
void allEightKindsCanBeRemoved() {
    for (int kind = 0; kind < 9; ++kind) {
        std::cout << "kind " << kind << '\n';
        bool changed = false;
        int reads = 0, cancellations = 0, invalidations = 0;
        std::function<void()> notification;
        Ui ui;
        auto handle = make(ui, kind);
        const bool readsSource = std::visit([](const auto& h) {
            return requires { h.connectSource(Source<std::u16string>{}); } || requires { h.connectSource(Source<bool>{}); } ||
                   requires { h.connectSource(Source<int>{}); } || requires { h.connectSource(Source<std::optional<OptionValue>>{}); };
        }, handle);
        const auto connected = connect(handle, changed, reads, [&](auto callback) {
            notification = std::move(callback);
            return [&] { ++cancellations; notification(); };
        });
        check(!readsSource || bool(connected), "Every source-bearing kind connects");
        auto savedNotification = notification;
        if (savedNotification) {
            auto initial = describe(handle);
            changed = true;
            savedNotification();
            check(*describe(handle) != *initial, "A live source notification changes the displayed component snapshot");
            changed = false;
            savedNotification();
        }
        auto first = ui.addTextField({.id = "first", .label = u"First"}).value.value();
        auto last = ui.addTextField({.id = "last", .label = u"Last"}).value.value();
        test::FakeMeasure measure;
        auto tree = [&](int mode, bool includeRemoved) {
            std::vector<layout::Node> children{layout::component(first)};
            if (includeRemoved) children.push_back(layout::component(handle));
            children.push_back(layout::component(last));
            auto content = mode == 0 ? layout::column(std::move(children), 13) :
                           mode == 1 ? layout::row(std::move(children), 13) : layout::grid(std::move(children), 120, 13);
            return layout::scroll(std::move(content));
        };
        auto frame = [&](int mode, bool includeRemoved) {
            ScrollState scroll;
            return arrange(tree(mode, includeRemoved), ui, webAppearance(), measure, 300, 900, 1, scroll);
        };
        check(frame(0, true).items.size() == 3, "The original page includes the component");
        const bool focused = describe(handle)->focusable;
        if (focused) check(bool(ui.semantics().perform({"removed"}, Verb::focus)), "A focusable component receives focus");
        int blurs = 0, focusMoves = 0;
        std::visit([&](const auto& h) {
            if constexpr (requires { h.onBlur([] {}); }) h.onBlur([&] { ++blurs; });
        }, handle);
        detail::access(ui).onFocus([&](const auto& from, const auto& to) { if (from == "removed" && to.empty()) ++focusMoves; });
        std::vector<SemanticEvent> events;
        auto subscription = ui.semantics().subscribe([&](const auto& e) { events.push_back(e); });
        check(bool(describe(handle)), "The component starts alive");
        auto trigger = ui.addButton({.id = "trigger", .label = u"Remove"}).value.value();
        auto groupButton = ui.addButton({.id = "group", .label = u"Group"}).value.value();
        trigger.bind([&] {
            check(bool(remove(handle)), "Owner removal is accepted inside a callback");
            check(cancellations == 0, "Physical teardown waits for the safe point");
            check(describe(handle).status == Status::notFound && ui.semantics().get({"removed"}).status == Status::notFound,
                  "Both interfaces refuse the pending component");
            std::visit([&](const auto& h) {
                if constexpr (std::is_constructible_v<ConfirmField, decltype(h)>)
                    check(ui.connectConfirmation(groupButton, {h}, [](const auto&) { return ConfirmationResult{}; }).status == Status::invalidValue,
                          "A pending field cannot enter a new confirmation group");
            }, handle);
            const int beforeReads = reads;
            invalidations = 0;
            detail::access(ui).onInvalidate([&] { ++invalidations; });
            changed = true;
            if (savedNotification) savedNotification();
            check(reads == beforeReads && invalidations == 0, "Pending source notifications neither read nor repaint");
            detail::access(ui).onInvalidate({});
            for (int mode = 0; mode < 3; ++mode) {
                auto pending = frame(mode, true), compact = frame(mode, false);
                check(pending.items.size() == 2 && pending.extent == compact.extent &&
                      pending.find(last)->bounds == compact.find(last)->bounds, "Pending removal also collapses during reentrant layout");
            }
            return Result{};
        });
        check(bool(trigger.invoke()), "The callback returns safely");
        check(blurs == 0 && focusMoves == (focused ? 1 : 0), "Removal skips field blur callbacks and keeps the host focus notification");
        check(cancellations == (readsSource ? 1 : 0), "Removal cancels the source once when present");
        check(describe(handle).status == Status::notFound, "A removed handle is invalid");
        check(remove(handle).status == Status::notFound, "Removal cannot be repeated");
        ui.semantics().dispatchEvents();
        check(std::any_of(events.begin(), events.end(), [](const auto& e) { return e.kind == SemanticEventKind::structureChanged; }),
              "The semantic tree announces the removed structure");
        for (int mode = 0; mode < 3; ++mode) {
            std::cout << "layout " << mode << '\n';
            auto actual = frame(mode, true), compact = frame(mode, false);
            check(actual.items.size() == 2 && actual.extent == compact.extent, "Removal leaves no height or vertical gap");
            check(actual.find(first)->bounds == compact.find(first)->bounds && actual.find(last)->bounds == compact.find(last)->bounds,
                  "Column, row and grid match a page assembled without the removed component");
        }
        auto replacement = make(ui, kind);
        check(handle != replacement && describe(handle).status == Status::notFound, "Reusing an id never revives the old handle");
        const auto replacementBefore = describe(replacement);
        invalidations = 0;
        detail::access(ui).onInvalidate([&] { ++invalidations; });
        if (savedNotification) savedNotification();
        check(invalidations == 0 && *describe(replacement) == *replacementBefore, "A late callback cannot reach the replacement");
        detail::access(ui).onInvalidate({});
        check(bool(first.remove()) && bool(last.remove()), "The surviving layout entries can also be removed");
        for (int mode = 0; mode < 3; ++mode) {
            auto empty = frame(mode, true);
            check(empty.items.empty() && empty.extent == 0, "A layout with every original instance removed has no residual gaps");
        }
    }
}
void confirmationLosesRemovedDraftsAndDissolves() {
    Ui ui;
    auto first = ui.addTextField({.id = "first", .label = u"First"}).value.value();
    auto second = ui.addCheckbox({.id = "second", .label = u"Second"}).value.value();
    auto apply = ui.addButton({.id = "apply", .label = u"Apply"}).value.value();
    auto subscribe = [](auto) { return [] {}; };
    first.connectSource({[] { return std::u16string(u"Initial"); }, subscribe});
    second.connectSource({[] { return false; }, subscribe});
    int batches = 0;
    auto capture = std::make_shared<int>(0);
    std::weak_ptr<int> captured = capture;
    check(bool(ui.connectConfirmation(apply, {first, second}, [&, capture](const auto& changes) {
        ++batches;
        check(changes.size() == 1, "A removed draft is omitted from the batch");
        const auto& change = std::get<Change<CheckboxHandle>>(changes[0]);
        check(change.field == second && change.value && !change.origin, "Only the surviving field's draft and origin are written");
        return ConfirmationResult{};
    })), "The mixed confirmation group connects");
    capture.reset();
    check(bool(first.setValue(u"Discarded")) && bool(second.toggle()), "Both fields have drafts");
    check(bool(first.remove()) && bool(apply.invoke()) && batches == 1, "Partial removal leaves a usable confirmation");
    check(bool(second.remove()) && captured.expired(), "The last removal releases the group's write callback");
    check(apply.describe()->enabled, "Dissolving the group preserves the button's enabled state");
    check(apply.invoke().status == Status::notSupported && batches == 1, "The ordinary button has no action");
    auto replacement = ui.addCheckbox({.id = "second", .label = u"Replacement"}).value.value();
    replacement.connectSource({[] { return false; }, subscribe});
    check(bool(ui.connectConfirmation(apply, {replacement}, [](const auto&) { return ConfirmationResult{}; })),
          "The former button can connect a new group with a new instance");
    apply.enable(false);
    check(bool(replacement.remove()) && !apply.describe()->enabled, "Dissolving a new group preserves a disabled button too");
    apply.enable(true);
    check(bool(apply.bind([&] { ++batches; return Result{}; })) && bool(apply.invoke()) && batches == 2, "The former confirmation button can be rebound");
}
void removalDiscardsCompositionAndSkipsBlur() {
    for (bool rich : {false, true}) {
        Ui ui;
        auto field = ui.addTextField({.id = "field", .label = u"Field", .multiline = rich, .rich = rich}).value.value();
        int writes = 0, blurs = 0, beforeFocus = 0, moved = 0;
        auto subscribe = [](auto) { return [] {}; };
        if (rich) {
            field.connectRichSource({[] { return RichText{u"Initial", {}}; }, subscribe});
            field.connectRichImmediate([&](const auto&) { ++writes; return Result{}; });
        } else {
            field.connectSource({[] { return std::u16string(u"Initial"); }, subscribe});
            field.connectImmediate([&](const auto&) { ++writes; return Result{}; });
        }
        field.onBlur([&] { ++blurs; });
        auto& host = detail::access(ui);
        field.focus();
        host.beforeFocus([&] { ++beforeFocus; });
        host.onFocus([&](const auto& from, const auto& to) { if (from == "field" && to.empty()) ++moved; });
        check(bool(host.compositionUpdate(u"Uncommitted", 11)), "IME preedit starts through the existing host seam");
        check(bool(field.remove()), "A composing field can be removed");
        check(writes == 0 && blurs == 0 && beforeFocus == 0 && moved == 1,
              "Removal discards composition and blur validation, but reports host focus movement");
        check(host.compositionCommit(u"Late").status != Status::ok && writes == 0, "A late IME result lands nowhere");
    }
}
void nameLabelsFallBackAndOrphansReassociate() {
    Ui ui;
    auto field = ui.addTextField({.id = "field", .label = u"Fallback"}).value.value();
    auto label = ui.addLabel({.id = "name", .value = u"Name", .target = "field", .labelKind = LabelKind::name}).value.value();
    std::vector<SemanticEvent> events;
    auto subscription = ui.semantics().subscribe([&](const auto& e) { events.push_back(e); });
    check(field.describe()->name == u"Name", "The name label supplies the accessible name");
    auto trigger = ui.addButton({.id = "trigger", .label = u"Remove"}).value.value();
    std::u16string labelText = u"Name";
    label.connectSource([&] { return labelText; });
    trigger.bind([&] {
        check(bool(label.remove()), "Name label removal is accepted");
        labelText = u"Must not appear";
        check(field.describe()->name == u"Fallback", "Pending name labels no longer supply text to their target");
        return Result{};
    });
    check(bool(trigger.invoke()) && field.describe()->name == u"Fallback", "Removing the name label restores the fallback");
    ui.semantics().dispatchEvents();
    check(std::count_if(events.begin(), events.end(), [](const auto& e) {
        return e.kind == SemanticEventKind::propertyChanged && e.ref.id == "field" && e.property == "name";
    }) == 1, "The accessible name changes exactly once");
    label = ui.addLabel({.id = "name", .value = u"Name", .target = "field", .labelKind = LabelKind::name}).value.value();
    check(bool(field.remove()) && label.describe()->value == u"Name", "Removing the target leaves its label alive");
    test::FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(layout::column({layout::component(label)}), ui, webAppearance(), measure, 300, 300, 1, scroll);
    check(frame.find(label) && label.describe()->visible, "An orphan label remains on screen");
    auto replacement = ui.addTextField({.id = "field", .label = u"New fallback"}).value.value();
    check(replacement.describe()->name == u"Name" && ui.semantics().get({"field"})->labelledBy == std::vector<NodeRef>{{"name"}},
          "The label automatically names a replacement with the same component id");
}
void dialogReferencesCannotBeRemoved() {
    for (int kind = 0; kind < 9; ++kind) {
        Ui ui;
        auto member = make(ui, kind);
        auto initial = ui.addTextField({.id = "initial", .label = u"Initial"}).value.value();
        auto fallback = ui.addButton({.id = "fallback", .label = u"Fallback"}).value.value();
        auto name = ui.addLabel({.id = "name", .value = u"Dialog"}).value.value();
        auto dialog = ui.addDialog("dialog", {member, initial, name}, initial, fallback, [](bool) {}, name);
        check(bool(dialog), "The dialog is assembled");
        check(remove(member).status == Status::invalidValue && initial.remove().status == Status::invalidValue &&
              fallback.remove().status == Status::invalidValue && name.remove().status == Status::invalidValue,
              "Members, both focus targets and the name label stay alive even while the dialog is closed");
        check(bool(dialog.value->open()) && remove(member).status == Status::invalidValue, "Open dialog membership is protected too");
    }
}
void retainedFramesRejectRemovedScrollRegions() {
    enum class View { textField, verticalLabel, dropdown, list, scrollingLabel, contextMenu };
    enum class Removal { pending, committed, reusedId };
    for (auto view : {View::textField, View::verticalLabel, View::dropdown, View::list, View::scrollingLabel, View::contextMenu}) {
        for (auto removal : {Removal::pending, Removal::committed, Removal::reusedId}) {
            Ui ui;
            test::FakeMeasure measure;
            ScrollState pageScroll, componentScroll;
            auto create = [&]() -> ComponentHandle {
                if (view == View::textField)
                    return *ui.addTextField({.id = "scrolling", .value = u"one\ntwo\nthree\nfour\nfive\nsix", .multiline = true, .visibleLines = 2});
                if (view == View::verticalLabel)
                    return *ui.addLabel({.id = "scrolling", .value = std::u16string(100, u'\u4e00'), .writingMode = WritingMode::vertical});
                if (view == View::scrollingLabel)
                    return *ui.addLabel({.id = "scrolling", .value = std::u16string(500, u'x')});
                if (view == View::contextMenu) {
                    auto button = *ui.addButton({.id = "scrolling", .label = u"Menu target"});
                    std::vector<MenuItem> items;
                    for (int i = 0; i < 30; ++i) items.push_back({.id = "app.item" + std::to_string(i), .label = u"Command"});
                    check(bool(ui.addContextMenu(button, items, MenuMode::replace)) &&
                          bool(ui.openContextMenu(button, MenuPosition{0, 0})), "The scrolling context menu opens");
                    return button;
                }
                std::vector<OptionItem> items;
                for (int i = 0; i < 30; ++i) items.push_back({{std::to_string(i)}, u"Option"});
                if (view == View::list) return *ui.addList({.id = "scrolling"}, items);
                auto dropdown = *ui.addDropdown({.id = "scrolling", .label = u"Dropdown"}, items);
                dropdown.connectSource({[] { return std::optional<OptionValue>{{"0"}}; }, [](std::function<void()>) { return [] {}; }});
                dropdown.connectConfirmed([](const auto&) { return Result{}; });
                check(bool(dropdown.open()), "The scrolling dropdown opens");
                return dropdown;
            };
            auto handle = create();
            auto filler = *ui.addLabel({.id = "filler", .value = std::u16string(1000, u'x')});
            auto root = [&](const ComponentHandle& h) {
                auto content = layout::component(h);
                if (view == View::verticalLabel) content = layout::verticalText(std::get<LabelHandle>(h), 100);
                if (view == View::scrollingLabel) content = layout::scrollText(std::get<LabelHandle>(h), componentScroll, 100);
                if (view == View::list) content = layout::scroll(std::move(content), componentScroll, 100);
                return layout::scroll(layout::column({std::move(content), layout::component(filler)}));
            };
            auto frame = arrange(root(handle), ui, webAppearance(), measure, 160, 220, 1, pageScroll);
            auto inner = std::find_if(frame.regions.begin(), frame.regions.end(), [&](const auto& r) { return r.state != &pageScroll; });
            const bool menu = view == View::contextMenu;
            check(menu ? frame.menuPanel() && frame.menuPanel()->menuMaximum > 0 : inner != frame.regions.end() && inner->maximum > 0,
                  "The component has an overflowing viewport");
            const Rect clip = menu ? frame.menuPanel()->bounds : inner->clip;
            const int x = clip.x + 1, y = clip.y + 1;
            check(frame.wheel(ui, x, y, -24), "The live region handles wheel input");
            auto rejectInput = [&] {
                std::optional<Frame> replacementFrame;
                ComponentHandle replacement;
                if (removal == Removal::reusedId) {
                    replacement = create();
                    replacementFrame = arrange(root(replacement), ui, webAppearance(), measure, 160, 220, 1, pageScroll);
                }
                const auto before = ui.semantics().get({"scrolling"});
                const int beforeComponent = componentScroll.offset;
                std::vector<int> replacementOffsets;
                if (replacementFrame) for (const auto& r : replacementFrame->regions) replacementOffsets.push_back(r.state->offset);
                check(!frame.wheel(ui, x, y, -24), "A retired viewport does not handle wheel input, even after id reuse");
                check(!frame.hwheel(ui, x, y, -24), "A retired viewport does not handle horizontal wheel input");
                check(!frame.wheel(ui, x, y, -24, true), "A retired viewport does not handle Shift-wheel input");
                if (!menu) {
                    check(!frame.scrollTo(ui, *inner, inner->offsetForThumb(inner->horizontal ? inner->track.x : inner->track.y)),
                          "A retained thumb drag never accesses retired scroll state");
                    check(!frame.scrollTo(ui, *inner, inner->maximum), "A retained track click never accesses retired scroll state");
                }
                if (view == View::textField) {
                    const int selectX = clip.x + 12, selectY = clip.y + clip.h + 1;
                    check(!frame.selectTo(ui, std::get<TextFieldHandle>(handle), selectX, selectY), "Retired selection and autoscroll are ignored");
                    if (replacementFrame) {
                        check(!frame.find(std::get<TextFieldHandle>(replacement)), "An old frame cannot find a replacement's placement");
                        check(!frame.selectTo(ui, std::get<TextFieldHandle>(replacement), selectX, selectY), "Old geometry cannot select in a replacement");
                    }
                }
                check(pageScroll.offset == 0 && componentScroll.offset == beforeComponent, "Retired viewport input leaves other scrolling unchanged");
                const auto after = ui.semantics().get({"scrolling"});
                check(before.status == after.status && (!before || *before == *after), "Retired viewport input never changes a replacement");
                if (replacementFrame) {
                    std::size_t i = 0;
                    for (const auto& r : replacementFrame->regions)
                        check(r.state->offset == replacementOffsets[i++], "The replacement viewport never scrolls through the old frame");
                    if (menu) {
                        auto afterFrame = arrange(root(replacement), ui, webAppearance(), measure, 160, 220, 1, pageScroll);
                        check(afterFrame.menuPanel()->menuRows.front().bounds == replacementFrame->menuPanel()->menuRows.front().bounds,
                              "The replacement context menu never scrolls through the old frame");
                    }
                }
            };
            if (removal == Removal::pending) {
                auto trigger = *ui.addButton({.id = "trigger", .label = u"Remove"});
                std::exception_ptr failure;
                trigger.bind([&] {
                    try { check(bool(remove(handle)), "Removal is accepted inside a callback"); rejectInput(); }
                    catch (...) { failure = std::current_exception(); }
                    return Result{};
                });
                check(bool(trigger.invoke()), "Pending removal input returns safely");
                if (failure) std::rethrow_exception(failure);
            } else {
                check(bool(remove(handle)), "Removal commits before input reaches the retained frame");
                rejectInput();
            }
        }
    }
}
}
int main() {
    const test::Case cases[] = {
        {"#104 eight removable kinds", allEightKindsCanBeRemoved},
        {"#104 confirmation membership removal", confirmationLosesRemovedDraftsAndDissolves},
        {"#104 composing field removal", removalDiscardsCompositionAndSkipsBlur},
        {"#104 name label and orphan removal", nameLabelsFallBackAndOrphansReassociate},
        {"#104 dialog removal protection", dialogReferencesCannotBeRemoved},
        {"#104 retained frame removal", retainedFramesRejectRemovedScrollRegions},
    };
    return test::run("removals", cases);
}
