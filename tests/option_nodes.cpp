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
        auto radio = ui.add_radio_group(config("radio"), items).value.value();
        radio.connect_source(source(std::optional{OptionValue{"a"}}));
        radio.connect_confirmed([](const auto&) { return Result{}; }); keep(radio);
        auto list = ui.add_list(config("list"), items).value.value();
        list.connect_source(source(std::optional{OptionValue{"a"}}));
        list.connect_immediate([](const auto&) { return Result{}; }); keep(list);
        auto dropdown = ui.add_dropdown(config("dropdown"), items).value.value();
        dropdown.connect_source(source(std::optional{OptionValue{"a"}}));
        dropdown.connect_confirmed([](const auto&) { return Result{}; }); keep(dropdown);
        keep(ui.add_tabs(config("tabs"), pages).value.value());
        keep(ui.add_navigation_list(config("navigation"), pages, {"a"}).value.value());
        for (const auto& subject : subjects) {
            for (auto kind : {LabelKind::name, LabelKind::description, LabelKind::error}) {
                auto label = ComponentConfig{.id = subject.id + ".label." + std::to_string(static_cast<int>(kind)),
                                             .value = u"Parent text", .target = subject.id, .label_kind = kind};
                check(bool(ui.add_label(label)), "option fixture: attach parent label");
            }
        }
    }
    layout::Node root() const {
        std::vector<layout::Node> nodes;
        for (const auto& subject : subjects) nodes.push_back(layout::component(subject.component()));
        return layout::column(std::move(nodes));
    }
    void arrange_page() { arrange(root(), ui, web_appearance(), measure, 600, 1600, 1.f, scroll); }
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
    bool page_error = false;
    std::string focused;
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
                if (subject.row() && state.menus) expected.verbs.push_back(Verb::context_menu);
            }
            check(static_cast<const CommonState&>(*node) == expected, (context + ": option-only CommonState").c_str());
            check(node->selected == (item.value.id == "a"), (context + ": own selection").c_str());
            check(node->labelled_by.empty() && node->described_by.empty() && !node->selection_required,
                  (context + ": no parent labels or selection requirement").c_str());
            check(node->hidden == parent->hidden && node->hidden == state.hidden &&
                  node->blocked_by_modal == parent->blocked_by_modal && node->blocked_by_modal == state.blocked,
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
}
int main() {
    const test::Case cases[] = {{"normal", normal}};
    return test::run("option_nodes", cases);
}
