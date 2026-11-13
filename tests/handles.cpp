// What every typed handle kind promises alike (#94), checked through the public API by one list of all handle kinds
// (`kinds` below) and generic drivers over it:
//   - lifetime: once the Ui is gone, every operation of every handle answers not_found with that operation's text of
//     today; a removed button's handle does the same while the Ui lives on.
// A new handle kind is one more World member and one more entry in `kinds` (with every public operation it has).
#include "lcars/core.hpp"
#include "check.hpp"
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
using namespace lcars;
using test::check;

namespace {
std::function<std::function<void()>(std::function<void()>)> no_updates() {
    return [](std::function<void()>) { return std::function<void()>([] {}); };
}
std::vector<RadioOption> two_options() { return {{{"a"}, u"Alpha", true}, {{"b"}, u"Beta", true}}; }
std::vector<TabPage> two_pages() { return {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Beta", true, {}}}; }
MenuItem command(std::string id, std::u16string label) { MenuItem item; item.id = std::move(id); item.label = std::move(label); return item; }
StyleChange bold() { StyleChange change; change.bold = true; return change; }

// One handle of every kind, all in one Ui. Every component exists and is wired as far as its kind allows, so an
// operation that answers not_found does so because the component (or the Ui) is gone, not for want of a source.
struct World {
    std::unique_ptr<Ui> ui = std::make_unique<Ui>();
    TextHandle text;
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
        text = ui->add_text({.id = "text", .kind = Kind::text, .label = u"Text"}).value.value();
        text.connect_source({[] { return std::u16string(u"x"); }, no_updates()});
        text.connect_immediate(accept);
        button = ui->add_button({.id = "button", .kind = Kind::button, .label = u"Button"}).value.value();
        button.bind([] { return Result{}; });
        label = ui->add_label({.id = "label", .kind = Kind::label, .value = u"Label"}).value.value();
        checkbox = ui->add_checkbox({.id = "checkbox", .kind = Kind::checkbox, .label = u"Check"}).value.value();
        checkbox.connect_source({[] { return false; }, no_updates()});
        checkbox.connect_immediate(accept);
        radio = ui->add_radio_group({.id = "radio", .kind = Kind::radio_group, .label = u"Radio"}, two_options()).value.value();
        radio.connect_source({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, no_updates()});
        radio.connect_immediate(accept);
        slider = ui->add_slider({.id = "slider", .kind = Kind::slider, .label = u"Slider"}, {0, 100, 10}).value.value();
        slider.connect_source({[] { return 20; }, no_updates()});
        slider.connect_immediate(accept);
        progress = ui->add_progress({.id = "progress", .kind = Kind::progress, .label = u"Progress"}).value.value();
        progress.connect_source({[] { return 20; }, no_updates()});
        dropdown = ui->add_dropdown({.id = "dropdown", .kind = Kind::dropdown, .label = u"Drop"}, two_options()).value.value();
        dropdown.connect_source({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, no_updates()});
        dropdown.connect_immediate(accept);
        tabs = ui->add_tabs({.id = "tabs", .kind = Kind::tabs, .label = u"Tabs"}, two_pages()).value.value();
        navigation = ui->add_navigation_list({.id = "navigation", .kind = Kind::navigation_list, .label = u"Nav"}, two_pages(), {"a"}).value.value();
        list = ui->add_list({.id = "list", .kind = Kind::list, .label = u"List"}, two_options()).value.value();
        list.connect_source({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, no_updates()});
        list.connect_immediate([](std::optional<OptionValue>) { return Result{}; });
        // The dialog has its own member and fallback, so the world's button stays free to be removed.
        auto inside = ui->add_button({.id = "dialog.ok", .kind = Kind::button, .label = u"OK"}).value.value();
        auto fallback = ui->add_button({.id = "dialog.fallback", .kind = Kind::button, .label = u"Back"}).value.value();
        dialog = ui->add_dialog("dialog", {inside}, inside, fallback, [](bool) {}).value.value();
        tooltip = ui->add_tooltip("tooltip", label, u"Tip").value.value();
        menu = ui->add_context_menu(text, {command("app.one", u"One")}).value.value();
    }
};

using Answer = std::pair<Status, std::u16string>;
template <class R> Answer answer(const R& r) { return {r.status, r.message}; }
// One public operation of a handle, called with harmless arguments. `gone` overrides the kind's not_found text for
// the operations whose text differs today (decision 7: the texts stay exactly as they are).
struct Op {
    std::string name;
    std::function<Answer()> run;
    const char16_t* gone = nullptr;
};
#define LCARS_OP(handle, call) Op{#call, [handle] { return answer(handle.call); }}

std::vector<Op> text_ops(const World& w) {
    auto h = w.text;
    return {LCARS_OP(h, set_value(u"y")), LCARS_OP(h, undo()), LCARS_OP(h, redo()), LCARS_OP(h, select(0, 1)), LCARS_OP(h, copy()),
            LCARS_OP(h, cut()), LCARS_OP(h, paste(u"p")), LCARS_OP(h, focus()), LCARS_OP(h, on_blur([] {})), LCARS_OP(h, set_error(u"e")),
            LCARS_OP(h, reset_to_source()), LCARS_OP(h, enable(true)), LCARS_OP(h, read_only(false)),
            LCARS_OP(h, set_visible_lines(2)), LCARS_OP(h, visible_lines()), LCARS_OP(h, set_scroll_line(0)), LCARS_OP(h, scroll_line()),
            LCARS_OP(h, set_text_align(TextAlign::center)), LCARS_OP(h, text_align()),
            LCARS_OP(h, set_justify_method(JustifyMethod::spaces)), LCARS_OP(h, justify_method()),
            LCARS_OP(h, set_line_spacing(1.5)), LCARS_OP(h, line_spacing()), LCARS_OP(h, set_paragraph_spacing(4)), LCARS_OP(h, paragraph_spacing()),
            LCARS_OP(h, set_word_segmentation(WordSegmentation::script_run)), LCARS_OP(h, word_segmentation()),
            LCARS_OP(h, word_segmentation_in_use()), LCARS_OP(h, word_at(0)), LCARS_OP(h, paragraph_at(0)),
            LCARS_OP(h, next_word_stop(0)), LCARS_OP(h, previous_word_stop(0)), LCARS_OP(h, select_word(0)), LCARS_OP(h, select_paragraph(0)),
            LCARS_OP(h, double_click(0)), LCARS_OP(h, triple_click(0)),
            LCARS_OP(h, connect_source({[] { return std::u16string(); }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const TextChange&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const TextChange&) { return Result{}; })),
            LCARS_OP(h, connect_rich_source({[] { return RichText{}; }, no_updates()})),
            LCARS_OP(h, connect_rich_immediate([](const RichTextChange&) { return Result{}; })),
            LCARS_OP(h, connect_rich_confirmed([](const RichTextChange&) { return Result{}; })),
            LCARS_OP(h, set_rich_value(RichText{u"r", {}})), LCARS_OP(h, rich_value()), LCARS_OP(h, set_style(0, 1, bold())),
            LCARS_OP(h, toggle_style(StyleAttribute::bold)), LCARS_OP(h, apply_style(bold())),
            LCARS_OP(h, typing_style()), LCARS_OP(h, set_typing_style(TextStyle{})), LCARS_OP(h, set_colors({})), LCARS_OP(h, colors()),
            LCARS_OP(h, confirm()), LCARS_OP(h, source_conflict()), LCARS_OP(h, use_latest()), LCARS_OP(h, describe()), LCARS_OP(h, draft())};
}
std::vector<Op> button_ops(const World& w) {
    auto h = w.button;
    return {LCARS_OP(h, invoke()), LCARS_OP(h, focus()), LCARS_OP(h, bind([] { return Result{}; })), LCARS_OP(h, enable(true)),
            LCARS_OP(h, remove()), LCARS_OP(h, executing()), LCARS_OP(h, describe())};
}
std::vector<Op> label_ops(const World& w) {
    auto h = w.label;
    return {LCARS_OP(h, connect_source([] { return std::u16string(u"l"); })),
            LCARS_OP(h, set_text_align(TextAlign::distribute)), LCARS_OP(h, text_align()),
            LCARS_OP(h, set_justify_method(JustifyMethod::per_character)), LCARS_OP(h, justify_method()),
            LCARS_OP(h, set_line_spacing(2.0)), LCARS_OP(h, line_spacing()), LCARS_OP(h, set_paragraph_spacing(8)), LCARS_OP(h, paragraph_spacing()),
            LCARS_OP(h, set_writing_mode(WritingMode::vertical)), LCARS_OP(h, writing_mode()),
            LCARS_OP(h, set_vertical_digits(2)), LCARS_OP(h, vertical_digits()), LCARS_OP(h, describe())};
}
std::vector<Op> checkbox_ops(const World& w) {
    auto h = w.checkbox;
    return {LCARS_OP(h, toggle()), LCARS_OP(h, use_latest()), LCARS_OP(h, focus()), LCARS_OP(h, enable(true)),
            LCARS_OP(h, connect_source({[] { return true; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const BoolChange&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const BoolChange&) { return Result{}; })),
            LCARS_OP(h, confirm()), LCARS_OP(h, draft()), LCARS_OP(h, describe())};
}
std::vector<Op> radio_ops(const World& w) {
    auto h = w.radio;
    return {LCARS_OP(h, select({"b"})), LCARS_OP(h, use_latest()), LCARS_OP(h, focus()), LCARS_OP(h, enable(true)),
            LCARS_OP(h, enable_option({"b"}, false)),
            LCARS_OP(h, connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const OptionChange&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const OptionChange&) { return Result{}; })),
            LCARS_OP(h, confirm()), LCARS_OP(h, draft()), LCARS_OP(h, describe())};
}
std::vector<Op> slider_ops(const World& w) {
    auto h = w.slider;
    return {LCARS_OP(h, on_blur([] {})), LCARS_OP(h, set_value(30)), LCARS_OP(h, use_latest()), LCARS_OP(h, focus()), LCARS_OP(h, enable(true)),
            LCARS_OP(h, connect_source({[] { return 40; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const NumericChange&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const NumericChange&) { return Result{}; })),
            LCARS_OP(h, confirm()), LCARS_OP(h, draft()), LCARS_OP(h, describe())};
}
std::vector<Op> progress_ops(const World& w) {
    auto h = w.progress;
    return {LCARS_OP(h, connect_source({[] { return 50; }, no_updates()})), LCARS_OP(h, describe())};
}
std::vector<Op> dropdown_ops(const World& w) {
    auto h = w.dropdown;
    return {LCARS_OP(h, open()), LCARS_OP(h, cancel()), LCARS_OP(h, select({"b"})), LCARS_OP(h, set_options(two_options())),
            LCARS_OP(h, use_latest()), LCARS_OP(h, focus()), LCARS_OP(h, enable(true)),
            LCARS_OP(h, connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const DropdownChange&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const DropdownChange&) { return Result{}; })),
            LCARS_OP(h, confirm()), LCARS_OP(h, draft()), LCARS_OP(h, describe())};
}
std::vector<Op> tabs_ops(const World& w) {
    auto h = w.tabs;
    return {LCARS_OP(h, activate({"b"})), LCARS_OP(h, enable_page({"b"}, false)), LCARS_OP(h, add_member({"a"}, ComponentHandle{})),
            LCARS_OP(h, focus()), LCARS_OP(h, active()), LCARS_OP(h, describe()), LCARS_OP(h, on_page_change([](OptionValue, OptionValue) {}))};
}
std::vector<Op> navigation_ops(const World& w) {
    auto h = w.navigation;
    return {LCARS_OP(h, select({"b"})), LCARS_OP(h, enable_page({"b"}, false)), LCARS_OP(h, add_member({"a"}, ComponentHandle{})),
            LCARS_OP(h, focus()), LCARS_OP(h, selected()), LCARS_OP(h, describe()), LCARS_OP(h, on_page_change([](OptionValue, OptionValue) {}))};
}
std::vector<Op> list_ops(const World& w) {
    auto h = w.list;
    auto focus_row = LCARS_OP(h, focus(OptionValue{"a"}));
    focus_row.gone = u"";  // this one operation of today answers not_found without text
    return {LCARS_OP(h, select({"b"})), LCARS_OP(h, set_items(two_options())), LCARS_OP(h, focus()), std::move(focus_row),
            LCARS_OP(h, enable(true)), LCARS_OP(h, connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](std::optional<OptionValue>) { return Result{}; })), LCARS_OP(h, describe())};
}
std::vector<Op> dialog_ops(const World& w) {
    auto h = w.dialog;
    return {LCARS_OP(h, open(ComponentHandle{})), LCARS_OP(h, open()), LCARS_OP(h, settle(true)), LCARS_OP(h, is_open())};
}
std::vector<Op> tooltip_ops(const World& w) {
    auto h = w.tooltip;
    return {LCARS_OP(h, visible())};
}
std::vector<Op> menu_ops(const World& w) {
    auto h = w.menu;
    return {LCARS_OP(h, set_mode(MenuMode::replace)), LCARS_OP(h, set_items({})), LCARS_OP(h, add_item(command("app.two", u"Two"))),
            LCARS_OP(h, remove_item("app.one")), LCARS_OP(h, update_item(command("app.one", u"Uno"))),
            LCARS_OP(h, enable_item("app.one", false)), LCARS_OP(h, items())};
}
#undef LCARS_OP

// The one list of handle kinds. `gone` is the not_found text the kind's operations give today once the Ui is gone.
struct HandleKind {
    const char* name;
    const char16_t* gone;
    std::vector<Op> (*operations)(const World&);
};
const std::vector<HandleKind>& kinds() {
    static const std::vector<HandleKind> list{
        {"TextHandle", u"Component no longer exists", text_ops},
        {"ButtonHandle", u"Component no longer exists", button_ops},
        {"LabelHandle", u"Component no longer exists", label_ops},
        {"CheckboxHandle", u"Component no longer exists", checkbox_ops},
        {"RadioGroupHandle", u"Component no longer exists", radio_ops},
        {"SliderHandle", u"Component no longer exists", slider_ops},
        {"ProgressHandle", u"Component no longer exists", progress_ops},
        {"DropdownHandle", u"Component no longer exists", dropdown_ops},
        {"TabsHandle", u"Component no longer exists", tabs_ops},
        {"NavigationListHandle", u"Component no longer exists", navigation_ops},
        {"ListHandle", u"Component no longer exists", list_ops},
        {"DialogHandle", u"UI no longer exists", dialog_ops},
        {"TooltipHandle", u"UI no longer exists", tooltip_ops},
        {"ContextMenuHandle", u"Component no longer exists", menu_ops},
    };
    return list;
}
const HandleKind& kind_named(std::string_view name) {
    for (const auto& kind : kinds()) if (kind.name == name) return kind;
    throw std::runtime_error("no such handle kind in the list");
}

std::string narrow(std::u16string_view text) { return {text.begin(), text.end()}; }  // the texts checked here are ASCII
// The generic lifetime driver: each operation answers not_found with its text of today.
void expect_gone(const HandleKind& kind, const std::vector<Op>& operations) {
    for (const auto& op : operations) {
        const auto [status, message] = op.run();
        const std::u16string want = op.gone ? op.gone : kind.gone;
        const auto what = std::string(kind.name) + "::" + op.name + " answers not_found \"" + narrow(want) + "\", got status " +
                          std::to_string(static_cast<int>(status)) + " \"" + narrow(message) + "\"";
        check(status == Status::not_found && message == want, what.c_str());
    }
}

void list_covers_every_kind() {
    check(kinds().size() == 14, "the list names all 14 handle kinds");
    // Every component handle alternative is in the list (the remaining three are DialogHandle, TooltipHandle, ContextMenuHandle).
    check(std::variant_size_v<ComponentHandle> - 1 + 3 == kinds().size(), "component handles plus dialog, tooltip and menu");
    for (const auto& kind : kinds()) {
        World world;
        check(!kind.operations(world).empty(), "every kind lists its operations");
    }
}

void every_operation_after_the_ui_ends_is_not_found() {
    World world;
    std::vector<std::vector<Op>> operations;
    for (const auto& kind : kinds()) operations.push_back(kind.operations(world));
    world.ui.reset();
    for (std::size_t i = 0; i < kinds().size(); ++i) expect_gone(kinds()[i], operations[i]);
}

void every_operation_of_a_removed_button_is_not_found() {
    World world;
    const auto& kind = kind_named("ButtonHandle");
    auto operations = kind.operations(world);
    check(world.button.remove().status == Status::ok, "the button is removed");
    expect_gone(kind, operations);
    check(world.text.describe().status == Status::ok, "the Ui itself lives on");
}
}

int main() {
    static constexpr test::Case cases[]{
        {"the list covers every handle kind", list_covers_every_kind},
        {"every operation of every handle answers not_found after the Ui ends", every_operation_after_the_ui_ends_is_not_found},
        {"every operation of a removed button's handle answers not_found", every_operation_of_a_removed_button_is_not_found},
    };
    return test::run("handles", cases);
}
