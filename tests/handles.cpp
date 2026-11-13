// What every typed handle kind promises alike (#94), checked through the public API by one list of all handle kinds
// (`kinds` below) and generic drivers over it:
//   - lifetime: once the Ui is gone, every operation of every handle answers not_found with that operation's text of
//     today; a removed button's handle does the same while the Ui lives on.
//   - equality: two handles are equal when they control the same thing now. Two default (empty) handles are equal and
//     differ from every real one; a recreated button differs from the removed one; the same id in another Ui differs.
//     Handles of different kinds do not compare at all (a compile error, checked with static_assert).
// A new handle kind is one more World member, one more entry in `kinds` (with every public operation it has) and one
// more type in handle_kinds::All (examples/consumer/handle_kinds.hpp, shared with the consumer example).
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
using HandleTypes = handle_kinds::All;
template <class T, class... H> constexpr bool listed(std::tuple<H...>*) { return (std::is_same_v<T, H> || ...); }
template <class... A> constexpr bool every_alternative_listed(std::variant<std::monostate, A...>*) {
    return (listed<A>(static_cast<HandleTypes*>(nullptr)) && ...);
}
// What a handle holds stays private: neither its user nor a class derived from it reaches the Ui, lifetime or id.
template <class H> struct Derived : H {
    static constexpr bool sees_state() {
        return requires(const Derived& d) { d.ui_; } || requires(const Derived& d) { d.lifetime_; } || requires(const Derived& d) { d.id_; };
    }
};
template <class H> concept exposes_state = requires(const H& h) { h.ui_; } || requires(const H& h) { h.lifetime_; } || requires(const H& h) { h.id_; };
template <class... H> constexpr bool state_private(std::tuple<H...>*) { return ((!exposes_state<H> && !Derived<H>::sees_state()) && ...); }

static_assert(std::tuple_size_v<HandleTypes> == 14);
static_assert(every_alternative_listed(static_cast<ComponentHandle*>(nullptr)));
static_assert(handle_kinds::equality_holds, "decisions 2 and 4: every handle kind has operator==, and different kinds never compare");
// The shared predicate does fail a list with a kind that compares across kinds, or one without ==, so it is not vacuous.
struct WithoutEquality {};
static_assert(!handle_kinds::compare_within_their_kind_only(static_cast<std::tuple<int, long>*>(nullptr)) &&
              !handle_kinds::compare_within_their_kind_only(static_cast<std::tuple<TextHandle, WithoutEquality>*>(nullptr)));
static_assert(handle_kinds::construction_holds, "a user makes only empty handles");
struct OpenConstruction { OpenConstruction() = default; OpenConstruction(detail::UiState*, std::weak_ptr<void>, std::string) {} };
static_assert(!handle_kinds::made_only_by_the_framework(static_cast<std::tuple<OpenConstruction>*>(nullptr)));
static_assert(!std::equality_comparable_with<TextHandle, CheckboxHandle>);
static_assert(!std::equality_comparable_with<TabsHandle, NavigationListHandle>);  // one implementation inside, still two kinds
static_assert(!std::equality_comparable_with<TextHandle, LabelHandle>);
static_assert(std::equality_comparable<ComponentHandle> && std::equality_comparable<ConfirmField>);
static_assert(state_private(static_cast<HandleTypes*>(nullptr)));
// The two probes do see state that is reachable, so the check above is not vacuous.
struct OpenState { void* ui_ = nullptr; };
class InheritedState { protected: int id_ = 0; };
static_assert(exposes_state<OpenState> && Derived<InheritedState>::sees_state());

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
        text = made(ui->add_text({.id = "text", .kind = ComponentKind::text, .label = u"Text"}), "add_text");
        wired(text.connect_source({[] { return std::u16string(u"x"); }, no_updates()}), "text.connect_source");
        wired(text.connect_immediate(accept), "text.connect_immediate");
        button = made(ui->add_button({.id = "button", .kind = ComponentKind::button, .label = u"Button"}), "add_button");
        wired(button.bind([] { return Result{}; }), "button.bind");
        label = made(ui->add_label({.id = "label", .kind = ComponentKind::label, .value = u"Label"}), "add_label");
        checkbox = made(ui->add_checkbox({.id = "checkbox", .kind = ComponentKind::checkbox, .label = u"Check"}), "add_checkbox");
        wired(checkbox.connect_source({[] { return false; }, no_updates()}), "checkbox.connect_source");
        wired(checkbox.connect_immediate(accept), "checkbox.connect_immediate");
        radio = made(ui->add_radio_group({.id = "radio", .kind = ComponentKind::radio_group, .label = u"Radio"}, two_options()), "add_radio_group");
        wired(radio.connect_source({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, no_updates()}), "radio.connect_source");
        wired(radio.connect_immediate(accept), "radio.connect_immediate");
        slider = made(ui->add_slider({.id = "slider", .kind = ComponentKind::slider, .label = u"Slider"}, {0, 100, 10}), "add_slider");
        wired(slider.connect_source({[] { return 20; }, no_updates()}), "slider.connect_source");
        wired(slider.connect_immediate(accept), "slider.connect_immediate");
        progress = made(ui->add_progress({.id = "progress", .kind = ComponentKind::progress, .label = u"Progress"}), "add_progress");
        wired(progress.connect_source({[] { return 20; }, no_updates()}), "progress.connect_source");
        dropdown = made(ui->add_dropdown({.id = "dropdown", .kind = ComponentKind::dropdown, .label = u"Drop"}, two_options()), "add_dropdown");
        wired(dropdown.connect_source({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, no_updates()}), "dropdown.connect_source");
        wired(dropdown.connect_immediate(accept), "dropdown.connect_immediate");
        tabs = made(ui->add_tabs({.id = "tabs", .kind = ComponentKind::tabs, .label = u"Tabs"}, two_pages()), "add_tabs");
        navigation = made(ui->add_navigation_list({.id = "navigation", .kind = ComponentKind::navigation_list, .label = u"Nav"}, two_pages(), {"a"}),
                          "add_navigation_list");
        list = made(ui->add_list({.id = "list", .kind = ComponentKind::list, .label = u"List"}, two_options()), "add_list");
        wired(list.connect_source({[] { return std::optional<OptionValue>{OptionValue{"a"}}; }, no_updates()}), "list.connect_source");
        wired(list.connect_immediate([](std::optional<OptionValue>) { return Result{}; }), "list.connect_immediate");
        // The dialog has its own member and fallback, so the world's button stays free to be removed.
        auto inside = made(ui->add_button({.id = "dialog.ok", .kind = ComponentKind::button, .label = u"OK"}), "add_button dialog.ok");
        auto fallback = made(ui->add_button({.id = "dialog.fallback", .kind = ComponentKind::button, .label = u"Back"}), "add_button dialog.fallback");
        dialog = made(ui->add_dialog("dialog", {inside}, inside, fallback, [](bool) {}), "add_dialog");
        tooltip = made(ui->add_tooltip("tooltip", label, u"Tip"), "add_tooltip");
        menu = made(ui->add_context_menu(text, {command("app.one", u"One")}), "add_context_menu");
    }
private:
    // Every wiring step must succeed, so an operation that later answers not_found does so because its component or
    // the Ui is gone, not because the world was built short.
    static void wired(const Result& result, const char* step) { check(result.status == Status::ok, (std::string("World: ") + step).c_str()); }
    template <class H> static H made(const Query<H>& created, const char* step) {
        check(created.status == Status::ok && created.value.has_value(), (std::string("World: ") + step).c_str());
        return *created.value;
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
            LCARS_OP(h, connect_immediate([](const Change<TextHandle>&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const Change<TextHandle>&) { return Result{}; })),
            LCARS_OP(h, connect_rich_source({[] { return RichText{}; }, no_updates()})),
            LCARS_OP(h, connect_rich_immediate([](const Change<TextHandle, RichText>&) { return Result{}; })),
            LCARS_OP(h, connect_rich_confirmed([](const Change<TextHandle, RichText>&) { return Result{}; })),
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
            LCARS_OP(h, connect_immediate([](const Change<CheckboxHandle>&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const Change<CheckboxHandle>&) { return Result{}; })),
            LCARS_OP(h, confirm()), LCARS_OP(h, draft()), LCARS_OP(h, describe())};
}
std::vector<Op> radio_ops(const World& w) {
    auto h = w.radio;
    return {LCARS_OP(h, select({"b"})), LCARS_OP(h, use_latest()), LCARS_OP(h, focus()), LCARS_OP(h, enable(true)),
            LCARS_OP(h, enable_option({"b"}, false)),
            LCARS_OP(h, connect_source({[] { return std::optional<OptionValue>{}; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const Change<RadioGroupHandle>&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const Change<RadioGroupHandle>&) { return Result{}; })),
            LCARS_OP(h, confirm()), LCARS_OP(h, draft()), LCARS_OP(h, describe())};
}
std::vector<Op> slider_ops(const World& w) {
    auto h = w.slider;
    return {LCARS_OP(h, on_blur([] {})), LCARS_OP(h, set_value(30)), LCARS_OP(h, use_latest()), LCARS_OP(h, focus()), LCARS_OP(h, enable(true)),
            LCARS_OP(h, connect_source({[] { return 40; }, no_updates()})),
            LCARS_OP(h, connect_immediate([](const Change<SliderHandle>&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const Change<SliderHandle>&) { return Result{}; })),
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
            LCARS_OP(h, connect_immediate([](const Change<DropdownHandle>&) { return Result{}; })),
            LCARS_OP(h, connect_confirmed([](const Change<DropdownHandle>&) { return Result{}; })),
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
    const ComponentHandle opener{w.text};  // a real opener, so the live call is not refused for want of one
    return {Op{"open(opener)", [h, opener] { return answer(h.open(opener)); }}, LCARS_OP(h, open()), LCARS_OP(h, settle(true)), LCARS_OP(h, is_open())};
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
// The comparisons are filled in from the World member, so a kind is listed once and every driver sees it.
struct HandleKind {
    const char* name;
    const char16_t* gone;
    std::vector<Op> (*operations)(const World&);
    bool (*equal)(const World&, const World&);  // the kind's handle in one world == the one in the other
    bool (*copies_equal)(const World&);         // a copy-constructed and a copy-assigned handle == the world's
    bool (*empty_equals)(const World&);         // a default handle of the kind == the world's
    bool (*empties_equal)();                    // two default handles of the kind are equal
};
template <auto member> HandleKind kind(const char* name, const char16_t* gone, std::vector<Op> (*operations)(const World&)) {
    using H = std::remove_cvref_t<decltype(std::declval<World&>().*member)>;
    return {name, gone, operations,
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
        kind<&World::text>("TextHandle", u"Component no longer exists", text_ops),
        kind<&World::button>("ButtonHandle", u"Component no longer exists", button_ops),
        kind<&World::label>("LabelHandle", u"Component no longer exists", label_ops),
        kind<&World::checkbox>("CheckboxHandle", u"Component no longer exists", checkbox_ops),
        kind<&World::radio>("RadioGroupHandle", u"Component no longer exists", radio_ops),
        kind<&World::slider>("SliderHandle", u"Component no longer exists", slider_ops),
        kind<&World::progress>("ProgressHandle", u"Component no longer exists", progress_ops),
        kind<&World::dropdown>("DropdownHandle", u"Component no longer exists", dropdown_ops),
        kind<&World::tabs>("TabsHandle", u"Component no longer exists", tabs_ops),
        kind<&World::navigation>("NavigationListHandle", u"Component no longer exists", navigation_ops),
        kind<&World::list>("ListHandle", u"Component no longer exists", list_ops),
        kind<&World::dialog>("DialogHandle", u"UI no longer exists", dialog_ops),
        kind<&World::tooltip>("TooltipHandle", u"UI no longer exists", tooltip_ops),
        kind<&World::menu>("ContextMenuHandle", u"Component no longer exists", menu_ops),
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

// The live baseline the lifetime drivers stand on: while the component and its Ui exist, no listed operation answers
// not_found, so a handle that always answered not_found fails here. Each operation runs in a World of its own, as some
// change what follows them (a button's remove, a menu's set_items).
void expect_live(const HandleKind& kind) {
    const auto count = kind.operations(World{}).size();
    for (std::size_t i = 0; i < count; ++i) {
        World world;
        const auto op = kind.operations(world)[i];
        const auto [status, message] = op.run();
        const auto what = std::string(kind.name) + "::" + op.name + " works while its component lives, got status " +
                          std::to_string(static_cast<int>(status)) + " \"" + narrow(message) + "\"";
        check(status != Status::not_found, what.c_str());
    }
}

void list_covers_every_kind() {
    check(kinds().size() == std::tuple_size_v<HandleTypes>, "the list names all 14 handle kinds");
    // Every component handle alternative is in the list (the remaining three are DialogHandle, TooltipHandle, ContextMenuHandle).
    check(std::variant_size_v<ComponentHandle> - 1 + 3 == kinds().size(), "component handles plus dialog, tooltip and menu");
    for (const auto& kind : kinds()) {
        World world;
        check(!kind.operations(world).empty(), "every kind lists its operations");
    }
}

void every_operation_after_the_ui_ends_is_not_found() {
    for (const auto& kind : kinds()) expect_live(kind);
    World world;
    std::vector<std::vector<Op>> operations;
    for (const auto& kind : kinds()) operations.push_back(kind.operations(world));
    world.ui.reset();
    for (std::size_t i = 0; i < kinds().size(); ++i) expect_gone(kinds()[i], operations[i]);
}

void every_operation_of_a_removed_button_is_not_found() {
    const auto& kind = kind_named("ButtonHandle");
    expect_live(kind);
    World world;
    auto operations = kind.operations(world);
    check(world.button.remove().status == Status::ok, "the button is removed");
    expect_gone(kind, operations);
    check(world.text.describe().status == Status::ok, "the Ui itself lives on");
}

void expect(bool condition, const HandleKind& kind, const char* expectation) {
    const auto text = std::string(kind.name) + ": " + expectation;
    check(condition, text.c_str());
}

void empty_handles_equal_each_other_and_no_real_one() {
    World world;
    for (const auto& kind : kinds()) {
        expect(kind.empties_equal(), kind, "two default handles are equal");
        expect(!kind.empty_equals(world), kind, "a default handle differs from a real one");
    }
}

void a_handle_equals_its_copy() {
    World world;
    for (const auto& kind : kinds()) expect(kind.copies_equal(world), kind, "a handle equals its copies");
}

void the_same_id_in_another_ui_differs() {
    World one, other;
    for (const auto& kind : kinds()) expect(!kind.equal(one, other), kind, "the same id in another Ui is another handle");
}

void a_handle_the_framework_hands_back_equals_the_one_from_creation() {
    Ui ui;
    auto checkbox = ui.add_checkbox({.id = "check", .kind = ComponentKind::checkbox, .label = u"Check"}).value.value();
    checkbox.connect_source({[] { return false; }, no_updates()});
    std::optional<CheckboxHandle> written;
    checkbox.connect_immediate([&](const Change<CheckboxHandle>& change) { written = change.field; return Result{}; });
    check(checkbox.toggle().status == Status::ok && written, "the write ran");
    check(*written == checkbox, "the change names the same checkbox");
    check(ComponentHandle{*written} == ComponentHandle{checkbox}, "and so does a component handle of it");
    check(ConfirmField{*written} == ConfirmField{checkbox}, "and a confirmation field of it");
    check(!(ComponentHandle{checkbox} == ComponentHandle{}), "an empty component handle is no checkbox");
}

void a_recreated_button_is_another_handle() {
    Ui ui;
    auto old = ui.add_button({.id = "save", .kind = ComponentKind::button, .label = u"Save"}).value.value();
    check(old.remove().status == Status::ok, "the button is removed");
    auto recreated = ui.add_button({.id = "save", .kind = ComponentKind::button, .label = u"Save"}).value.value();
    check(!(old == recreated) && old != recreated, "the recreated button is not the removed one");
    check(old.invoke().status == Status::not_found, "the old handle still controls nothing");
    check(recreated.focus().status == Status::ok, "the new one controls the new button");
    check(ComponentHandle{old} != ComponentHandle{recreated}, "as component handles too");
}

// A context menu handle names its target (same Ui, same id): a menu attached again to a recreated target is the same
// menu handle, as the old handle controls the new attachment today.
void a_menu_attached_again_is_the_same_handle() {
    Ui ui;
    auto target = ui.add_button({.id = "target", .kind = ComponentKind::button, .label = u"Target"}).value.value();
    auto old = ui.add_context_menu(target, {command("app.one", u"One")}).value.value();
    check(target.remove().status == Status::ok, "the target is removed");
    check(old.items().status == Status::not_found, "the old menu is gone with it");
    auto recreated = ui.add_button({.id = "target", .kind = ComponentKind::button, .label = u"Target"}).value.value();
    auto again = ui.add_context_menu(recreated, {command("app.two", u"Two")}).value.value();
    check(old == again, "the menu attached again to the same id is the same handle");
    auto items = old.items();
    check(items && items->size() == 1 && items->front().id == "app.two", "and the old handle controls the new attachment");
}

// A tooltip handle lives as long as its source component: a tooltip on a recreated button is not reached through the
// old handle (it answers not_found today), so the two handles do not control the same thing and are not equal.
void a_tooltip_on_a_recreated_button_is_another_handle() {
    Ui ui;
    auto source = ui.add_button({.id = "source", .kind = ComponentKind::button, .label = u"Source"}).value.value();
    auto old = ui.add_tooltip("tip", source, u"Tip").value.value();
    check(source.remove().status == Status::ok, "the source is removed");
    auto recreated = ui.add_button({.id = "source", .kind = ComponentKind::button, .label = u"Source"}).value.value();
    auto again = ui.add_tooltip("tip", recreated, u"Tip").value.value();
    auto stale = old.visible();
    check(stale.status == Status::not_found && stale.message == u"UI no longer exists", "the old handle controls nothing");
    check(again.visible().status == Status::ok, "the new one controls the new tooltip");
    check(!(old == again), "so they are different handles");
}
}

int main() {
    static constexpr test::Case cases[]{
        {"the list covers every handle kind", list_covers_every_kind},
        {"every operation of every handle answers not_found after the Ui ends", every_operation_after_the_ui_ends_is_not_found},
        {"every operation of a removed button's handle answers not_found", every_operation_of_a_removed_button_is_not_found},
        {"default handles equal each other and no real handle", empty_handles_equal_each_other_and_no_real_one},
        {"a handle equals its copy", a_handle_equals_its_copy},
        {"the same id in another Ui is another handle", the_same_id_in_another_ui_differs},
        {"a handle handed back by the framework equals the one from creation", a_handle_the_framework_hands_back_equals_the_one_from_creation},
        {"a recreated button is another handle", a_recreated_button_is_another_handle},
        {"a menu attached again to the same target is the same handle", a_menu_attached_again_is_the_same_handle},
        {"a tooltip on a recreated button is another handle", a_tooltip_on_a_recreated_button_is_another_handle},
    };
    return test::run("handles", cases);
}
