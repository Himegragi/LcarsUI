#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "lcars/layout.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
using namespace lcars;
using test::check;

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
template <class T> Source<T> source(T value) {
    return {[value] { return value; }, [](std::function<void()>) { return [] {}; }};
}
struct World {
    Ui ui;
    test::FakeMeasure measure;
    ScrollState scroll;
    std::vector<Subject> subjects;
    template <class H> void keep(H h) { subjects.push_back({h.describe()->id, h}); }
    explicit World(bool enabled = true, bool second_enabled = true) {
        const std::vector<OptionItem> items = {{{"a"}, u"Alpha"}, {{"b"}, u"Beta", second_enabled}};
        const std::vector<TabPage> pages = {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Beta", second_enabled, {}}};
        auto config = [&](std::string id) { return ComponentConfig{.id = std::move(id), .label = u"Fallback", .enabled = enabled}; };
        auto radio = ui.addRadioGroup(config("radio"), items).value.value();
        radio.connectSource(source(std::optional{OptionValue{"a"}}));
        radio.connectConfirmed([](const auto&) { return Result{}; }); keep(radio);
        auto list = ui.addList(config("list"), items).value.value();
        list.connectSource(source(std::optional{OptionValue{"a"}}));
        list.connectImmediate([](const auto&) { return Result{}; }); keep(list);
        auto dropdown = ui.addDropdown(config("dropdown"), items).value.value();
        dropdown.connectSource(source(std::optional{OptionValue{"a"}}));
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
    void arrange_page() { arrange(root(), ui, webAppearance(), measure, 600, 1600, 1.f, scroll); }
};

struct State {
    bool arranged = true;
    bool enabled = true;
    bool second_enabled = true;
    bool hidden = false;
    bool blocked = false;
    bool dropdown_open = false;
    bool menus = false;
    bool menu_open = false;
    bool field_errors = false;
    bool page_error = false;
    std::string focused = {};
};
// Start from the complete default base: a future common field must stay default on options
// unless this explicit allowlist is extended by a product decision.
void compare_all(World& w, const char* state_name, const State& state = {}) {
    for (const auto& subject : w.subjects) {
        const auto component = subject.describe();
        const auto parent = w.ui.semantics().get({subject.id});
        for (const auto& item : component.items) {
            const auto context = std::string(state_name) + "/" + subject.id + "/" + item.value.id;
            const auto node = w.ui.semantics().get({subject.id, item.value});
            check(bool(parent) && bool(node), (context + ": parent and option readable").c_str());
            CommonState expected;
            expected.name = item.value.id == "a" ? u"Alpha" : u"Beta";
            expected.enabled = item.value.id == "a" || state.second_enabled;
            expected.invalid = state.page_error && (std::holds_alternative<TabsHandle>(subject.handle) ||
                               std::holds_alternative<NavigationListHandle>(subject.handle)) && item.value.id == "b";
            expected.focused = subject.row() && state.focused == subject.id && item.value.id == "a" && !state.menu_open;
            expected.focusable = subject.row() && component.focusable && expected.enabled;
            expected.visible = state.arranged && !state.hidden && (!subject.dropdown() || state.dropdown_open);
            if (state.enabled && expected.enabled && !state.hidden && !state.blocked) {
                expected.verbs = {Verb::select};
                if (std::holds_alternative<ListHandle>(subject.handle)) expected.verbs.push_back(Verb::focus);
                if (subject.row() && state.menus) expected.verbs.push_back(Verb::contextMenu);
            }
            check(static_cast<const CommonState&>(*node) == expected, (context + ": option-only CommonState").c_str());
            const bool rejected = state.field_errors && (std::holds_alternative<RadioGroupHandle>(subject.handle) || subject.dropdown());
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
    compare_all(w, "before_layout", {.arranged = false});
    w.arrange_page(); compare_all(w, "normal");
    for (const auto& subject : w.subjects) {
        check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), ("normal/" + subject.id + ": focus parent").c_str());
        compare_all(w, "focused", {.focused = subject.id});
    }
}
void disabled() {
    World w(false); w.arrange_page();
    compare_all(w, "component_disabled", {.enabled = false});
    for (const auto& subject : w.subjects)
        check(!subject.describe().focusable, ("component_disabled/" + subject.id + ": parent cannot take focus").c_str());
}
void option_disabled() {
    World w(true, false); w.arrange_page();
    compare_all(w, "option_disabled", {.second_enabled = false});
    for (const auto& subject : w.subjects) {
        check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), ("option_disabled/" + subject.id + ": focus parent").c_str());
        compare_all(w, "option_disabled_focused", {.second_enabled = false, .focused = subject.id});
    }
}
void hidden() {
    World w; w.arrange_page();
    std::vector<ComponentHandle> members;
    for (const auto& subject : w.subjects) members.push_back(subject.component());
    const auto gate = w.ui.addNavigationList({.id = "gate"},
        {{{"shown"}, u"Shown", true, members}, {{"hidden"}, u"Hidden", true, {}}}, {"shown"}).value.value();
    check(bool(gate.select({"hidden"})), "hidden: switch away from every option component");
    w.arrange_page(); compare_all(w, "hidden", {.hidden = true});
}
void blockedByModal() {
    World w;
    auto opener = w.ui.addButton({.id = "opener", .label = u"Open"}).value.value();
    auto inside = w.ui.addButton({.id = "dialog.close", .label = u"Close"}).value.value();
    const auto dialog = w.ui.addDialog("dialog", {inside}, inside, opener, [](bool) {}).value.value();
    check(bool(dialog.open()), "modal: open dialog over every option component");
    arrange(layout::modal(w.root(), layout::component(inside), dialog), w.ui, webAppearance(), w.measure, 600, 1600, 1.f, w.scroll);
    compare_all(w, "blocked_by_modal", {.blocked = true});
}
void errors() {
    World w;
    for (const auto& subject : w.subjects) {
        // Only the radio group and dropdown support a rejected confirmed write. Lists have
        // no retained error, and page containers get their marks from members in page_errors.
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
    w.arrange_page(); compare_all(w, "component_error", {.field_errors = true});
}
void page_errors() {
    World w;
    for (const auto& subject : w.subjects) std::visit([&](auto h) {
        if constexpr (requires { h.addMember(OptionValue{"b"}, ComponentHandle{}); }) {
            auto member = w.ui.addTextField({.id = subject.id + ".member", .label = u"Member"}).value.value();
            check(bool(member.setError(u"Member error")) && bool(h.addMember({"b"}, member)),
                  ("page_error/" + subject.id + "/b: erroneous member attached to inactive page").c_str());
        }
    }, subject.handle);
    w.arrange_page(); compare_all(w, "page_member_error", {.page_error = true});
    for (const auto& subject : w.subjects) if (std::holds_alternative<TabsHandle>(subject.handle) ||
                                             std::holds_alternative<NavigationListHandle>(subject.handle)) {
        const auto node = w.ui.semantics().get({subject.id, OptionValue{"b"}});
        check(node->invalid && node->error.empty(), ("page_error/" + subject.id + "/b: mark without duplicate message").c_str());
    }
}
void dropdown_open() {
    World w; w.arrange_page();
    for (const auto& subject : w.subjects) if (auto h = std::get_if<DropdownHandle>(&subject.handle)) {
        check(bool(h->open()), "dropdown_open/dropdown/a: open popup");
        w.arrange_page();
        check(h->describe()->expanded, "dropdown_open/dropdown/a: parent expanded");
    }
    compare_all(w, "dropdown_open", {.dropdown_open = true});
}
void contextMenuOpen() {
    World w; w.arrange_page();
    for (const auto& subject : w.subjects)
        check(bool(w.ui.addContextMenu(subject.component(), {{.id = "inspect", .label = u"Inspect", .shortcutHint = {},
                   .action = [](const auto&) { return Result{}; }}})), ("context_menu/" + subject.id + ": register menu").c_str());
    for (const auto& subject : w.subjects) {
        check(std::visit([](auto h) { return bool(h.focus()); }, subject.handle), ("context_menu/" + subject.id + ": focus parent").c_str());
        compare_all(w, "before_context_menu", {.menus = true, .focused = subject.id});
        check(bool(w.ui.openContextMenu(subject.component())), ("context_menu/" + subject.id + ": open menu").c_str());
        w.arrange_page();
        compare_all(w, "context_menu_open", {.menus = true, .menu_open = true, .focused = subject.id});
        check(bool(w.ui.closeContextMenu()), ("context_menu/" + subject.id + ": close menu").c_str());
        compare_all(w, "context_menu_closed", {.menus = true, .focused = subject.id});
    }
}
}
int main() {
    const test::Case cases[] = {{"normal", normal}, {"disabled", disabled}, {"option_disabled", option_disabled},
                              {"hidden", hidden}, {"blocked_by_modal", blockedByModal}, {"errors", errors},
                              {"page_errors", page_errors}, {"dropdown_open", dropdown_open}, {"context_menu_open", contextMenuOpen}};
    return test::run("option_nodes", cases);
}
