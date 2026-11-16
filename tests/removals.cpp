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
    case 0: return ui.add_label(c).value.value();
    case 1: return ui.add_text_field(c).value.value();
    case 2: return ui.add_checkbox(c).value.value();
    case 3: return ui.add_radio_group(c, choices).value.value();
    case 4: return ui.add_slider(c, {0, 100, 5}).value.value();
    case 5: return ui.add_progress(c).value.value();
    case 6: return ui.add_dropdown(c, choices).value.value();
    case 7: return ui.add_list(c, choices).value.value();
    default: return ui.add_button(c).value.value();
    }
}
Result remove(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Result {
        if constexpr (requires { h.remove(); }) return h.remove();
        else return {Status::not_supported, {}};
    }, handle);
}
Query<ComponentSnapshot> describe(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Query<ComponentSnapshot> {
        if constexpr (requires { h.describe(); }) return h.describe();
        else return {Status::not_supported, {}, {}};
    }, handle);
}
using Subscribe = std::function<std::function<void()>(std::function<void()>)>;
Result connect(const ComponentHandle& handle, bool& changed, int& reads, Subscribe subscribe) {
    return std::visit([&](const auto& h) -> Result {
        using H = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<H, LabelHandle> || std::is_same_v<H, TextFieldHandle>)
            return h.connect_source(Source<std::u16string>{[&] { ++reads; return std::u16string(changed ? u"Changed" : u"Initial"); }, subscribe});
        else if constexpr (std::is_same_v<H, CheckboxHandle>)
            return h.connect_source(Source<bool>{[&] { ++reads; return changed; }, subscribe});
        else if constexpr (std::is_same_v<H, SliderHandle> || std::is_same_v<H, ProgressHandle>)
            return h.connect_source(Source<int>{[&] { ++reads; return changed ? 80 : 20; }, subscribe});
        else if constexpr (std::is_same_v<H, RadioGroupHandle> || std::is_same_v<H, DropdownHandle> || std::is_same_v<H, ListHandle>)
            return h.connect_source(Source<std::optional<OptionValue>>{[&] { ++reads; return std::optional<OptionValue>{{changed ? "b" : "a"}}; }, subscribe});
        else return {Status::not_supported, {}};
    }, handle);
}
void all_eight_kinds_can_be_removed() {
    for (int kind = 0; kind < 9; ++kind) {
        std::cout << "kind " << kind << '\n';
        bool changed = false;
        int reads = 0, cancellations = 0, invalidations = 0;
        std::function<void()> notification;
        Ui ui;
        auto handle = make(ui, kind);
        const auto connected = connect(handle, changed, reads, [&](auto callback) {
            notification = std::move(callback);
            return [&] { ++cancellations; notification(); };
        });
        check(kind == 8 || bool(connected), "Every source-bearing kind connects");
        auto saved_notification = notification;
        if (saved_notification) {
            auto initial = describe(handle);
            changed = true;
            saved_notification();
            check(*describe(handle) != *initial, "A live source notification changes the displayed component snapshot");
            changed = false;
            saved_notification();
        }
        auto first = ui.add_text_field({.id = "first", .label = u"First"}).value.value();
        auto last = ui.add_text_field({.id = "last", .label = u"Last"}).value.value();
        test::FakeMeasure measure;
        auto tree = [&](int mode, bool include_removed) {
            std::vector<layout::Node> children{layout::component(first)};
            if (include_removed) children.push_back(layout::component(handle));
            children.push_back(layout::component(last));
            auto content = mode == 0 ? layout::column(std::move(children), 13) :
                           mode == 1 ? layout::row(std::move(children), 13) : layout::grid(std::move(children), 120, 13);
            return layout::scroll(std::move(content));
        };
        auto frame = [&](int mode, bool include_removed) {
            ScrollState scroll;
            return arrange(tree(mode, include_removed), ui, web_appearance(), measure, 300, 900, 1, scroll);
        };
        check(frame(0, true).items.size() == 3, "The original page includes the component");
        const bool focused = describe(handle)->focusable;
        if (focused) check(bool(ui.semantics().perform({"removed"}, Verb::focus)), "A focusable component receives focus");
        int blurs = 0, focus_moves = 0;
        std::visit([&](const auto& h) {
            if constexpr (requires { h.on_blur([] {}); }) h.on_blur([&] { ++blurs; });
        }, handle);
        detail::access(ui).on_focus([&](const auto& from, const auto& to) { if (from == "removed" && to.empty()) ++focus_moves; });
        std::vector<SemanticEvent> events;
        auto subscription = ui.semantics().subscribe([&](const auto& e) { events.push_back(e); });
        check(bool(describe(handle)), "The component starts alive");
        auto trigger = ui.add_button({.id = "trigger", .label = u"Remove"}).value.value();
        auto group_button = ui.add_button({.id = "group", .label = u"Group"}).value.value();
        trigger.bind([&] {
            check(bool(remove(handle)), "Owner removal is accepted inside a callback");
            check(cancellations == 0, "Physical teardown waits for the safe point");
            check(describe(handle).status == Status::not_found && ui.semantics().get({"removed"}).status == Status::not_found,
                  "Both interfaces refuse the pending component");
            std::visit([&](const auto& h) {
                if constexpr (std::is_constructible_v<ConfirmField, decltype(h)>)
                    check(ui.connect_confirmation(group_button, {h}, [](const auto&) { return ConfirmationResult{}; }).status == Status::invalid_value,
                          "A pending field cannot enter a new confirmation group");
            }, handle);
            const int before_reads = reads;
            invalidations = 0;
            detail::access(ui).on_invalidate([&] { ++invalidations; });
            changed = true;
            if (saved_notification) saved_notification();
            check(reads == before_reads && invalidations == 0, "Pending source notifications neither read nor repaint");
            detail::access(ui).on_invalidate({});
            for (int mode = 0; mode < 3; ++mode) {
                auto pending = frame(mode, true), compact = frame(mode, false);
                check(pending.items.size() == 2 && pending.extent == compact.extent &&
                      pending.find(last)->bounds == compact.find(last)->bounds, "Pending removal also collapses during reentrant layout");
            }
            return Result{};
        });
        check(bool(trigger.invoke()), "The callback returns safely");
        check(blurs == 0 && focus_moves == (focused ? 1 : 0), "Removal skips field blur callbacks and keeps the host focus notification");
        check(cancellations == (kind == 8 ? 0 : 1), "Removal cancels the source once when present");
        check(describe(handle).status == Status::not_found, "A removed handle is invalid");
        check(remove(handle).status == Status::not_found, "Removal cannot be repeated");
        ui.semantics().dispatch_events();
        check(std::any_of(events.begin(), events.end(), [](const auto& e) { return e.kind == SemanticEventKind::structure_changed; }),
              "The semantic tree announces the removed structure");
        for (int mode = 0; mode < 3; ++mode) {
            std::cout << "layout " << mode << '\n';
            auto actual = frame(mode, true), compact = frame(mode, false);
            check(actual.items.size() == 2 && actual.extent == compact.extent, "Removal leaves no height or vertical gap");
            check(actual.find(first)->bounds == compact.find(first)->bounds && actual.find(last)->bounds == compact.find(last)->bounds,
                  "Column, row and grid match a page assembled without the removed component");
        }
        auto replacement = make(ui, kind);
        check(handle != replacement && describe(handle).status == Status::not_found, "Reusing an id never revives the old handle");
        const auto replacement_before = describe(replacement);
        invalidations = 0;
        detail::access(ui).on_invalidate([&] { ++invalidations; });
        if (saved_notification) saved_notification();
        check(invalidations == 0 && *describe(replacement) == *replacement_before, "A late callback cannot reach the replacement");
        detail::access(ui).on_invalidate({});
        check(bool(first.remove()) && bool(last.remove()), "The surviving layout entries can also be removed");
        for (int mode = 0; mode < 3; ++mode) {
            auto empty = frame(mode, true);
            check(empty.items.empty() && empty.extent == 0, "A layout with every original instance removed has no residual gaps");
        }
    }
}
void confirmation_loses_removed_drafts_and_dissolves() {
    Ui ui;
    auto first = ui.add_text_field({.id = "first", .label = u"First"}).value.value();
    auto second = ui.add_checkbox({.id = "second", .label = u"Second"}).value.value();
    auto apply = ui.add_button({.id = "apply", .label = u"Apply"}).value.value();
    auto subscribe = [](auto) { return [] {}; };
    first.connect_source({[] { return std::u16string(u"Initial"); }, subscribe});
    second.connect_source({[] { return false; }, subscribe});
    int batches = 0;
    auto capture = std::make_shared<int>(0);
    std::weak_ptr<int> captured = capture;
    check(bool(ui.connect_confirmation(apply, {first, second}, [&, capture](const auto& changes) {
        ++batches;
        check(changes.size() == 1, "A removed draft is omitted from the batch");
        const auto& change = std::get<Change<CheckboxHandle>>(changes[0]);
        check(change.field == second && change.value && !change.origin, "Only the surviving field's draft and origin are written");
        return ConfirmationResult{};
    })), "The mixed confirmation group connects");
    capture.reset();
    check(bool(first.set_value(u"Discarded")) && bool(second.toggle()), "Both fields have drafts");
    check(bool(first.remove()) && bool(apply.invoke()) && batches == 1, "Partial removal leaves a usable confirmation");
    check(bool(second.remove()) && captured.expired(), "The last removal releases the group's write callback");
    check(apply.describe()->enabled, "Dissolving the group preserves the button's enabled state");
    check(apply.invoke().status == Status::not_supported && batches == 1, "The ordinary button has no action");
    auto replacement = ui.add_checkbox({.id = "second", .label = u"Replacement"}).value.value();
    replacement.connect_source({[] { return false; }, subscribe});
    check(bool(ui.connect_confirmation(apply, {replacement}, [](const auto&) { return ConfirmationResult{}; })),
          "The former button can connect a new group with a new instance");
    apply.enable(false);
    check(bool(replacement.remove()) && !apply.describe()->enabled, "Dissolving a new group preserves a disabled button too");
    apply.enable(true);
    check(bool(apply.bind([&] { ++batches; return Result{}; })) && bool(apply.invoke()) && batches == 2, "The former confirmation button can be rebound");
}
void removal_discards_composition_and_skips_blur() {
    for (bool rich : {false, true}) {
        Ui ui;
        auto field = ui.add_text_field({.id = "field", .label = u"Field", .multiline = rich, .rich = rich}).value.value();
        int writes = 0, blurs = 0, before_focus = 0, moved = 0;
        auto subscribe = [](auto) { return [] {}; };
        if (rich) {
            field.connect_rich_source({[] { return RichText{u"Initial", {}}; }, subscribe});
            field.connect_rich_immediate([&](const auto&) { ++writes; return Result{}; });
        } else {
            field.connect_source({[] { return std::u16string(u"Initial"); }, subscribe});
            field.connect_immediate([&](const auto&) { ++writes; return Result{}; });
        }
        field.on_blur([&] { ++blurs; });
        auto& host = detail::access(ui);
        field.focus();
        host.before_focus([&] { ++before_focus; });
        host.on_focus([&](const auto& from, const auto& to) { if (from == "field" && to.empty()) ++moved; });
        check(bool(host.composition_update(u"Uncommitted", 11)), "IME preedit starts through the existing host seam");
        check(bool(field.remove()), "A composing field can be removed");
        check(writes == 0 && blurs == 0 && before_focus == 0 && moved == 1,
              "Removal discards composition and blur validation, but reports host focus movement");
        check(host.composition_commit(u"Late").status != Status::ok && writes == 0, "A late IME result lands nowhere");
    }
}
void name_labels_fall_back_and_orphans_reassociate() {
    Ui ui;
    auto field = ui.add_text_field({.id = "field", .label = u"Fallback"}).value.value();
    auto label = ui.add_label({.id = "name", .value = u"Name", .target = "field", .label_kind = LabelKind::name}).value.value();
    std::vector<SemanticEvent> events;
    auto subscription = ui.semantics().subscribe([&](const auto& e) { events.push_back(e); });
    check(field.describe()->name == u"Name", "The name label supplies the accessible name");
    auto trigger = ui.add_button({.id = "trigger", .label = u"Remove"}).value.value();
    std::u16string label_text = u"Name";
    label.connect_source([&] { return label_text; });
    trigger.bind([&] {
        check(bool(label.remove()), "Name label removal is accepted");
        label_text = u"Must not appear";
        check(field.describe()->name == u"Fallback", "Pending name labels no longer supply text to their target");
        return Result{};
    });
    check(bool(trigger.invoke()) && field.describe()->name == u"Fallback", "Removing the name label restores the fallback");
    ui.semantics().dispatch_events();
    check(std::count_if(events.begin(), events.end(), [](const auto& e) {
        return e.kind == SemanticEventKind::property_changed && e.ref.id == "field" && e.property == "name";
    }) == 1, "The accessible name changes exactly once");
    label = ui.add_label({.id = "name", .value = u"Name", .target = "field", .label_kind = LabelKind::name}).value.value();
    check(bool(field.remove()) && label.describe()->value == u"Name", "Removing the target leaves its label alive");
    test::FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(layout::column({layout::component(label)}), ui, web_appearance(), measure, 300, 300, 1, scroll);
    check(frame.find(label) && label.describe()->visible, "An orphan label remains on screen");
    auto replacement = ui.add_text_field({.id = "field", .label = u"New fallback"}).value.value();
    check(replacement.describe()->name == u"Name" && ui.semantics().get({"field"})->labelled_by == std::vector<NodeRef>{{"name"}},
          "The label automatically names a replacement with the same component id");
}
void dialog_references_cannot_be_removed() {
    for (int kind = 0; kind < 9; ++kind) {
        Ui ui;
        auto member = make(ui, kind);
        auto initial = ui.add_text_field({.id = "initial", .label = u"Initial"}).value.value();
        auto fallback = ui.add_button({.id = "fallback", .label = u"Fallback"}).value.value();
        auto name = ui.add_label({.id = "name", .value = u"Dialog"}).value.value();
        auto dialog = ui.add_dialog("dialog", {member, initial, name}, initial, fallback, [](bool) {}, name);
        check(bool(dialog), "The dialog is assembled");
        check(remove(member).status == Status::invalid_value && initial.remove().status == Status::invalid_value &&
              fallback.remove().status == Status::invalid_value && name.remove().status == Status::invalid_value,
              "Members, both focus targets and the name label stay alive even while the dialog is closed");
        check(bool(dialog.value->open()) && remove(member).status == Status::invalid_value, "Open dialog membership is protected too");
    }
}
}
int main() {
    const test::Case cases[] = {
        {"#104 eight removable kinds", all_eight_kinds_can_be_removed},
        {"#104 confirmation membership removal", confirmation_loses_removed_drafts_and_dissolves},
        {"#104 composing field removal", removal_discards_composition_and_skips_blur},
        {"#104 name label and orphan removal", name_labels_fall_back_and_orphans_reassociate},
        {"#104 dialog removal protection", dialog_references_cannot_be_removed},
    };
    return test::run("removals", cases);
}
