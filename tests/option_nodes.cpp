#include <algorithm>
#include "isoduo/core.hpp"
#include "isoduo/semantics.hpp"
#include "isoduo/layout.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include "option_properties.hpp"
#include "constant_source.hpp"
#include "core_internal.hpp"
using namespace isoduo;
using test::check;
using test::constantSource;

namespace {
using OptionHandle = std::variant<RadioGroupHandle, ListHandle, DropdownHandle, TabsHandle, NavigationListHandle>;
struct Subject {
    std::string id;
    OptionHandle handle;
    bool row() const { return std::holds_alternative<ListHandle>(handle) || std::holds_alternative<NavigationListHandle>(handle); }
    bool dropdown() const { return std::holds_alternative<DropdownHandle>(handle); }
    ComponentHandle component() const { return std::visit([](auto h) -> ComponentHandle { return h; }, handle); }
    ComponentSnapshot describe() const { return std::visit([](auto h) { return *h.describe(); }, handle); }
};
struct World {
    Ui ui;
    test::FakeMeasure measure;
    ScrollState scroll;
    std::vector<Subject> subjects;
    template <class H> void keep(H h) { subjects.push_back({h.describe()->id, h}); }
    explicit World(bool enabled = true, bool secondEnabled = true) {
        const std::vector<OptionItem> items = {{{"a"}, u"Alpha"}, {{"b"}, u"Beta", secondEnabled}};
        const std::vector<TabPage> pages = {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Beta", secondEnabled, {}}};
        auto config = [&](std::string id) { return ComponentConfig{.id = std::move(id), .label = u"Fallback", .enabled = enabled}; };
        auto radio = ui.addRadioGroup(config("radio"), items).value.value();
        radio.connectSource(constantSource(std::optional{OptionValue{"a"}}));
        radio.connectConfirmed([](const auto&) { return Result{}; }); keep(radio);
        auto list = ui.addList(config("list"), items).value.value();
        list.connectSource(constantSource(std::optional{OptionValue{"a"}}));
        list.connectImmediate([](const auto&) { return Result{}; }); keep(list);
        auto dropdown = ui.addDropdown(config("dropdown"), items).value.value();
        dropdown.connectSource(constantSource(std::optional{OptionValue{"a"}}));
        dropdown.connectConfirmed([](const auto&) { return Result{}; }); keep(dropdown);
        keep(ui.addTabs(config("tabs"), pages).value.value());
        keep(ui.addNavigationList(config("navigation"), pages, {"a"}).value.value());
        for (const auto& subject : subjects) {
            for (auto kind : {LabelKind::name, LabelKind::description, LabelKind::error}) {
                auto label = ComponentConfig{.id = subject.id + ".label." + std::to_string(static_cast<int>(kind)),
                                             .value = u"Parent text", .target = subject.id, .labelKind = kind};
                check(bool(ui.addLabel(label)), "option fixture: attach parent label");
            }
        }
    }
    layout::Node root() const {
        std::vector<layout::Node> nodes;
        for (const auto& subject : subjects) nodes.push_back(layout::component(subject.component()));
        return layout::column(std::move(nodes));
    }
    void arrangePage() { arrange(root(), ui, webAppearance(), measure, 600, 1600, 1.f, scroll); }
};

struct State {
    bool arranged = true;
    bool enabled = true;
    bool secondEnabled = true;
    bool hidden = false;
    bool blocked = false;
    bool dropdownOpen = false;
    bool menus = false;
    bool menuOpen = false;
    bool fieldErrors = false;
    bool pageError = false;
    std::string focused = {};
};
// Start from the complete default base: a future common field must stay default on options
// unless this explicit allowlist is extended by a product decision.
void compareAll(World& w, const char* stateName, const State& state = {}) {
    for (const auto& subject : w.subjects) {
        const auto component = subject.describe();
        const auto parent = w.ui.semantics().get({subject.id});
        check(parent && parent->focused == component.focused, (std::string(stateName) + "/" + subject.id + ": component focus matches describe").c_str());
        for (const auto& item : component.items) {
            const auto context = std::string(stateName) + "/" + subject.id + "/" + item.value.id;
            const auto node = w.ui.semantics().get({subject.id, item.value});
            check(bool(parent) && bool(node), (context + ": parent and option readable").c_str());
            check(test::optionHelpTextIsEmpty(node->description.size()), (context + ": option HelpText is empty").c_str());
            if (subject.row())
                check(test::disabledRowCannotTakeFocus(parent->enabled && node->enabled, node->focusable),
                      (context + ": disabled row cannot take keyboard focus").c_str());
            CommonState expected;
            expected.name = item.value.id == "a" ? u"Alpha" : u"Beta";
            expected.enabled = item.value.id == "a" || state.secondEnabled;
            expected.invalid = state.pageError && (std::holds_alternative<TabsHandle>(subject.handle) ||
                               std::holds_alternative<NavigationListHandle>(subject.handle)) && item.value.id == "b";
            expected.focused = (!subject.dropdown() || state.dropdownOpen) && state.focused == subject.id && item.value.id == "a" && !state.menuOpen;
            expected.focusable = component.focusable && expected.enabled && (!subject.dropdown() || state.dropdownOpen);
            expected.visible = state.arranged && !state.hidden && (!subject.dropdown() || state.dropdownOpen);
            if (state.enabled && expected.enabled && !state.hidden && !state.blocked) {
                expected.verbs = {Verb::select};
                if (!std::holds_alternative<RadioGroupHandle>(subject.handle) && (!subject.dropdown() || state.dropdownOpen))
                    expected.verbs.push_back(Verb::focus);
                if (subject.row() && state.menus) expected.verbs.push_back(Verb::contextMenu);
            }
            check(static_cast<const CommonState&>(*node) == expected, (context + ": option-only CommonState").c_str());
            const bool rejected = state.fieldErrors && (std::holds_alternative<RadioGroupHandle>(subject.handle) || subject.dropdown());
            check(node->selected == (item.value.id == (rejected ? "b" : "a")), (context + ": own selection").c_str());
            check(node->labelledBy.empty() && node->describedBy.empty() && !node->selectionRequired,
                  (context + ": no parent labels or selection requirement").c_str());
            check(node->hidden == parent->hidden && node->hidden == state.hidden &&
                  node->blockedByModal == parent->blockedByModal && node->blockedByModal == state.blocked,
                  (context + ": hidden and modal state follow parent").c_str());
            check(node->bounds.has_value() == expected.visible, (context + ": bounds belong to visible option").c_str());
        }
    }
}
void normal() {
    World w;
    compareAll(w, "before_layout", {.arranged = false});
    w.arrangePage(); compareAll(w, "normal");
    for (const auto& subject : w.subjects) {
        check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), ("normal/" + subject.id + ": focus parent").c_str());
        compareAll(w, "focused", {.focused = subject.id});
    }
}
void disabled() {
    World w(false); w.arrangePage();
    compareAll(w, "component_disabled", {.enabled = false});
    for (const auto& subject : w.subjects)
        check(!subject.describe().focusable, ("component_disabled/" + subject.id + ": parent cannot take focus").c_str());
}
void optionDisabled() {
    World w(true, false); w.arrangePage();
    compareAll(w, "option_disabled", {.secondEnabled = false});
    for (const auto& subject : w.subjects) {
        check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), ("option_disabled/" + subject.id + ": focus parent").c_str());
        compareAll(w, "option_disabled_focused", {.secondEnabled = false, .focused = subject.id});
    }
}
void hidden() {
    World w; w.arrangePage();
    std::vector<ComponentHandle> members;
    for (const auto& subject : w.subjects) members.push_back(subject.component());
    const auto gate = w.ui.addNavigationList({.id = "gate"},
        {{{"shown"}, u"Shown", true, members}, {{"hidden"}, u"Hidden", true, {}}}, {"shown"}).value.value();
    check(bool(gate.select({"hidden"})), "hidden: switch away from every option component");
    w.arrangePage(); compareAll(w, "hidden", {.hidden = true});
}
void blockedByModal() {
    World w;
    auto opener = w.ui.addButton({.id = "opener", .label = u"Open"}).value.value();
    auto inside = w.ui.addButton({.id = "dialog.close", .label = u"Close"}).value.value();
    const auto dialog = w.ui.addDialog("dialog", {inside}, inside, opener, [](bool) {}).value.value();
    check(bool(dialog.open()), "modal: open dialog over every option component");
    arrange(layout::modal(w.root(), layout::component(inside), dialog), w.ui, webAppearance(), w.measure, 600, 1600, 1.f, w.scroll);
    compareAll(w, "blocked_by_modal", {.blocked = true});
}
void errors() {
    World w;
    for (const auto& subject : w.subjects) {
        // Only the radio group and dropdown support a rejected confirmed write. Lists have
        // no retained error, and page containers get their marks from members in pageErrors.
        std::visit([&](auto h) {
            if constexpr (requires { h.connectConfirmed([](const auto&) { return Result{}; }); }) {
                check(bool(h.connectConfirmed([](const auto&) { return Result{Status::invalidValue, u"Parent error"}; })) &&
                      bool(h.select({"b"})) && h.confirm().status == Status::invalidValue,
                      ("component_error/" + subject.id + ": rejected confirmation").c_str());
                const auto parent = h.describe();
                check(parent->invalid && parent->error == u"Parent error", ("component_error/" + subject.id + ": error reached").c_str());
            }
        }, subject.handle);
    }
    w.arrangePage(); compareAll(w, "component_error", {.fieldErrors = true});
}
void pageErrors() {
    World w;
    for (const auto& subject : w.subjects) std::visit([&](auto h) {
        if constexpr (requires { h.addMember(OptionValue{"b"}, ComponentHandle{}); }) {
            auto member = w.ui.addTextField({.id = subject.id + ".member", .label = u"Member"}).value.value();
            check(bool(member.setError(u"Member error")) && bool(h.addMember({"b"}, member)),
                  ("page_error/" + subject.id + "/b: erroneous member attached to inactive page").c_str());
        }
    }, subject.handle);
    w.arrangePage(); compareAll(w, "page_member_error", {.pageError = true});
    for (const auto& subject : w.subjects) if (std::holds_alternative<TabsHandle>(subject.handle) ||
                                             std::holds_alternative<NavigationListHandle>(subject.handle)) {
        const auto node = w.ui.semantics().get({subject.id, OptionValue{"b"}});
        check(node->invalid && node->error.empty(), ("page_error/" + subject.id + "/b: mark without duplicate message").c_str());
    }
}
void dropdownOpen() {
    World w; w.arrangePage();
    for (const auto& subject : w.subjects) if (auto h = std::get_if<DropdownHandle>(&subject.handle)) {
        check(bool(h->open()), "dropdown_open/dropdown/a: open popup");
        w.arrangePage();
        check(h->describe()->expanded, "dropdown_open/dropdown/a: parent expanded");
    }
    compareAll(w, "dropdown_open", {.dropdownOpen = true, .focused = "dropdown"});
}
void focusEvents() {
    World w; w.arrangePage();
    auto tree = w.ui.semantics();
    std::vector<NodeRef> events;
    auto subscription = tree.subscribe([&](const SemanticEvent& event) {
        if (event.kind == SemanticEventKind::focusChanged) events.push_back(event.ref);
    });
    auto expect = [&](const char* state, NodeRef ref) {
        tree.dispatchEvents();
        check(events == std::vector<NodeRef>{ref}, (std::string(state) + "/" + ref.id + "/" +
              (ref.item ? ref.item->id : "component") + ": exactly one focus event").c_str());
        events.clear();
    };
    auto key = [&](Key value) { check(bool(detail::access(w.ui).keyDown({.key = value})), "focus event: key admitted"); };
    auto tabs = std::get<TabsHandle>(w.subjects[3].handle);
    tabs.focus(); expect("enter_tabs", {"tabs", OptionValue{"a"}});
    key(Key::right); expect("candidate_page", {"tabs", OptionValue{"b"}});
    check(*tabs.active() == OptionValue{"a"}, "candidate page does not activate");
    auto dropdown = std::get<DropdownHandle>(w.subjects[2].handle);
    dropdown.focus(); expect("closed_dropdown", {"dropdown"});
    key(Key::enter); expect("open_dropdown", {"dropdown", OptionValue{"a"}});
    key(Key::down); expect("dropdown_candidate", {"dropdown", OptionValue{"b"}});
    key(Key::escape); expect("cancel_dropdown", {"dropdown"});
    key(Key::enter); expect("reopen_dropdown", {"dropdown", OptionValue{"a"}});
    key(Key::enter); expect("accept_dropdown", {"dropdown"});
    auto radio = std::get<RadioGroupHandle>(w.subjects[0].handle);
    radio.focus(); expect("enter_radio", {"radio", OptionValue{"a"}});
    key(Key::down); expect("radio_selection", {"radio", OptionValue{"b"}});
    radio.enableOption({"b"}, false); expect("disabled_selection", {"radio", OptionValue{"a"}});
    radio.enableOption({"a"}, false); expect("all_radio_disabled", {"radio"});
    auto list = std::get<ListHandle>(w.subjects[1].handle);
    list.focus(); expect("enter_list", {"list", OptionValue{"a"}});
    list.setItems({{{"a"}, u"Alpha", false}, {{"b"}, u"Beta"}});
    expect("disabled_cursor", {"list", OptionValue{"b"}});
    list.setItems({{{"a"}, u"Alpha"}}); expect("removed_cursor", {"list", OptionValue{"a"}});
}
void programmaticFocus() {
    World w; w.arrangePage();
    auto tree = w.ui.semantics();
    std::vector<NodeRef> events;
    auto subscription = tree.subscribe([&](const SemanticEvent& event) {
        if (event.kind == SemanticEventKind::focusChanged) events.push_back(event.ref);
    });
    for (const auto& subject : w.subjects) std::visit([&](auto h) {
        if constexpr (requires { h.focus(OptionValue{}); }) {
            if (subject.dropdown()) check(bool(std::get<DropdownHandle>(subject.handle).open()), "open before option focus");
            tree.dispatchEvents(); events.clear();
            check(bool(h.focus({"b"})), (subject.id + "/b: typed focus").c_str());
            tree.dispatchEvents();
            check(events == std::vector<NodeRef>{{subject.id, OptionValue{"b"}}}, (subject.id + "/b: one focus event").c_str());
            const auto snapshot = h.describe();
            check(test::focusPreservesSelection(snapshot->items[0].selected, snapshot->items[1].selected),
                  (subject.id + "/b: focus preserves selection").c_str());
            events.clear();
            check(bool(tree.perform({subject.id, OptionValue{"b"}}, Verb::focus)), (subject.id + "/b: semantic focus").c_str());
            tree.dispatchEvents();
            check(events.empty(), (subject.id + "/b: repeated focus sends no event").c_str());
            check(h.focus({"missing"}).status == Status::notFound, (subject.id + ": unknown focus target").c_str());
        }
    }, subject.handle);
    auto dropdown = std::get<DropdownHandle>(w.subjects[2].handle);
    dropdown.cancel();
    check(dropdown.focus({"a"}).status == Status::invalidValue && !dropdown.describe()->expanded,
          "closed dropdown rejects option focus without opening");
    check(tree.perform({"dropdown", OptionValue{"a"}}, Verb::focus).status == Status::invalidValue,
          "closed dropdown semantic focus uses same admission");
}
void compareFocus(World& w, const char* state, const Subject& subject, std::optional<OptionValue> expected) {
    const auto component = subject.describe();
    const auto parent = w.ui.semantics().get({subject.id});
    check(parent && parent->focused == component.focused && (!parent->focused || parent->focusable),
          (std::string(state) + "/" + subject.id + ": component focus matches describe").c_str());
    int focused = 0;
    for (const auto& item : component.items) {
        const auto node = w.ui.semantics().get({subject.id, item.value});
        const auto context = std::string(state) + "/" + subject.id + "/" + item.value.id;
        check(node && test::optionFocusMatches(node->focused, node->focusable, expected == item.value),
              (context + ": expected focus option and focused implies focusable").c_str());
        check(test::optionFocusabilityMatches(node->focusable, parent->focusable, item.enabled, !subject.dropdown() || component.expanded),
              (context + ": option focusability").c_str());
        focused += node->focused;
    }
    check(focused == (expected ? 1 : 0), (std::string(state) + "/" + subject.id + ": unique focus option").c_str());
}
void focusAdmissionAndReveal() {
    World w(true, false); w.arrangePage();
    for (const auto& subject : w.subjects) std::visit([&](auto h) {
        if constexpr (requires { h.focus(OptionValue{}); }) {
            check(h.focus({"b"}).status == Status::disabled &&
                  w.ui.semantics().perform({subject.id, OptionValue{"b"}}, Verb::focus).status == Status::disabled,
                  (subject.id + "/b: disabled focus target is rejected at both seams").c_str());
        }
    }, subject.handle);
    auto dropdown = std::get<DropdownHandle>(w.subjects[2].handle);
    std::vector<OptionItem> items;
    for (int i = 0; i < 30; ++i) items.push_back({OptionValue{std::to_string(i)}, u"Region"});
    check(bool(dropdown.setItems(items)) && bool(dropdown.open()), "large dropdown opens");
    auto root = layout::component(dropdown);
    arrange(root, w.ui, webAppearance(), w.measure, 400, 300, 1.f, w.scroll);
    check(!w.ui.semantics().get({"dropdown", OptionValue{"29"}})->visible, "last option begins offscreen");
    check(bool(dropdown.focus({"29"})), "programmatic dropdown focus targets offscreen option");
    arrange(root, w.ui, webAppearance(), w.measure, 400, 300, 1.f, w.scroll);
    const auto target = w.ui.semantics().get({"dropdown", OptionValue{"29"}});
    check(target->focused && target->visible && !dropdown.draft()->has_value(), "focus reveals option without selecting it");
}
void pageFocusRechecksAfterBlur() {
    for (bool navigation : {false, true}) {
        World w;
        auto previous = w.ui.addTextField({.id = "previous", .label = u"Previous"}).value.value();
        for (const auto& subject : w.subjects) std::visit([&](auto h) {
            if constexpr (std::is_same_v<decltype(h), TabsHandle> || std::is_same_v<decltype(h), NavigationListHandle>) {
                if (std::holds_alternative<NavigationListHandle>(subject.handle) != navigation) return;
                check(bool(previous.focus()) && bool(previous.onBlur([h] { h.enablePage({"b"}, false); })), "install page-disabling blur callback");
                check(h.focus({"b"}).status == Status::disabled, (subject.id + "/b: blur-disabled focus target is rejected").c_str());
                check(h.describe()->candidate == OptionValue{"a"}, (subject.id + ": rejected focus keeps enabled candidate").c_str());
                compareFocus(w, "blur_disabled_target", subject, OptionValue{"a"});
            }
        }, subject.handle);
    }
}
void focusStates() {
    for (const auto* state : {"unfocused", "selected", "keyboard_candidate", "closed_dropdown", "disabled_selection", "all_disabled"}) {
        World w; w.arrangePage();
        for (const auto& subject : w.subjects) {
            std::optional<OptionValue> expected;
            if (std::string_view(state) != "unfocused") {
                check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), "matrix focus component");
                if (subject.dropdown() && std::string_view(state) != "closed_dropdown")
                    check(bool(std::get<DropdownHandle>(subject.handle).open()), "matrix open dropdown");
                if (!subject.dropdown() || std::string_view(state) != "closed_dropdown") expected = OptionValue{"a"};
            }
            if (std::string_view(state) == "keyboard_candidate") {
                check(bool(detail::access(w.ui).keyDown({.key = std::holds_alternative<TabsHandle>(subject.handle) ? Key::right : Key::down})),
                      "matrix keyboard moves focus option");
                expected = OptionValue{"b"};
            }
            if (std::string_view(state) == "disabled_selection") {
                std::visit([&](auto h) {
                    if constexpr (std::is_same_v<decltype(h), RadioGroupHandle>) {
                        h.select({"b"}); h.enableOption({"b"}, false); expected = OptionValue{"a"};
                    } else if constexpr (std::is_same_v<decltype(h), ListHandle>) {
                        h.setItems({{{"a"}, u"Alpha", false}, {{"b"}, u"Beta"}}); expected = OptionValue{"b"};
                    }
                }, subject.handle);
            }
            if (std::string_view(state) == "all_disabled") {
                std::visit([&](auto h) {
                    if constexpr (std::is_same_v<decltype(h), RadioGroupHandle>) {
                        h.enableOption({"a"}, false); h.enableOption({"b"}, false); expected.reset();
                    } else if constexpr (requires { h.setItems(std::vector<OptionItem>{}); }) {
                        h.setItems({{{"a"}, u"Alpha", false}, {{"b"}, u"Beta", false}}); expected.reset();
                    } else {
                        // Page containers require one enabled active page: an all-disabled state is rejected.
                        check(h.enablePage({"a"}, false).status == Status::invalidValue, "active page cannot be disabled");
                        h.enablePage({"b"}, false);
                    }
                }, subject.handle);
            }
            compareFocus(w, state, subject, expected);
        }
    }
}
void snapshotReadBudget() {
    Ui ui;
    std::vector<OptionItem> items;
    for (int i = 0; i < 64; ++i) items.push_back({{std::to_string(i)}, u"Item"});
    auto list = ui.addList({.id = "list", .label = u"List"}, items).value.value();
    int reads = 0;
    list.connectSource({[&] { ++reads; return std::optional{OptionValue{"0"}}; }, [](auto) { return [] {}; }});
    list.connectImmediate([](const auto&) { return Result{}; });
    auto button = ui.addButton({.id = "button", .label = u"Button"}).value.value();
    std::vector<SemanticEvent> events;
    auto subscription = ui.semantics().subscribe([&](const auto& event) { events.push_back(event); });
    reads = 0;
    check(bool(button.focus()), "focus with an observed large option collection");
    // Capturing a semantic event must not re-read the whole collection for every option.
    // This generous constant budget permits several complete before/after reads, independently of item count.
    check(reads <= 32, "semantic capture reads a collection a bounded number of times");
    ui.semantics().dispatchEvents();
    check(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.kind == SemanticEventKind::focusChanged && event.ref == NodeRef{"button"};
    }), "bounded capture still delivers the focus event");
    check(ui.semantics().get({"list", OptionValue{"0"}})->selected, "bounded capture preserves option selection");
}
void contextMenuOpen() {
    World w; w.arrangePage();
    for (const auto& subject : w.subjects)
        check(bool(w.ui.addContextMenu(subject.component(), {{.id = "inspect", .label = u"Inspect", .shortcutHint = {},
                   .action = [](const auto&) { return Result{}; }}})), ("context_menu/" + subject.id + ": register menu").c_str());
    for (const auto& subject : w.subjects) {
        check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), ("context_menu/" + subject.id + ": focus parent").c_str());
        compareAll(w, "before_context_menu", {.menus = true, .focused = subject.id});
        check(bool(w.ui.openContextMenu(subject.component())), ("context_menu/" + subject.id + ": open menu").c_str());
        w.arrangePage();
        compareAll(w, "context_menu_open", {.menus = true, .menuOpen = true, .focused = subject.id});
        check(bool(w.ui.closeContextMenu()), ("context_menu/" + subject.id + ": close menu").c_str());
        compareAll(w, "context_menu_closed", {.menus = true, .focused = subject.id});
    }
}
}
int main() {
    const test::Case cases[] = {{"normal", normal}, {"disabled", disabled}, {"option_disabled", optionDisabled},
                              {"hidden", hidden}, {"blocked_by_modal", blockedByModal}, {"errors", errors},
                              {"page_errors", pageErrors}, {"dropdown_open", dropdownOpen}, {"focus_events", focusEvents},
                              {"programmatic_focus", programmaticFocus}, {"focus_states", focusStates},
                              {"focus_admission_and_reveal", focusAdmissionAndReveal}, {"page_focus_after_blur", pageFocusRechecksAfterBlur},
                              {"snapshot_read_budget", snapshotReadBudget}, {"context_menu_open", contextMenuOpen}};
    return test::run("option_nodes", cases);
}
