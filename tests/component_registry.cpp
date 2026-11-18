// Component types are told apart only by what the registry says about them (#93). This suite pins what a built-in
// type does today (key policy, semantic roles, verbs, debug names) through public seams, and runs a
// registered test-only type through the same dispatch points. `--print` writes the observed tables instead of checking them.
#include "isoduo/core.hpp"
#include "isoduo/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "fake_measure.hpp"
#include "check.hpp"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <sstream>
using namespace isoduo;
using test::check;

namespace {
bool printing = false;

Source<std::u16string> textSource(std::shared_ptr<std::u16string> value) {
    return {[value] { return *value; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
Source<bool> boolSource() {
    return {[] { return false; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
Source<int> numericSource() {
    return {[] { return 20; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
Source<std::optional<OptionValue>> optionSource() {
    return {[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, [](std::function<void()>) { return std::function<void()>([] {}); }};
}
std::vector<OptionItem> twoOptions() { return {{{"a"}, u"Alpha", true}, {{"b"}, u"Beta", true}}; }
std::vector<TabPage> twoPages() { return {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Beta", true, {}}}; }

// One component of every built-in type, wired as far as its type allows (a source and an immediate write).
struct Everything {
    Ui ui;
    std::shared_ptr<std::u16string> text = std::make_shared<std::u16string>(u"x");
    std::vector<std::pair<const char*, ComponentHandle>> components;
    explicit Everything(bool confirmed) {
        auto accepted = [](const auto&) { return Result{}; };
        auto textField = ui.addTextField({.id = "text", .label = u"Text"}).value.value();
        textField.connectSource(textSource(text));
        if (confirmed) textField.connectConfirmed(accepted); else textField.connectImmediate(accepted);
        components.push_back({"text", textField});
        auto button = ui.addButton({.id = "button", .label = u"Button"}).value.value();
        button.bind([] { return Result{}; });
        components.push_back({"button", button});
        components.push_back({"label", ui.addLabel({.id = "label", .value = u"Label"}).value.value()});
        auto checkbox = ui.addCheckbox({.id = "checkbox", .label = u"Check"}).value.value();
        checkbox.connectSource(boolSource());
        if (confirmed) checkbox.connectConfirmed(accepted); else checkbox.connectImmediate(accepted);
        components.push_back({"checkbox", checkbox});
        auto radio = ui.addRadioGroup({.id = "radio", .label = u"Radio"}, twoOptions()).value.value();
        radio.connectSource(optionSource());
        if (confirmed) radio.connectConfirmed(accepted); else radio.connectImmediate(accepted);
        components.push_back({"radio", radio});
        auto slider = ui.addSlider({.id = "slider", .label = u"Slider"}, {0, 100, 10}).value.value();
        slider.connectSource(numericSource());
        if (confirmed) slider.connectConfirmed(accepted); else slider.connectImmediate(accepted);
        components.push_back({"slider", slider});
        auto dropdown = ui.addDropdown({.id = "dropdown", .label = u"Drop"}, twoOptions()).value.value();
        dropdown.connectSource(optionSource());
        if (confirmed) dropdown.connectConfirmed(accepted); else dropdown.connectImmediate(accepted);
        components.push_back({"dropdown", dropdown});
        auto list = ui.addList({.id = "list", .label = u"List"}, twoOptions()).value.value();
        list.connectSource(optionSource());
        list.connectImmediate([](const Change<ListHandle>&) { return Result{}; });
        components.push_back({"list", list});
        components.push_back({"tabs", ui.addTabs({.id = "tabs", .label = u"Tabs"}, twoPages()).value.value()});
        auto progress = ui.addProgress({.id = "progress", .label = u"Progress"}).value.value();
        progress.connectSource(numericSource());
        components.push_back({"progress", progress});
        components.push_back({"navigation_list", ui.addNavigationList({.id = "navigation_list", .label = u"Nav"}, twoPages(), {"a"}).value.value()});
    }
};

// Each enumerator is named explicitly, so inserting a value into an enumeration cannot silently mislabel the tables.
template <class E> struct Named { E value; const char* name; };
template <class E, std::size_t N> std::string nameIn(const Named<E> (&names)[N], E value) {
    for (const auto& entry : names) if (entry.value == value) return entry.name;
    throw std::runtime_error("an enumerator has no name in the registry tables");
}
std::string nameOf(Role role) {
    static const Named<Role> names[] = {
        {Role::textField, "text"}, {Role::button, "button"}, {Role::label, "label"}, {Role::checkbox, "checkbox"}, {Role::radioGroup, "radio_group"},
        {Role::radioOption, "radio_option"}, {Role::slider, "slider"}, {Role::dropdown, "dropdown"}, {Role::dropdownOption, "dropdown_option"},
        {Role::list, "list"}, {Role::listItem, "list_item"}, {Role::tabs, "tabs"}, {Role::tabItem, "tab_item"}, {Role::progress, "progress"},
        {Role::dialog, "dialog"}, {Role::tooltip, "tooltip"}, {Role::menu, "menu"}, {Role::menuItem, "menu_item"}, {Role::menuSeparator, "menu_separator"}};
    return nameIn(names, role);
}
std::string nameOf(Verb verb) {
    static const Named<Verb> names[] = {
        {Verb::invoke, "invoke"}, {Verb::toggle, "toggle"}, {Verb::setValue, "set_value"}, {Verb::select, "select"}, {Verb::open, "open"},
        {Verb::cancel, "cancel"}, {Verb::focus, "focus"}, {Verb::undo, "undo"}, {Verb::redo, "redo"}, {Verb::copy, "copy"}, {Verb::cut, "cut"},
        {Verb::paste, "paste"}, {Verb::confirm, "confirm"}, {Verb::scroll, "scroll"}, {Verb::setTextAlign, "set_text_align"},
        {Verb::setLineSpacing, "set_line_spacing"}, {Verb::setParagraphSpacing, "set_paragraph_spacing"}, {Verb::contextMenu, "context_menu"},
        {Verb::setWritingMode, "set_writing_mode"}, {Verb::setVerticalDigits, "set_vertical_digits"}, {Verb::hover, "hover"},
        {Verb::setStyle, "set_style"}, {Verb::setTypingStyle, "set_typing_style"}, {Verb::setWordSegmentation, "set_word_segmentation"},
        {Verb::selectWord, "select_word"}, {Verb::selectParagraph, "select_paragraph"}, {Verb::doubleClick, "double_click"},
        {Verb::tripleClick, "triple_click"}, {Verb::setJustifyMethod, "set_justify_method"}};
    return nameIn(names, verb);
}
std::string describeNode(const NodeSnapshot& node) {
    std::ostringstream out;
    out << nameOf(node.role) << (node.selectionRequired ? " selection_required" : "") << " verbs=";
    for (auto verb : node.verbs) out << nameOf(verb) << ',';
    return out.str();
}

// The semantic layer of every built-in type: role, verbs, for the container and its first item.
std::vector<std::string> semanticTable(bool confirmed) {
    Everything all(confirmed);
    std::vector<std::string> lines;
    for (const auto& [name, handle] : all.components) {
        auto node = all.ui.semantics().get({name});
        auto semantics = detail::access(all.ui).describe(name);
        lines.push_back(std::string(name) + ": " + (node ? describeNode(*node) : "none"));
        if (semantics && !semantics->items.empty()) {
            auto item = all.ui.semantics().get({name, semantics->items[0].value});
            lines.push_back(std::string(name) + "[0]: " + (item ? describeNode(*item) : "none"));
        }
    }
    return lines;
}

// Which keys a focused component takes for itself: a scoped shortcut on the key fires only when the component declines it,
// so the keys listed are the ones that did not fire it. Labels and progress bars cannot hold focus, so they take every key here.
std::vector<std::string> keyTable() {
    struct Press { const char* name; KeyPress key; };
    const Press presses[] = {{"enter", {.key = Key::enter}}, {"space", {.key = Key::space}}, {"left", {.key = Key::left}}, {"right", {.key = Key::right}},
        {"up", {.key = Key::up}}, {"down", {.key = Key::down}}, {"home", {.key = Key::home}}, {"end", {.key = Key::end}},
        {"page_down", {.key = Key::pageDown}}, {"backspace", {.key = Key::backspace}}, {"ctrl+a", {.ctrl = true, .letter = u'A'}},
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
            auto sink = all.ui.addButton({.id = "sink", .label = u"Sink"}).value.value();
            int fired = 0;
            sink.bind([&] { ++fired; return Result{}; });
            Shortcut chord{.key = press.key.key, .letter = press.key.letter, .ctrl = press.key.ctrl};
            if (!all.ui.addShortcut(chord, sink, subject)) throw std::runtime_error("shortcut was not registered");
            detail::access(all.ui).focus(name);
            if (popup) std::get<DropdownHandle>(subject).open();
            detail::access(all.ui).keyDown(press.key);
            if (!fired) line += std::string(" ") + press.name;
        }
        lines.push_back(line);
    }
    return lines;
}

const std::vector<std::string> goldenKeys = {
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
    "dropdown(open): enter space left right up down home end ctrl+left"};
const std::vector<std::string> goldenSemantics = {
    "text: text verbs=context_menu,focus,set_value,select,copy,cut,paste,set_text_align,set_justify_method,set_line_spacing,set_paragraph_spacing,set_word_segmentation,select_word,select_paragraph,double_click,triple_click,",
    "button: button verbs=focus,invoke,",
    "label: label verbs=set_writing_mode,set_vertical_digits,set_text_align,set_justify_method,set_line_spacing,set_paragraph_spacing,",
    "checkbox: checkbox verbs=focus,toggle,",
    "radio: radio_group verbs=focus,",
    "radio[0]: radio_option verbs=select,",
    "slider: slider verbs=focus,set_value,",
    "dropdown: dropdown verbs=focus,open,cancel,",
    "dropdown[0]: dropdown_option verbs=select,",
    "list: list verbs=focus,",
    "list[0]: list_item verbs=select,focus,",
    "tabs: tabs selection_required verbs=focus,",
    "tabs[0]: tab_item verbs=select,focus,",
    "progress: progress verbs=",
    "navigation_list: list selection_required verbs=focus,",
    "navigation_list[0]: list_item verbs=select,focus,"};
// With a confirmed write instead of an immediate one, the five types that take a write timing also list the confirm verb (after
// their other verbs); every other row is what the immediate table says.
std::vector<std::string> confirmedFrom(const std::vector<std::string>& immediate) {
    static const char* const confirming[] = {"text", "checkbox", "radio", "slider", "dropdown"};
    std::vector<std::string> rows;
    for (auto row : immediate) {
        for (const char* name : confirming)
            if (row.starts_with(std::string(name) + ": ")) { row.insert(row.size(), "confirm,"); break; }
        rows.push_back(std::move(row));
    }
    return rows;
}

void compare(const std::vector<std::string>& observed, const std::vector<std::string>& expected, const char* what) {
    if (printing) { std::cout << "== " << what << '\n'; for (const auto& line : observed) std::cout << line << '\n'; return; }
    check(observed.size() == expected.size(), what);
    for (std::size_t i = 0; i < expected.size() && i < observed.size(); ++i) {
        if (observed[i] != expected[i]) throw std::runtime_error(std::string(what) + ": expected [" + expected[i] + "] observed [" + observed[i] + "]");
    }
}

void keyPolicyOfEveryBuiltInType() { compare(keyTable(), goldenKeys, "keys taken by each type (its scoped shortcut stays silent)"); }
void semanticsOfEveryBuiltInType() { compare(semanticTable(false), goldenSemantics, "semantic nodes, immediate writes"); }
void confirmVerbOfEveryBuiltInType() { compare(semanticTable(true), confirmedFrom(goldenSemantics), "semantic nodes, confirmed writes"); }


// A component type that exists only in a test. Everything it does comes from its own code and its registration: the
// press and release, the keys it takes, its place in the semantic tree and the verbs it answers.
class Stepper final : public detail::ComponentType {
public:
    mutable int value = 7, keyPresses = 0, presses = 0, releases = 0;
    mutable Key lastKey = Key::other;
    mutable std::optional<std::size_t> pressedOption, releasedPressedOption, releasedOption;
    std::string_view name() const override { return "stepper"; }
    Role role() const override { return Role::slider; }
    ComponentHandle handle(detail::UiState& ui, const detail::Component& control) const override { return ui.typedHandle<ButtonHandle>(control); }
    void arrange(detail::ComponentLayout& layout) const override { layout.placement.bounds = {0, 0, 80, 30}; }
    void paint(detail::ComponentPaint& paint) const override { paint.fill(paint.placement.bounds, {17, 91, 173}); }
    bool handlesKey(const detail::UiState&, const detail::Component&, KeyPress key) const override { return key.key == Key::up || key.key == Key::down; }
    Result keyDown(detail::UiState&, KeyPress key) const override { ++keyPresses; lastKey = key.key; return {}; }
    bool ignoresPress(const detail::UiState&, const detail::Component&, std::optional<std::size_t> option) const override { return option == 5u; }
    void pointerDown(detail::UiState& ui, const detail::Component& control, std::optional<std::size_t> option) const override {
        ++presses; pressedOption = option; ui.armPointer(control.id, option);
    }
    Result pointerUp(detail::UiState&, const detail::PointerRelease& release) const override {
        ++releases; releasedPressedOption = release.pressedOption; releasedOption = release.option; return {};
    }
    void describe(const detail::UiState&, const detail::Component&, ComponentSnapshot& s) const override { s.numericValue = value; s.range = SliderRange{0, 10, 1}; }
    void verbs(const detail::VerbContext& context, std::vector<Verb>& out) const override {
        ComponentType::verbs(context, out);
        if (!context.item) out.push_back(Verb::setValue);
    }
    std::optional<Result> perform(const detail::PerformContext& context) const override {
        if (context.verb != Verb::setValue) return std::nullopt;
        if (const auto* number = std::get_if<int>(&context.argument)) { value = *number; return Result{}; }
        return context.invalidArgument();
    }
};
constexpr auto stepperKind = static_cast<ComponentKind>(101);

void aRegisteredTypeTakesPartInKeysPointerAndSemantics() {
    Stepper stepper;
    detail::ScopedComponentType registration(stepperKind, stepper);
    Ui ui; auto& host = detail::access(ui);
    host.seedTestControls({{stepperKind, {.id = "stepper", .label = u"Stepper"}}});
    check(detail::componentType(stepperKind).name() == "stepper", "the type names itself");
    // ComponentSnapshot: its role, its description, the verbs it offers.
    auto node = ui.semantics().get({"stepper"});
    check(node && node->role == Role::slider && node->numericValue == 7 && node->range && node->range->maximum == 10, "role and description come from the type");
    check(node && std::find(node->verbs.begin(), node->verbs.end(), Verb::setValue) != node->verbs.end(), "the type's verb is offered");
    // The type extends the shared focus verb with its own setValue verb.
    const auto described = host.describe("stepper");
    check(described && described->verbs == std::vector<Verb>{Verb::focus, Verb::setValue}, "the verbs come from the type");
    check(bool(ui.semantics().perform({"stepper"}, Verb::setValue, 9)) && stepper.value == 9 && ui.semantics().get({"stepper"})->numericValue == 9, "the type performs its verb");
    check(ui.semantics().perform({"stepper"}, Verb::setValue, std::u16string(u"nine")).status == Status::invalidValue, "a wrong argument is the common answer");
    // Keys: it takes Up and Down, and leaves Left to the application's shortcut.
    int fired = 0;
    auto sink = ui.addButton({.id = "sink", .label = u"Sink"}).value.value();
    sink.bind([&] { ++fired; return Result{}; });
    check(bool(ui.addShortcut({.key = Key::left}, sink, host.layoutHandle("stepper"))), "shortcut registered");
    check(bool(host.focus("stepper")), "the type can be focused");
    host.keyDown({.key = Key::up});
    check(stepper.keyPresses == 1 && stepper.lastKey == Key::up, "a key the type takes reaches it");
    host.keyDown({.key = Key::left});
    check(stepper.keyPresses == 1 && fired == 1, "a key the type leaves goes to the shortcut");
    // Pointer: press and release reach the type with the rows; a press it ignores changes nothing.
    check(bool(host.pointerDown("stepper", 2u)) && stepper.presses == 1 && stepper.pressedOption == 2u, "the press reaches the type");
    check(bool(host.pointerUp("stepper", 2u)) && stepper.releases == 1 && stepper.releasedPressedOption == 2u && stepper.releasedOption == 2u, "the release reaches the type");
    check(bool(host.focus("sink")) && host.focused() == "sink", "focus moved away");
    host.pointerDown("stepper", 5u);
    host.pointerUp("stepper", 5u);
    check(stepper.presses == 1 && stepper.releases == 1 && host.focused() == "sink", "an ignored press neither focuses nor arms");
}

// The one expectation of what each built-in type calls itself: the id of its component in Everything, its ComponentKind and its name. The
// registry table and the diagnostics (which name the type of an ID that is taken again) are both checked against it.
struct BuiltIn { const char* component; ComponentKind kind; const char* name; };
const BuiltIn builtInTypes[] = {
    {"text", ComponentKind::textField, "text"}, {"button", ComponentKind::button, "button"}, {"label", ComponentKind::label, "label"}, {"checkbox", ComponentKind::checkbox, "checkbox"},
    {"radio", ComponentKind::radioGroup, "radio_group"}, {"slider", ComponentKind::slider, "slider"}, {"dropdown", ComponentKind::dropdown, "dropdown"}, {"list", ComponentKind::list, "list"},
    {"tabs", ComponentKind::tabs, "tabs"}, {"progress", ComponentKind::progress, "progress"}, {"navigation_list", ComponentKind::navigationList, "navigation_list"}};

// Debug names come from the registry, not from the order of the ComponentKind enumeration: each built-in type names itself.
void builtInTypesNameThemselves() {
    for (const auto& item : builtInTypes) check(detail::componentType(item.kind).name() == item.name, item.name);
    check(detail::findComponentType(static_cast<ComponentKind>(99)) == nullptr, "a Kind without a type is not invented");
}

#ifndef NDEBUG
// The diagnostic that names an existing component's type when its ID is taken again.
void debugNamesInDiagnostics() {
    Everything all(false);
    check(all.components.size() == std::size(builtInTypes), "every built-in type has a component here");
    for (const auto& [name, unused] : all.components) {
        const auto expected = std::find_if(std::begin(builtInTypes), std::end(builtInTypes), [&](const BuiltIn& item) { return std::string_view(name) == item.component; });
        check(expected != std::end(builtInTypes), name);
        if (expected == std::end(builtInTypes)) continue;
        const auto before = all.ui.diagnostics().size();
        check(all.ui.addLabel({.id = name}).status == Status::invalidValue, "duplicate id is refused");
        check(all.ui.diagnostics().size() == before + 1, "refusal is diagnosed");
        const auto& message = all.ui.diagnostics().back();
        check(message.ends_with(std::string("existing component type: ") + expected->name), "diagnostic names the type");
    }
}
#endif
}

int main(int argc, char** argv) {
    printing = argc > 1 && std::strcmp(argv[1], "--print") == 0;
    const test::Case cases[] = {
        {"key policy of every built-in type", keyPolicyOfEveryBuiltInType},
        {"semantics of every built-in type", semanticsOfEveryBuiltInType},
        {"confirm verb of every built-in type", confirmVerbOfEveryBuiltInType},
        {"built-in types name themselves", builtInTypesNameThemselves},
        {"a registered type takes part in keys, pointer and semantics", aRegisteredTypeTakesPartInKeysPointerAndSemantics},
#ifndef NDEBUG
        {"debug names in diagnostics", debugNamesInDiagnostics},
#endif
    };
    return test::run("component_registry", cases);
}
