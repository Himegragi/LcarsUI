// Component types are told apart only by what the registry says about them (#93). This suite pins what a built-in
// type does today (key policy, semantic roles, verbs and operations, debug names) through public seams, and runs a
// registered test-only type through the same dispatch points. `--print` writes the observed tables instead of checking them.
#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "check.hpp"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace lcars;
using test::check;

namespace {
bool printing = false;

TextSource text_source(std::shared_ptr<std::u16string> value) {
    return {[value] { return *value; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
BoolSource bool_source() {
    return {[] { return false; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
NumericSource numeric_source() {
    return {[] { return 20; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
OptionSource option_source() {
    return {[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
std::vector<RadioOption> two_options() { return {{{"a"}, u"Alpha", true}, {{"b"}, u"Beta", true}}; }
std::vector<TabPage> two_pages() { return {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Beta", true, {}}}; }

// One component of every built-in type, wired as far as its type allows (a source and an immediate write).
struct Everything {
    Ui ui;
    std::shared_ptr<std::u16string> text = std::make_shared<std::u16string>(u"x");
    std::vector<std::pair<const char*, ComponentHandle>> components;
    explicit Everything(bool confirmed) {
        auto accepted = [](const auto&) { return Result{}; };
        auto text_field = ui.add_text({.id = "text", .kind = Kind::text, .label = u"Text"}).value.value();
        text_field.connect_source(text_source(text));
        if (confirmed) text_field.connect_confirmed(accepted); else text_field.connect_immediate(accepted);
        components.push_back({"text", text_field});
        auto button = ui.add_button({.id = "button", .kind = Kind::button, .label = u"Button"}).value.value();
        button.bind([] { return Result{}; });
        components.push_back({"button", button});
        components.push_back({"label", ui.add_label({.id = "label", .kind = Kind::label, .value = u"Label"}).value.value()});
        auto checkbox = ui.add_checkbox({.id = "checkbox", .kind = Kind::checkbox, .label = u"Check"}).value.value();
        checkbox.connect_source(bool_source());
        if (confirmed) checkbox.connect_confirmed(accepted); else checkbox.connect_immediate(accepted);
        components.push_back({"checkbox", checkbox});
        auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group, .label = u"Radio"}, two_options()).value.value();
        radio.connect_source(option_source());
        if (confirmed) radio.connect_confirmed(accepted); else radio.connect_immediate(accepted);
        components.push_back({"radio", radio});
        auto slider = ui.add_slider({.id = "slider", .kind = Kind::slider, .label = u"Slider"}, {0, 100, 10}).value.value();
        slider.connect_source(numeric_source());
        if (confirmed) slider.connect_confirmed(accepted); else slider.connect_immediate(accepted);
        components.push_back({"slider", slider});
        auto dropdown = ui.add_dropdown({.id = "dropdown", .kind = Kind::dropdown, .label = u"Drop"}, two_options()).value.value();
        dropdown.connect_source(option_source());
        if (confirmed) dropdown.connect_confirmed(accepted); else dropdown.connect_immediate(accepted);
        components.push_back({"dropdown", dropdown});
        auto list = ui.add_list({.id = "list", .kind = Kind::list, .label = u"List"}, two_options()).value.value();
        list.connect_source(option_source());
        list.connect_immediate([](std::optional<OptionValue>) { return Result{}; });
        components.push_back({"list", list});
        components.push_back({"tabs", ui.add_tabs({.id = "tabs", .kind = Kind::tabs, .label = u"Tabs"}, two_pages()).value.value()});
        auto progress = ui.add_progress({.id = "progress", .kind = Kind::progress, .label = u"Progress"}).value.value();
        progress.connect_source(numeric_source());
        components.push_back({"progress", progress});
        components.push_back({"navigation_list", ui.add_navigation_list({.id = "navigation_list", .kind = Kind::navigation_list, .label = u"Nav"}, two_pages(), {"a"}).value.value()});
    }
};

std::string name_of(Role role) {
    static const char* names[] = {"text", "button", "label", "checkbox", "radio_group", "radio_option", "slider", "dropdown", "dropdown_option", "list",
                                  "list_item", "tabs", "tab_item", "progress", "dialog", "tooltip", "menu", "menu_item", "menu_separator"};
    return names[static_cast<int>(role)];
}
std::string name_of(Verb verb) {
    static const char* names[] = {"invoke", "toggle", "set_value", "select", "open", "cancel", "focus", "undo", "redo", "copy", "cut", "paste", "confirm", "scroll",
                                  "set_text_align", "set_line_spacing", "set_paragraph_spacing", "context_menu", "set_writing_mode", "set_vertical_digits", "hover",
                                  "set_style", "set_typing_style", "set_word_segmentation", "select_word", "select_paragraph", "double_click", "triple_click", "set_justify_method"};
    return names[static_cast<int>(verb)];
}
std::string name_of(Operation op) {
    static const char* names[] = {"focus", "activate", "select", "copy", "edit", "cut", "paste", "undo", "redo", "scroll", "style"};
    return names[static_cast<int>(op)];
}
std::string describe_node(const SemanticNode& node, const Semantics* semantics) {
    std::ostringstream out;
    out << name_of(node.role) << (node.selection_required ? " selection_required" : "") << " verbs=";
    for (auto verb : node.verbs) out << name_of(verb) << ',';
    if (semantics) { out << " ops="; for (auto op : semantics->operations) out << name_of(op) << ','; }
    return out.str();
}

// The semantic layer of every built-in type: role, verbs and operations, for the container and its first item.
std::vector<std::string> semantic_table(bool confirmed) {
    Everything all(confirmed);
    std::vector<std::string> lines;
    for (const auto& [name, handle] : all.components) {
        auto node = all.ui.semantics().get({name});
        auto semantics = detail::access(all.ui).describe(name);
        lines.push_back(std::string(name) + ": " + (node ? describe_node(*node, semantics ? &*semantics : nullptr) : "none"));
        if (semantics && !semantics->options.empty()) {
            auto item = all.ui.semantics().get({name, semantics->options[0].value});
            lines.push_back(std::string(name) + "[0]: " + (item ? describe_node(*item, nullptr) : "none"));
        }
    }
    return lines;
}

// Which keys a focused component takes for itself: a scoped shortcut on the key fires only when the component declines it,
// so the keys listed are the ones that did not fire it. Labels and progress bars cannot hold focus, so they take every key here.
std::vector<std::string> key_table() {
    struct Press { const char* name; KeyPress key; };
    const Press presses[] = {{"enter", {.key = Key::enter}}, {"space", {.key = Key::space}}, {"left", {.key = Key::left}}, {"right", {.key = Key::right}},
        {"up", {.key = Key::up}}, {"down", {.key = Key::down}}, {"home", {.key = Key::home}}, {"end", {.key = Key::end}},
        {"page_down", {.key = Key::page_down}}, {"backspace", {.key = Key::backspace}}, {"ctrl+a", {.ctrl = true, .letter = u'A'}},
        {"ctrl+left", {.key = Key::left, .ctrl = true}}};
    std::vector<std::string> lines;
    Everything names(false);
    std::vector<std::pair<std::string, bool>> rows;  // a dropdown also takes the arrows while its list is open
    for (const auto& [name, unused] : names.components) rows.push_back({name, false});
    rows.push_back({"dropdown", true});
    for (const auto& [name, popup] : rows) {
        std::string line = name + (popup ? "(open):" : ":");
        for (const auto& press : presses) {
            Everything all(false);
            ComponentHandle subject;
            for (const auto& [candidate, handle] : all.components) if (name == candidate) subject = handle;
            auto sink = all.ui.add_button({.id = "sink", .kind = Kind::button, .label = u"Sink"}).value.value();
            int fired = 0;
            sink.bind([&] { ++fired; return Result{}; });
            Shortcut chord{.key = press.key.key, .letter = press.key.letter, .ctrl = press.key.ctrl};
            if (!all.ui.add_shortcut(chord, sink, subject)) throw std::runtime_error("shortcut was not registered");
            detail::access(all.ui).focus(name);
            if (popup) std::get<DropdownHandle>(subject).open();
            detail::access(all.ui).key_down(press.key);
            if (!fired) line += std::string(" ") + press.name;
        }
        lines.push_back(line);
    }
    return lines;
}

const char* const golden_keys[] = {
    "text: left right home end backspace ctrl+a ctrl+left",
    "button: enter space",
    "label: enter space left right up down home end page_down backspace ctrl+a ctrl+left",
    "checkbox: enter space",
    "radio: left right up down home end ctrl+left",
    "slider: left right up down home end ctrl+left",
    "dropdown: enter space",
    "list: space left right up down home end ctrl+left",
    "tabs: enter space left right ctrl+left",
    "progress: enter space left right up down home end page_down backspace ctrl+a ctrl+left",
    "navigation_list: up down home end",
    "dropdown(open): enter space left right up down home end ctrl+left",
    nullptr };
const char* const golden_semantics[] = {
    "text: text verbs=context_menu,focus,set_value,select,copy,cut,paste,set_text_align,set_justify_method,set_line_spacing,set_paragraph_spacing,set_word_segmentation,select_word,select_paragraph,double_click,triple_click, ops=focus,select,copy,edit,cut,paste,",
    "button: button verbs=focus,invoke, ops=focus,activate,",
    "label: label verbs=set_writing_mode,set_vertical_digits,set_text_align,set_justify_method,set_line_spacing,set_paragraph_spacing, ops=",
    "checkbox: checkbox verbs=focus,toggle, ops=focus,activate,",
    "radio: radio_group verbs=focus, ops=focus,select,",
    "radio[0]: radio_option verbs=select,",
    "slider: slider verbs=focus,set_value, ops=focus,edit,",
    "dropdown: dropdown verbs=focus,open,cancel, ops=focus,select,",
    "dropdown[0]: dropdown_option verbs=select,",
    "list: list verbs=focus, ops=focus,select,",
    "list[0]: list_item verbs=select,focus,",
    "tabs: tabs selection_required verbs=focus, ops=focus,activate,",
    "tabs[0]: tab_item selection_required verbs=select,",
    "progress: progress verbs= ops=",
    "navigation_list: list selection_required verbs=focus, ops=focus,",
    "navigation_list[0]: list_item selection_required verbs=select,",
    nullptr };
const char* const golden_confirmed[] = {
    "text: text verbs=context_menu,focus,set_value,select,copy,cut,paste,set_text_align,set_justify_method,set_line_spacing,set_paragraph_spacing,set_word_segmentation,select_word,select_paragraph,double_click,triple_click,confirm, ops=focus,select,copy,edit,cut,paste,",
    "button: button verbs=focus,invoke, ops=focus,activate,",
    "label: label verbs=set_writing_mode,set_vertical_digits,set_text_align,set_justify_method,set_line_spacing,set_paragraph_spacing, ops=",
    "checkbox: checkbox verbs=focus,toggle,confirm, ops=focus,activate,",
    "radio: radio_group verbs=focus,confirm, ops=focus,select,",
    "radio[0]: radio_option verbs=select,",
    "slider: slider verbs=focus,set_value,confirm, ops=focus,edit,",
    "dropdown: dropdown verbs=focus,open,cancel,confirm, ops=focus,select,",
    "dropdown[0]: dropdown_option verbs=select,",
    "list: list verbs=focus, ops=focus,select,",
    "list[0]: list_item verbs=select,focus,",
    "tabs: tabs selection_required verbs=focus, ops=focus,activate,",
    "tabs[0]: tab_item selection_required verbs=select,",
    "progress: progress verbs= ops=",
    "navigation_list: list selection_required verbs=focus, ops=focus,",
    "navigation_list[0]: list_item selection_required verbs=select,",
    nullptr };

template <std::size_t N>
void compare(const std::vector<std::string>& observed, const char* const (&expected)[N], const char* what) {
    if (printing) { std::cout << "== " << what << '\n'; for (const auto& line : observed) std::cout << line << '\n'; return; }
    check(observed.size() == N - 1, what);
    for (std::size_t i = 0; i + 1 < N && i < observed.size(); ++i) {
        if (observed[i] != expected[i]) throw std::runtime_error(std::string(what) + ": expected [" + expected[i] + "] observed [" + observed[i] + "]");
    }
}

void key_policy_of_every_built_in_type() { compare(key_table(), golden_keys, "keys taken by each type (its scoped shortcut stays silent)"); }
void semantics_of_every_built_in_type() { compare(semantic_table(false), golden_semantics, "semantic nodes, immediate writes"); }
void confirm_verb_of_every_built_in_type() { compare(semantic_table(true), golden_confirmed, "semantic nodes, confirmed writes"); }

#ifndef NDEBUG
// The diagnostic that names an existing component's type when its ID is taken again.
void debug_names_in_diagnostics() {
    Everything all(false);
    for (const auto& [name, unused] : all.components) {
        const auto before = all.ui.diagnostics().size();
        check(all.ui.add_label({.id = name, .kind = Kind::label}).status == Status::invalid_value, "duplicate id is refused");
        check(all.ui.diagnostics().size() == before + 1, "refusal is diagnosed");
        const auto& message = all.ui.diagnostics().back();
        const std::string expected = std::string_view(name) == "radio" ? "radio_group" : name;
        check(message.ends_with("existing component type: " + expected), "diagnostic names the type");
    }
}
#endif
}

int main(int argc, char** argv) {
    printing = argc > 1 && std::strcmp(argv[1], "--print") == 0;
    const test::Case cases[] = {
        {"key policy of every built-in type", key_policy_of_every_built_in_type},
        {"semantics of every built-in type", semantics_of_every_built_in_type},
        {"confirm verb of every built-in type", confirm_verb_of_every_built_in_type},
#ifndef NDEBUG
        {"debug names in diagnostics", debug_names_in_diagnostics},
#endif
    };
    return test::run("component_registry", cases);
}
