// What every typed handle kind promises alike (#94), checked through the public API by one list of all handle kinds
// (`kinds` below) and generic drivers over it:
//   - lifetime: once the Ui is gone, every operation of every handle answers notFound with that operation's text of
//     today; a removed button's handle does the same while the Ui lives on.
//   - equality: two handles are equal when they control the same thing now. Two default (empty) handles are equal and
//     differ from every real one; a recreated button differs from the removed one; the same id in another Ui differs.
//     Handles of different kinds do not compare at all (a compile error, checked with static_assert).
// A new handle kind is one more World member, one more entry in `kinds` (with every public operation it has) and one
// more type in handleKinds::All (examples/consumer/handle_kinds.hpp, shared with the consumer example).
#include "lcars/core.hpp"
#include "check.hpp"
#include "../examples/consumer/handle_kinds.hpp"
#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
using namespace lcars;
using test::check;

// Every handle kind, for the compile-time checks: the one list the consumer example asserts too.
using HandleTypes = handleKinds::All;
template <class T, class... H> constexpr bool listed(std::tuple<H...>*) { return (std::is_same_v<T, H> || ...); }
template <class... A> constexpr bool everyAlternativeListed(std::variant<std::monostate, A...>*) {
    return (listed<A>(static_cast<HandleTypes*>(nullptr)) && ...);
}
// What a handle holds stays private: neither its user nor a class derived from it reaches the Ui, lifetime or id.
template <class H> struct Derived : H {
    static constexpr bool seesState() {
        return requires(const Derived& d) { d.ui_; } || requires(const Derived& d) { d.lifetime_; } || requires(const Derived& d) { d.id_; };
    }
};
template <class H> concept exposesState = requires(const H& h) { h.ui_; } || requires(const H& h) { h.lifetime_; } || requires(const H& h) { h.id_; };
template <class... H> constexpr bool statePrivate(std::tuple<H...>*) { return ((!exposesState<H> && !Derived<H>::seesState()) && ...); }

static_assert(std::tuple_size_v<HandleTypes> == 14);
static_assert(everyAlternativeListed(static_cast<ComponentHandle*>(nullptr)));
static_assert(handleKinds::equalityHolds, "decisions 2 and 4: every handle kind has operator==, and different kinds never compare");
// The shared predicate does fail a list with a kind that compares across kinds, or one without ==, so it is not vacuous.
struct WithoutEquality {};
static_assert(!handleKinds::compareWithinTheirKindOnly(static_cast<std::tuple<int, long>*>(nullptr)) &&
              !handleKinds::compareWithinTheirKindOnly(static_cast<std::tuple<TextFieldHandle, WithoutEquality>*>(nullptr)));
static_assert(handleKinds::constructionHolds, "a user makes only empty handles");
struct OpenConstruction { OpenConstruction() = default; OpenConstruction(detail::UiState*, std::weak_ptr<void>, std::string) {} };
static_assert(!handleKinds::madeOnlyByTheFramework(static_cast<std::tuple<OpenConstruction>*>(nullptr)));
static_assert(!std::equality_comparable_with<TextFieldHandle, CheckboxHandle>);
static_assert(!std::equality_comparable_with<TabsHandle, NavigationListHandle>);  // one implementation inside, still two kinds
static_assert(!std::equality_comparable_with<TextFieldHandle, LabelHandle>);
static_assert(std::equality_comparable<ComponentHandle> && std::equality_comparable<ConfirmField>);
static_assert(statePrivate(static_cast<HandleTypes*>(nullptr)));
// The two probes do see state that is reachable, so the check above is not vacuous.
struct OpenState { void* ui_ = nullptr; };
class InheritedState { protected: int id_ = 0; };
static_assert(exposesState<OpenState> && Derived<InheritedState>::seesState());

namespace {
std::function<std::function<void()>(std::function<void()>)> noUpdates() {
    return [](std::function<void()>) { return std::function<void()>([] {}); };
}
std::vector<OptionItem> twoOptions() { return {{{"a"}, u"Alpha", true}, {{"b"}, u"Beta", true}}; }
std::vector<TabPage> twoPages() { return {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Beta", true, {}}}; }
MenuItem command(std::string id, std::u16string label) { MenuItem item; item.id = std::move(id); item.label = std::move(label); return item; }
StyleChange bold() { StyleChange change; change.bold = true; return change; }

// One handle of every kind, all in one Ui. Every component exists and is wired as far as its kind allows, so an
// operation that answers notFound does so because the component (or the Ui) is gone, not for want of a source.
struct World {
    std::unique_ptr<Ui> ui = std::make_unique<Ui>();
    TextFieldHandle text;
    ButtonHandle button;
    LabelHandle label;
    CheckboxHandle checkbox;
    RadioGroupHandle radio;
    SliderHandle slider;
    ProgressHandle progress;
    DropdownHandle dropdown;
    TabsHandle tabs;
    NavigationListHandle navigation;
    ListHandle list;
    DialogHandle dialog;
    TooltipHandle tooltip;
    ContextMenuHandle menu;
    World() {
        auto accept = [](const auto&) { return Result{}; };
        text = made(ui->addTextField({.id = "text", .label = u"Text"}), "add_text_field");
        wired(text.connectSource({[] { return std::u16string(u"x"); }, noUpdates()}), "text.connect_source");
        wired(text.connectImmediate(accept), "text.connect_immediate");
        button = made(ui->addButton({.id = "button", .label = u"Button"}), "add_button");
        wired(button.bind([] { return Result{}; }), "button.bind");
        label = made(ui->addLabel({.id = "label", .value = u"Label"}), "add_label");
        checkbox = made(ui->addCheckbox({.id = "checkbox", .label = u"Check"}), "add_checkbox");
        wired(checkbox.connectSource({[] { return false; }, noUpdates()}), "checkbox.connect_source");
        wired(checkbox.connectImmediate(accept), "checkbox.connect_immediate");
        radio = made(ui->addRadioGroup({.id = "radio", .label = u"Radio"}, twoOptions()), "add_radio_group");
        wired(radio.connectSource({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, noUpdates()}), "radio.connect_source");
        wired(radio.connectImmediate(accept), "radio.connect_immediate");
        slider = made(ui->addSlider({.id = "slider", .label = u"Slider"}, {0, 100, 10}), "add_slider");
        wired(slider.connectSource({[] { return 20; }, noUpdates()}), "slider.connect_source");
        wired(slider.connectImmediate(accept), "slider.connect_immediate");
        progress = made(ui->addProgress({.id = "progress", .label = u"Progress"}), "add_progress");
        wired(progress.connectSource({[] { return 20; }, noUpdates()}), "progress.connect_source");
        dropdown = made(ui->addDropdown({.id = "dropdown", .label = u"Drop"}, twoOptions()), "add_dropdown");
        wired(dropdown.connectSource({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, noUpdates()}), "dropdown.connect_source");
        wired(dropdown.connectImmediate(accept), "dropdown.connect_immediate");
        tabs = made(ui->addTabs({.id = "tabs", .label = u"Tabs"}, twoPages()), "add_tabs");
        navigation = made(ui->addNavigationList({.id = "navigation", .label = u"Nav"}, twoPages(), {"a"}),
                          "add_navigation_list");
        list = made(ui->addList({.id = "list", .label = u"List"}, twoOptions()), "add_list");
        wired(list.connectSource({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, noUpdates()}), "list.connect_source");
        wired(list.connectImmediate([](const Change<ListHandle>&) { return Result{}; }), "list.connect_immediate");
        // The dialog has its own member and fallback, so the world's button stays free to be removed.
        auto inside = made(ui->addButton({.id = "dialog.ok", .label = u"OK"}), "add_button dialog.ok");
        auto fallback = made(ui->addButton({.id = "dialog.fallback", .label = u"Back"}), "add_button dialog.fallback");
        dialog = made(ui->addDialog("dialog", {inside}, inside, fallback, [](bool) {}), "add_dialog");
        tooltip = made(ui->addTooltip("tooltip", label, u"Tip"), "add_tooltip");
        menu = made(ui->addContextMenu(text, {command("app.one", u"One")}), "add_context_menu");
    }
private:
    // Every wiring step must succeed, so an operation that later answers notFound does so because its component or
    // the Ui is gone, not because the world was built short.
    static void wired(const Result& result, const char* step) { check(result.status == Status::ok, (std::string("World: ") + step).c_str()); }
    template <class H> static H made(const Query<H>& created, const char* step) {
        check(created.status == Status::ok && created.value.has_value(), (std::string("World: ") + step).c_str());
        return *created.value;
    }
};

using Answer = std::pair<Status, std::u16string>;
template <class R> Answer answer(const R& r) { return {r.status, r.message}; }
// One public operation of a handle, called with harmless arguments. `gone` overrides the kind's notFound text for
// the operations whose text differs today (decision 7: the texts stay exactly as they are).
struct Op {
    std::string name;
    std::function<Answer()> run;
    const char16_t* gone = nullptr;
};
#define LCARS_OP(handle, call, description) Op{description, [handle] { return answer(handle.call); }}

std::vector<Op> textOps(const World& w) {
    auto h = w.text;
    return {LCARS_OP(h, setValue(u"y"), "set_value(u\"y\")"), LCARS_OP(h, undo(), "undo()"), LCARS_OP(h, redo(), "redo()"), LCARS_OP(h, select(0, 1), "select(0, 1)"), LCARS_OP(h, copy(), "copy()"),
            LCARS_OP(h, cut(), "cut()"), LCARS_OP(h, paste(u"p"), "paste(u\"p\")"), LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, onBlur([] {}), "on_blur([] {})"), LCARS_OP(h, setError(u"e"), "set_error(u\"e\")"),
            LCARS_OP(h, resetToSource(), "reset_to_source()"), LCARS_OP(h, enable(true), "enable(true)"), LCARS_OP(h, readOnly(false), "read_only(false)"),
            LCARS_OP(h, setVisibleLines(2), "set_visible_lines(2)"), LCARS_OP(h, visibleLines(), "visible_lines()"), LCARS_OP(h, setScrollLine(0), "set_scroll_line(0)"), LCARS_OP(h, scrollLine(), "scroll_line()"),
            LCARS_OP(h, setTextAlign(TextAlign::center), "set_text_align(TextAlign::center)"), LCARS_OP(h, textAlign(), "text_align()"),
            LCARS_OP(h, setJustifyMethod(JustifyMethod::spaces), "set_justify_method(JustifyMethod::spaces)"), LCARS_OP(h, justifyMethod(), "justify_method()"),
            LCARS_OP(h, setLineSpacing(1.5), "set_line_spacing(1.5)"), LCARS_OP(h, lineSpacing(), "line_spacing()"), LCARS_OP(h, setParagraphSpacing(4), "set_paragraph_spacing(4)"), LCARS_OP(h, paragraphSpacing(), "paragraph_spacing()"),
            LCARS_OP(h, setWordSegmentation(WordSegmentation::scriptRun), "set_word_segmentation(WordSegmentation::script_run)"), LCARS_OP(h, wordSegmentation(), "word_segmentation()"),
            LCARS_OP(h, wordSegmentationInUse(), "word_segmentation_in_use()"), LCARS_OP(h, wordAt(0), "word_at(0)"), LCARS_OP(h, paragraphAt(0), "paragraph_at(0)"),
            LCARS_OP(h, nextWordStop(0), "next_word_stop(0)"), LCARS_OP(h, previousWordStop(0), "previous_word_stop(0)"), LCARS_OP(h, selectWord(0), "select_word(0)"), LCARS_OP(h, selectParagraph(0), "select_paragraph(0)"),
            LCARS_OP(h, doubleClick(0), "double_click(0)"), LCARS_OP(h, tripleClick(0), "triple_click(0)"),
            LCARS_OP(h, connectSource({[] { return std::u16string(); }, noUpdates()}), "connect_source({[] { return std::u16string(); }, no_updates()})"),
            LCARS_OP(h, connectImmediate([](const Change<TextFieldHandle>&) { return Result{}; }), "connect_immediate([](const Change<TextFieldHandle>&) { return Result{}; })"),
            LCARS_OP(h, connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; }), "connect_confirmed([](const Change<TextFieldHandle>&) { return Result{}; })"),
            LCARS_OP(h, connectRichSource({[] { return RichText{}; }, noUpdates()}), "connect_rich_source({[] { return RichText{}; }, no_updates()})"),
            LCARS_OP(h, connectRichImmediate([](const Change<TextFieldHandle, RichText>&) { return Result{}; }), "connect_rich_immediate([](const Change<TextFieldHandle, RichText>&) { return Result{}; })"),
            LCARS_OP(h, connectRichConfirmed([](const Change<TextFieldHandle, RichText>&) { return Result{}; }), "connect_rich_confirmed([](const Change<TextFieldHandle, RichText>&) { return Result{}; })"),
            LCARS_OP(h, setRichValue(RichText{u"r", {}}), "set_rich_value(RichText{u\"r\", {}})"), LCARS_OP(h, richValue(), "rich_value()"), LCARS_OP(h, setStyle(0, 1, bold()), "set_style(0, 1, bold())"),
            LCARS_OP(h, toggleStyle(StyleAttribute::bold), "toggle_style(StyleAttribute::bold)"), LCARS_OP(h, applyStyle(bold()), "apply_style(bold())"),
            LCARS_OP(h, typingStyle(), "typing_style()"), LCARS_OP(h, setTypingStyle(TextStyle{}), "set_typing_style(TextStyle{})"), LCARS_OP(h, setColors({}), "set_colors({})"), LCARS_OP(h, colors(), "colors()"),
            LCARS_OP(h, confirm(), "confirm()"), LCARS_OP(h, sourceConflict(), "source_conflict()"), LCARS_OP(h, useLatest(), "use_latest()"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()"), LCARS_OP(h, draft(), "draft()")};
}
std::vector<Op> buttonOps(const World& w) {
    auto h = w.button;
    return {LCARS_OP(h, invoke(), "invoke()"), LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, bind([] { return Result{}; }), "bind([] { return Result{}; })"), LCARS_OP(h, enable(true), "enable(true)"),
            LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, executing(), "executing()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> labelOps(const World& w) {
    auto h = w.label;
    return {LCARS_OP(h, connectSource([] { return std::u16string(u"l"); }), "connect_source([] { return std::u16string(u\"l\"); })"),
            LCARS_OP(h, setTextAlign(TextAlign::distribute), "set_text_align(TextAlign::distribute)"), LCARS_OP(h, textAlign(), "text_align()"),
            LCARS_OP(h, setJustifyMethod(JustifyMethod::perCharacter), "set_justify_method(JustifyMethod::per_character)"), LCARS_OP(h, justifyMethod(), "justify_method()"),
            LCARS_OP(h, setLineSpacing(2.0), "set_line_spacing(2.0)"), LCARS_OP(h, lineSpacing(), "line_spacing()"), LCARS_OP(h, setParagraphSpacing(8), "set_paragraph_spacing(8)"), LCARS_OP(h, paragraphSpacing(), "paragraph_spacing()"),
            LCARS_OP(h, setWritingMode(WritingMode::vertical), "set_writing_mode(WritingMode::vertical)"), LCARS_OP(h, writingMode(), "writing_mode()"),
            LCARS_OP(h, setVerticalDigits(2), "set_vertical_digits(2)"), LCARS_OP(h, verticalDigits(), "vertical_digits()"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> checkboxOps(const World& w) {
    auto h = w.checkbox;
    return {LCARS_OP(h, toggle(), "toggle()"), LCARS_OP(h, useLatest(), "use_latest()"), LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, enable(true), "enable(true)"),
            LCARS_OP(h, connectSource({[] { return true; }, noUpdates()}), "connect_source({[] { return true; }, no_updates()})"),
            LCARS_OP(h, connectImmediate([](const Change<CheckboxHandle>&) { return Result{}; }), "connect_immediate([](const Change<CheckboxHandle>&) { return Result{}; })"),
            LCARS_OP(h, connectConfirmed([](const Change<CheckboxHandle>&) { return Result{}; }), "connect_confirmed([](const Change<CheckboxHandle>&) { return Result{}; })"),
            LCARS_OP(h, confirm(), "confirm()"), LCARS_OP(h, draft(), "draft()"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> radioOps(const World& w) {
    auto h = w.radio;
    return {LCARS_OP(h, select({"b"}), "select({\"b\"})"), LCARS_OP(h, useLatest(), "use_latest()"), LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, enable(true), "enable(true)"),
            LCARS_OP(h, enableOption({"b"}, false), "enable_option({\"b\"}, false)"),
            LCARS_OP(h, connectSource({[] { return std::optional<OptionValue>{}; }, noUpdates()}), "connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})"),
            LCARS_OP(h, connectImmediate([](const Change<RadioGroupHandle>&) { return Result{}; }), "connect_immediate([](const Change<RadioGroupHandle>&) { return Result{}; })"),
            LCARS_OP(h, connectConfirmed([](const Change<RadioGroupHandle>&) { return Result{}; }), "connect_confirmed([](const Change<RadioGroupHandle>&) { return Result{}; })"),
            LCARS_OP(h, confirm(), "confirm()"), LCARS_OP(h, draft(), "draft()"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> sliderOps(const World& w) {
    auto h = w.slider;
    return {LCARS_OP(h, onBlur([] {}), "on_blur([] {})"), LCARS_OP(h, setValue(30), "set_value(30)"), LCARS_OP(h, useLatest(), "use_latest()"), LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, enable(true), "enable(true)"),
            LCARS_OP(h, connectSource({[] { return 40; }, noUpdates()}), "connect_source({[] { return 40; }, no_updates()})"),
            LCARS_OP(h, connectImmediate([](const Change<SliderHandle>&) { return Result{}; }), "connect_immediate([](const Change<SliderHandle>&) { return Result{}; })"),
            LCARS_OP(h, connectConfirmed([](const Change<SliderHandle>&) { return Result{}; }), "connect_confirmed([](const Change<SliderHandle>&) { return Result{}; })"),
            LCARS_OP(h, confirm(), "confirm()"), LCARS_OP(h, draft(), "draft()"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> progressOps(const World& w) {
    auto h = w.progress;
    return {LCARS_OP(h, connectSource({[] { return 50; }, noUpdates()}), "connect_source({[] { return 50; }, no_updates()})"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> dropdownOps(const World& w) {
    auto h = w.dropdown;
    return {LCARS_OP(h, open(), "open()"), LCARS_OP(h, cancel(), "cancel()"), LCARS_OP(h, select({"b"}), "select({\"b\"})"), LCARS_OP(h, setItems(twoOptions()), "set_items(two_options())"),
            LCARS_OP(h, useLatest(), "use_latest()"), LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, enable(true), "enable(true)"),
            LCARS_OP(h, connectSource({[] { return std::optional<OptionValue>{}; }, noUpdates()}), "connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})"),
            LCARS_OP(h, connectImmediate([](const Change<DropdownHandle>&) { return Result{}; }), "connect_immediate([](const Change<DropdownHandle>&) { return Result{}; })"),
            LCARS_OP(h, connectConfirmed([](const Change<DropdownHandle>&) { return Result{}; }), "connect_confirmed([](const Change<DropdownHandle>&) { return Result{}; })"),
            LCARS_OP(h, confirm(), "confirm()"), LCARS_OP(h, draft(), "draft()"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> tabsOps(const World& w) {
    auto h = w.tabs;
    return {LCARS_OP(h, activate({"b"}), "activate({\"b\"})"), LCARS_OP(h, enablePage({"b"}, false), "enable_page({\"b\"}, false)"), LCARS_OP(h, addMember({"a"}, ComponentHandle{}), "add_member({\"a\"}, ComponentHandle{})"),
            LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, active(), "active()"), LCARS_OP(h, describe(), "describe()"), LCARS_OP(h, onPageChange([](OptionValue, OptionValue) {}), "on_page_change([](OptionValue, OptionValue) {})")};
}
std::vector<Op> navigationOps(const World& w) {
    auto h = w.navigation;
    return {LCARS_OP(h, select({"b"}), "select({\"b\"})"), LCARS_OP(h, enablePage({"b"}, false), "enable_page({\"b\"}, false)"), LCARS_OP(h, addMember({"a"}, ComponentHandle{}), "add_member({\"a\"}, ComponentHandle{})"),
            LCARS_OP(h, focus(), "focus()"), LCARS_OP(h, selected(), "selected()"), LCARS_OP(h, describe(), "describe()"), LCARS_OP(h, onPageChange([](OptionValue, OptionValue) {}), "on_page_change([](OptionValue, OptionValue) {})")};
}
std::vector<Op> listOps(const World& w) {
    auto h = w.list;
    auto focusRow = LCARS_OP(h, focus(OptionValue{"a"}), "focus(OptionValue{\"a\"})");
    focusRow.gone = u"";  // this one operation of today answers notFound without text
    return {LCARS_OP(h, select({"b"}), "select({\"b\"})"), LCARS_OP(h, setItems(twoOptions()), "set_items(two_options())"), LCARS_OP(h, focus(), "focus()"), std::move(focusRow),
            LCARS_OP(h, enable(true), "enable(true)"), LCARS_OP(h, connectSource({[] { return std::optional<OptionValue>{}; }, noUpdates()}), "connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})"),
            LCARS_OP(h, connectImmediate([](const Change<ListHandle>&) { return Result{}; }), "connect_immediate([](const Change<ListHandle>&) { return Result{}; })"), LCARS_OP(h, remove(), "remove()"), LCARS_OP(h, describe(), "describe()")};
}
std::vector<Op> dialogOps(const World& w) {
    auto h = w.dialog;
    const ComponentHandle opener{w.text};  // a real opener, so the live call is not refused for want of one
    return {Op{"open(opener)", [h, opener] { return answer(h.open(opener)); }}, LCARS_OP(h, open(), "open()"), LCARS_OP(h, settle(true), "settle(true)"), LCARS_OP(h, isOpen(), "is_open()")};
}
std::vector<Op> tooltipOps(const World& w) {
    auto h = w.tooltip;
    return {LCARS_OP(h, visible(), "visible()")};
}
std::vector<Op> menuOps(const World& w) {
    auto h = w.menu;
    return {LCARS_OP(h, setMode(MenuMode::replace), "set_mode(MenuMode::replace)"), LCARS_OP(h, setItems({}), "set_items({})"), LCARS_OP(h, addItem(command("app.two", u"Two")), "add_item(command(\"app.two\", u\"Two\"))"),
            LCARS_OP(h, removeItem("app.one"), "remove_item(\"app.one\")"), LCARS_OP(h, updateItem(command("app.one", u"Uno")), "update_item(command(\"app.one\", u\"Uno\"))"),
            LCARS_OP(h, enableItem("app.one", false), "enable_item(\"app.one\", false)"), LCARS_OP(h, items(), "items()")};
}
#undef LCARS_OP

// The one list of handle kinds. `gone` is the notFound text the kind's operations give today once the Ui is gone.
// The comparisons are filled in from the World member, so a kind is listed once and every driver sees it.
struct HandleKind {
    const char* name;
    const char16_t* gone;
    Result (*remove)(World&); // null for handle kinds outside owner removal
    std::vector<Op> (*operations)(const World&);
    bool (*equal)(const World&, const World&);  // the kind's handle in one world == the one in the other
    bool (*copiesEqual)(const World&);         // a copy-constructed and a copy-assigned handle == the world's
    bool (*emptyEquals)(const World&);         // a default handle of the kind == the world's
    bool (*emptiesEqual)();                    // two default handles of the kind are equal
};
template <auto member> HandleKind kind(const char* name, const char16_t* gone, std::vector<Op> (*operations)(const World&)) {
    using H = std::remove_cvref_t<decltype(std::declval<World&>().*member)>;
    Result (*remove)(World&) = nullptr;
    if constexpr (requires(H h) { h.remove(); }) remove = [](World& w) { return (w.*member).remove(); };
    return {name, gone, remove, operations,
            [](const World& a, const World& b) { return a.*member == b.*member; },
            [](const World& w) {
                const H constructed(w.*member);
                H assigned;
                assigned = w.*member;
                return constructed == w.*member && w.*member == constructed && assigned == w.*member && assigned == constructed;
            },
            [](const World& w) { return H{} == w.*member; },
            [] { return H{} == H{}; }};
}
const std::vector<HandleKind>& kinds() {
    static const std::vector<HandleKind> list{
        kind<&World::text>("TextFieldHandle", u"Component no longer exists", textOps),
        kind<&World::button>("ButtonHandle", u"Component no longer exists", buttonOps),
        kind<&World::label>("LabelHandle", u"Component no longer exists", labelOps),
        kind<&World::checkbox>("CheckboxHandle", u"Component no longer exists", checkboxOps),
        kind<&World::radio>("RadioGroupHandle", u"Component no longer exists", radioOps),
        kind<&World::slider>("SliderHandle", u"Component no longer exists", sliderOps),
        kind<&World::progress>("ProgressHandle", u"Component no longer exists", progressOps),
        kind<&World::dropdown>("DropdownHandle", u"Component no longer exists", dropdownOps),
        kind<&World::tabs>("TabsHandle", u"Component no longer exists", tabsOps),
        kind<&World::navigation>("NavigationListHandle", u"Component no longer exists", navigationOps),
        kind<&World::list>("ListHandle", u"Component no longer exists", listOps),
        kind<&World::dialog>("DialogHandle", u"UI no longer exists", dialogOps),
        kind<&World::tooltip>("TooltipHandle", u"UI no longer exists", tooltipOps),
        kind<&World::menu>("ContextMenuHandle", u"Component no longer exists", menuOps),
    };
    return list;
}
std::string narrow(std::u16string_view text) { return {text.begin(), text.end()}; }  // the texts checked here are ASCII
// The generic lifetime driver: each operation answers notFound with its text of today.
void expectGone(const HandleKind& kind, const std::vector<Op>& operations) {
    for (const auto& op : operations) {
        const auto [status, message] = op.run();
        const std::u16string want = op.gone ? op.gone : kind.gone;
        const auto what = std::string(kind.name) + "::" + op.name + " answers not_found \"" + narrow(want) + "\", got status " +
                          std::to_string(static_cast<int>(status)) + " \"" + narrow(message) + "\"";
        check(status == Status::notFound && message == want, what.c_str());
    }
}

// The live baseline the lifetime drivers stand on: while the component and its Ui exist, no listed operation answers
// notFound, so a handle that always answered notFound fails here. Each operation runs in a World of its own, as some
// change what follows them (a button's remove, a menu's setItems).
void expectLive(const HandleKind& kind) {
    const auto count = kind.operations(World{}).size();
    for (std::size_t i = 0; i < count; ++i) {
        World world;
        const auto op = kind.operations(world)[i];
        const auto [status, message] = op.run();
        const auto what = std::string(kind.name) + "::" + op.name + " works while its component lives, got status " +
                          std::to_string(static_cast<int>(status)) + " \"" + narrow(message) + "\"";
        check(status != Status::notFound, what.c_str());
    }
}

void listCoversEveryKind() {
    check(kinds().size() == std::tuple_size_v<HandleTypes>, "the list names all 14 handle kinds");
    // Every component handle alternative is in the list (the remaining three are DialogHandle, TooltipHandle, ContextMenuHandle).
    check(std::variant_size_v<ComponentHandle> - 1 + 3 == kinds().size(), "component handles plus dialog, tooltip and menu");
    for (const auto& kind : kinds()) {
        World world;
        check(!kind.operations(world).empty(), "every kind lists its operations");
    }
}

void everyOperationAfterTheUiEndsIsNotFound() {
    for (const auto& kind : kinds()) expectLive(kind);
    World world;
    std::vector<std::vector<Op>> operations;
    for (const auto& kind : kinds()) operations.push_back(kind.operations(world));
    world.ui.reset();
    for (std::size_t i = 0; i < kinds().size(); ++i) expectGone(kinds()[i], operations[i]);
}

void everyOperationOfARemovedComponentIsNotFound() {
    for (const auto& kind : kinds()) {
        if (!kind.remove) continue;
        expectLive(kind);
        World world;
        auto operations = kind.operations(world);
        auto trigger = world.ui->addButton({.id = "remove.trigger", .label = u"Remove"}).value.value();
        trigger.bind([&] {
            check(kind.remove(world).status == Status::ok, "Removal is accepted");
            expectGone(kind, operations);
            return Result{};
        });
        check(bool(trigger.invoke()), "Every operation refuses a pending component inside the current callback");
        expectGone(kind, operations);
    }
}

void expect(bool condition, const HandleKind& kind, const char* expectation) {
    const auto text = std::string(kind.name) + ": " + expectation;
    check(condition, text.c_str());
}

void emptyHandlesEqualEachOtherAndNoRealOne() {
    World world;
    for (const auto& kind : kinds()) {
        expect(kind.emptiesEqual(), kind, "two default handles are equal");
        expect(!kind.emptyEquals(world), kind, "a default handle differs from a real one");
    }
}

void aHandleEqualsItsCopy() {
    World world;
    for (const auto& kind : kinds()) expect(kind.copiesEqual(world), kind, "a handle equals its copies");
}

void theSameIdInAnotherUiDiffers() {
    World one, other;
    for (const auto& kind : kinds()) expect(!kind.equal(one, other), kind, "the same id in another Ui is another handle");
}

void aHandleTheFrameworkHandsBackEqualsTheOneFromCreation() {
    Ui ui;
    auto checkbox = ui.addCheckbox({.id = "check", .label = u"Check"}).value.value();
    checkbox.connectSource({[] { return false; }, noUpdates()});
    std::optional<CheckboxHandle> written;
    checkbox.connectImmediate([&](const Change<CheckboxHandle>& change) { written = change.field; return Result{}; });
    check(checkbox.toggle().status == Status::ok && written, "the write ran");
    check(*written == checkbox, "the change names the same checkbox");
    check(ComponentHandle{*written} == ComponentHandle{checkbox}, "and so does a component handle of it");
    check(ConfirmField{*written} == ConfirmField{checkbox}, "and a confirmation field of it");
    check(!(ComponentHandle{checkbox} == ComponentHandle{}), "an empty component handle is no checkbox");
}

void aRecreatedButtonIsAnotherHandle() {
    Ui ui;
    auto old = ui.addButton({.id = "save", .label = u"Save"}).value.value();
    check(old.remove().status == Status::ok, "the button is removed");
    auto recreated = ui.addButton({.id = "save", .label = u"Save"}).value.value();
    check(!(old == recreated) && old != recreated, "the recreated button is not the removed one");
    check(old.invoke().status == Status::notFound, "the old handle still controls nothing");
    check(recreated.focus().status == Status::ok, "the new one controls the new button");
    check(ComponentHandle{old} != ComponentHandle{recreated}, "as component handles too");
}

// A context menu handle names its target (same Ui, same id): a menu attached again to a recreated target is the same
// menu handle, as the old handle controls the new attachment today.
void aMenuAttachedAgainIsTheSameHandle() {
    Ui ui;
    auto target = ui.addButton({.id = "target", .label = u"Target"}).value.value();
    auto old = ui.addContextMenu(target, {command("app.one", u"One")}).value.value();
    check(target.remove().status == Status::ok, "the target is removed");
    check(old.items().status == Status::notFound, "the old menu is gone with it");
    auto recreated = ui.addButton({.id = "target", .label = u"Target"}).value.value();
    auto again = ui.addContextMenu(recreated, {command("app.two", u"Two")}).value.value();
    check(old == again, "the menu attached again to the same id is the same handle");
    auto items = old.items();
    check(items && items->size() == 1 && items->front().id == "app.two", "and the old handle controls the new attachment");
}

// A tooltip handle lives as long as its source component: a tooltip on a recreated button is not reached through the
// old handle (it answers notFound today), so the two handles do not control the same thing and are not equal.
void aTooltipOnARecreatedButtonIsAnotherHandle() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"Source"}).value.value();
    auto old = ui.addTooltip("tip", source, u"Tip").value.value();
    check(source.remove().status == Status::ok, "the source is removed");
    auto recreated = ui.addButton({.id = "source", .label = u"Source"}).value.value();
    auto again = ui.addTooltip("tip", recreated, u"Tip").value.value();
    auto stale = old.visible();
    check(stale.status == Status::notFound && stale.message == u"UI no longer exists", "the old handle controls nothing");
    check(again.visible().status == Status::ok, "the new one controls the new tooltip");
    check(!(old == again), "so they are different handles");
}
}

int main() {
    static constexpr test::Case cases[]{
        {"the list covers every handle kind", listCoversEveryKind},
        {"every operation of every handle answers not_found after the Ui ends", everyOperationAfterTheUiEndsIsNotFound},
        {"every operation of a removed component's handle answers not_found", everyOperationOfARemovedComponentIsNotFound},
        {"default handles equal each other and no real handle", emptyHandlesEqualEachOtherAndNoRealOne},
        {"a handle equals its copy", aHandleEqualsItsCopy},
        {"the same id in another Ui is another handle", theSameIdInAnotherUiDiffers},
        {"a handle handed back by the framework equals the one from creation", aHandleTheFrameworkHandsBackEqualsTheOneFromCreation},
        {"a recreated button is another handle", aRecreatedButtonIsAnotherHandle},
        {"a menu attached again to the same target is the same handle", aMenuAttachedAgainIsTheSameHandle},
        {"a tooltip on a recreated button is another handle", aTooltipOnARecreatedButtonIsAnotherHandle},
    };
    return test::run("handles", cases);
}
