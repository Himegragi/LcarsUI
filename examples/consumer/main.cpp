// A consumer form unrelated to the shipped edit form: its own IDs, data model, validation
// and callback, composed from C01-C03 and laid out with its own text measurement.
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "lcars/version.hpp"
#include "handle_kinds.hpp"
static_assert(lcars::framework_version == LCARS_PACKAGE_VERSION);
static_assert(LCARS_VERSION_MAJOR == 0 && LCARS_VERSION_MINOR == 3 && LCARS_VERSION_PATCH == 0);
static_assert(lcars::framework_version_major == 0 && lcars::framework_version_minor == 3 && lcars::framework_version_patch == 0);
static_assert(!std::is_constructible_v<lcars::SemanticTree, lcars::detail::UiState&>);
#include <concepts>
#include <cstdio>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// Read sources are one template told apart by value type (#95), and stay strongly typed (ADR 0004).
static_assert(!std::is_convertible_v<lcars::Source<bool>, lcars::Source<int>>);
static_assert(!std::is_convertible_v<lcars::Source<bool>, lcars::Source<std::optional<lcars::OptionValue>>>);
static_assert(!std::is_convertible_v<lcars::Source<std::u16string>, lcars::Source<int>>);
static_assert(!std::is_convertible_v<lcars::Source<std::u16string>, lcars::Source<bool>>);
static_assert(!std::is_convertible_v<bool, lcars::OptionValue>);
// Change records are told apart by field kind and value type: a radio group's and a dropdown's share a value type, a
// text field's plain and rich records share a field kind, and none of them converts into another.
static_assert(!std::is_convertible_v<lcars::Change<lcars::RadioGroupHandle>, lcars::Change<lcars::DropdownHandle>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::DropdownHandle>, lcars::Change<lcars::RadioGroupHandle>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::TextHandle>, lcars::Change<lcars::TextHandle, lcars::RichText>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::TextHandle, lcars::RichText>, lcars::Change<lcars::TextHandle>>);
// Each field kind's default value type, and the only combinations that can be spelled: those the framework sends (the
// list's is checked below).
template <class Field, class... Value> concept SpellsChange = requires { typename lcars::Change<Field, Value...>; };
template <class Field, class Value> constexpr bool change_holds =
    SpellsChange<Field> && std::same_as<decltype(lcars::Change<Field>::value), Value> && std::same_as<decltype(lcars::Change<Field>::origin), Value> &&
    std::same_as<decltype(lcars::Change<Field>::field), Field> && std::same_as<lcars::Change<Field>, lcars::Change<Field, Value>>;
static_assert(change_holds<lcars::TextHandle, std::u16string>);
static_assert(change_holds<lcars::CheckboxHandle, bool>);
static_assert(change_holds<lcars::RadioGroupHandle, std::optional<lcars::OptionValue>>);
static_assert(change_holds<lcars::SliderHandle, int>);
static_assert(change_holds<lcars::DropdownHandle, std::optional<lcars::OptionValue>>);
static_assert(SpellsChange<lcars::TextHandle, lcars::RichText> && std::same_as<decltype(lcars::Change<lcars::TextHandle, lcars::RichText>::value), lcars::RichText>);
static_assert(!SpellsChange<lcars::ButtonHandle>);
static_assert(!SpellsChange<lcars::SliderHandle, bool>);
static_assert(std::same_as<lcars::FieldChange, std::variant<lcars::Change<lcars::TextHandle>, lcars::Change<lcars::CheckboxHandle>,
    lcars::Change<lcars::RadioGroupHandle>, lcars::Change<lcars::SliderHandle>, lcars::Change<lcars::DropdownHandle>, lcars::Change<lcars::TextHandle, lcars::RichText>>>);
// A list's write hands a change record too (#99): the seventh combination, written immediately only, so it is in neither
// FieldChange (above) nor ConfirmField. It shares its value type with the radio group and the dropdown and converts into
// neither; it has no other value type; and the bare-value write callback of 0.2 is no longer accepted.
static_assert(change_holds<lcars::ListHandle, std::optional<lcars::OptionValue>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::ListHandle>, lcars::Change<lcars::RadioGroupHandle>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::RadioGroupHandle>, lcars::Change<lcars::ListHandle>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::ListHandle>, lcars::Change<lcars::DropdownHandle>>);
static_assert(!std::is_convertible_v<lcars::Change<lcars::DropdownHandle>, lcars::Change<lcars::ListHandle>>);
static_assert(!SpellsChange<lcars::ListHandle, int>);
static_assert(!std::is_convertible_v<lcars::ListHandle, lcars::ConfirmField>);
template <class List> concept ListWritesBareValue = requires(const List& list) {
    list.connect_immediate([](std::optional<lcars::OptionValue>) { return lcars::Result{}; });
};
template <class List> concept ListWritesChange = requires(const List& list) {
    list.connect_immediate([](const lcars::Change<lcars::ListHandle>&) { return lcars::Result{}; });
};
static_assert(!ListWritesBareValue<lcars::ListHandle> && ListWritesChange<lcars::ListHandle>);
// A label takes either a read function or a full read source (#99). Braces around a single lambda match both and are
// ambiguous by design; every other spelling picks one.
template <class Label> concept LabelTakesFunction = requires(const Label& label) { label.connect_source([] { return std::u16string(); }); };
template <class Label> concept LabelTakesSource = requires(const Label& label, std::function<std::u16string()> read,
                                                             std::function<std::function<void()>(std::function<void()>)> subscribe,
                                                             lcars::Source<std::u16string> source) {
    label.connect_source({read, subscribe});
    label.connect_source({{}, {}});
    label.connect_source(source);
};
template <class Label> concept LabelTakesBracedFunction = requires(const Label& label) { label.connect_source({[] { return std::u16string(); }}); };
static_assert(LabelTakesFunction<lcars::LabelHandle> && LabelTakesSource<lcars::LabelHandle> && !LabelTakesBracedFunction<lcars::LabelHandle>);
// The pre-0.3.0 names are gone without aliases (#95). Unqualified lookup inside a namespace nested in lcars finds lcars's
// own declaration first and reaches these global stand-ins only when lcars has none, so a check fails while an old name exists.
namespace removed_in_0_3 { struct Name {}; }
using TextSource = removed_in_0_3::Name;
using RichTextSource = removed_in_0_3::Name;
using BoolSource = removed_in_0_3::Name;
using NumericSource = removed_in_0_3::Name;
using OptionSource = removed_in_0_3::Name;
using TextChange = removed_in_0_3::Name;
using RichTextChange = removed_in_0_3::Name;
using BoolChange = removed_in_0_3::Name;
using OptionChange = removed_in_0_3::Name;
using NumericChange = removed_in_0_3::Name;
using DropdownChange = removed_in_0_3::Name;
namespace lcars::consumer_probe {
template <class T> concept Removed = std::same_as<T, ::removed_in_0_3::Name>;
static_assert(Removed<TextSource> && Removed<RichTextSource> && Removed<BoolSource> && Removed<NumericSource> && Removed<OptionSource>);
static_assert(Removed<TextChange> && Removed<RichTextChange> && Removed<BoolChange> && Removed<OptionChange> && Removed<NumericChange> && Removed<DropdownChange>);
}
// The text-only confirmation group is gone; text fields join the confirmation group that takes every field kind. The
// stand-in result converts to whatever the callback must return, so only the field list decides the check: the same call
// with the list of every field kind compiles (#96 gave that group the name).
struct AnyResult { template <class R> operator R() const { return R{}; } };
template <class T> concept HasTextOnlyConfirmation = requires(T& ui, lcars::ButtonHandle button, std::vector<lcars::TextHandle> fields) {
    ui.connect_confirmation(button, fields, [](const auto&) { return AnyResult{}; });
};
template <class T> concept HasConfirmation = requires(T& ui, lcars::ButtonHandle button, std::vector<lcars::ConfirmField> fields) {
    ui.connect_confirmation(button, fields, [](const std::vector<lcars::FieldChange>&) { return AnyResult{}; });
};
static_assert(!HasTextOnlyConfirmation<lcars::Ui>);
static_assert(HasConfirmation<lcars::Ui>);
static_assert(std::same_as<std::invoke_result_t<decltype(&lcars::Ui::connect_confirmation), lcars::Ui&, lcars::ButtonHandle, std::vector<lcars::ConfirmField>,
    std::function<lcars::ConfirmationResult(const std::vector<lcars::FieldChange>&)>>, lcars::Result>);
static_assert(std::same_as<decltype(lcars::ConfirmationResult::errors), std::vector<lcars::FieldError>>);
static_assert(std::same_as<decltype(lcars::FieldError::field), lcars::ConfirmField> && std::same_as<decltype(lcars::FieldError::message), std::u16string>);

// Public names follow the glossary (#96). Each new name, and the pre-0.3.0 name it replaces, which is gone without an alias.
// A component type is a ComponentKind; add_x sets it, so the configuration has no kind field.
static_assert(std::is_enum_v<lcars::ComponentKind> && std::same_as<decltype(lcars::ComponentSnapshot::kind), lcars::ComponentKind> &&
              std::same_as<decltype(lcars::Placement::kind), lcars::ComponentKind>);
template <class T> concept HasKind = requires(T value) { value.kind; };
static_assert(!HasKind<lcars::ComponentConfig>);
// What a label says about its target is its LabelKind, on the configuration and the snapshot alike; role is the accessibility Role.
static_assert(std::same_as<decltype(lcars::ComponentConfig::label_kind), lcars::LabelKind> &&
              std::same_as<decltype(lcars::ComponentSnapshot::label_kind), lcars::LabelKind>);
static_assert(lcars::LabelKind{} == lcars::LabelKind::plain && lcars::LabelKind::name != lcars::LabelKind::description &&
              lcars::LabelKind::description != lcars::LabelKind::error);
template <class T> concept HasRole = requires(T value) { value.role; };
static_assert(!HasRole<lcars::ComponentConfig> && !HasRole<lcars::ComponentSnapshot> && HasRole<lcars::NodeSnapshot>);
// One option of a radio group, a dropdown or a list is an OptionItem; its snapshot an OptionSnapshot, listed as items.
static_assert(std::same_as<decltype(lcars::OptionItem::value), lcars::OptionValue> && std::same_as<decltype(lcars::OptionItem::label), std::u16string>);
static_assert(std::same_as<decltype(lcars::ComponentSnapshot::items), std::vector<lcars::OptionSnapshot>>);
template <class T> concept HasOptions = requires(T value) { value.options; };
static_assert(!HasOptions<lcars::ComponentSnapshot>);
template <class T> concept SetsItems = requires(const T& handle, std::vector<lcars::OptionItem> items) { { handle.set_items(items) } -> std::same_as<lcars::Result>; };
template <class T> concept SetsOptions = requires(const T& handle, std::vector<lcars::OptionItem> items) { handle.set_options(items); };
static_assert(SetsItems<lcars::DropdownHandle> && SetsItems<lcars::ListHandle> && !SetsOptions<lcars::DropdownHandle>);
template <class T> concept HasAnchorItem = requires(T state) { { state.anchor_item } -> std::same_as<std::string&>; };
template <class T> concept HasAnchorOption = requires(T state) { state.anchor_option; };
static_assert(HasAnchorItem<lcars::ScrollState> && !HasAnchorOption<lcars::ScrollState>);
// Read-only snapshots are XxxSnapshot.
static_assert(std::same_as<decltype(lcars::MenuSnapshot::items), std::vector<lcars::MenuItemSnapshot>>);
static_assert(std::same_as<decltype(std::declval<lcars::Ui&>().context_menu()), lcars::Query<lcars::MenuSnapshot>>);
static_assert(std::same_as<decltype(std::declval<lcars::ContextMenuHandle&>().items()), lcars::Query<std::vector<lcars::MenuItemSnapshot>>>);
static_assert(std::same_as<decltype(std::declval<lcars::SemanticTree&>().get(lcars::NodeRef{})), lcars::Query<lcars::NodeSnapshot>>);
static_assert(std::same_as<decltype(std::declval<lcars::TextHandle&>().describe()), lcars::Query<lcars::ComponentSnapshot>>);
// A layout node's kind is a Node::Kind, and the node that places one component is made by layout::component.
static_assert(std::same_as<decltype(lcars::layout::Node::kind), lcars::layout::Node::Kind>);
static_assert(std::same_as<decltype(lcars::layout::component(lcars::ComponentHandle{})), lcars::layout::Node>);
template <class T> concept HasType = requires(T node) { node.type; };
static_assert(!HasType<lcars::layout::Node>);
template <class T> concept HasMixedConfirmation = requires { &T::connect_mixed_confirmation; };
static_assert(!HasMixedConfirmation<lcars::Ui>);
namespace removed_in_0_3 { inline Name control(const lcars::ComponentHandle&, int = 0) { return {}; } }
using Kind = removed_in_0_3::Name;
using Control = removed_in_0_3::Name;
using TextRole = removed_in_0_3::Name;
using RadioOption = removed_in_0_3::Name;
using Semantics = removed_in_0_3::Name;
using OptionSemantics = removed_in_0_3::Name;
using MenuInfo = removed_in_0_3::Name;
using MenuItemInfo = removed_in_0_3::Name;
using SemanticNode = removed_in_0_3::Name;
using MixedFieldError = removed_in_0_3::Name;
using MixedConfirmationResult = removed_in_0_3::Name;
using DesktopOptions = removed_in_0_3::Name;
using DesktopCreation = removed_in_0_3::Name;
using DiagnosticCategory = removed_in_0_3::Name;
using removed_in_0_3::control;
namespace lcars::consumer_probe {
static_assert(Removed<Kind> && Removed<Control> && Removed<TextRole> && Removed<RadioOption>);
static_assert(Removed<Semantics> && Removed<OptionSemantics> && Removed<MenuInfo> && Removed<MenuItemInfo> && Removed<SemanticNode>);
static_assert(Removed<MixedFieldError> && Removed<MixedConfirmationResult>);
}
namespace lcars::layout::consumer_probe {
static_assert(std::same_as<decltype(control(lcars::ComponentHandle{})), ::removed_in_0_3::Name>);
}
// The desktop module (when the package has it): creation parameters are a Config, creation hands back a Result, and a
// diagnostic says which kind of problem it is.
#if __has_include("lcars/desktop.hpp")
#include "lcars/desktop.hpp"
static_assert(std::same_as<decltype(lcars::Desktop::create), lcars::CreateResult(lcars::Ui&, const lcars::Appearance&, std::function<lcars::layout::Node()>,
                                                                                 lcars::ScrollState&, lcars::DesktopConfig)>);
static_assert(std::same_as<decltype(lcars::DesktopConfig::title), std::u16string>);
static_assert(std::same_as<decltype(lcars::RenderDiagnostic::kind), lcars::DiagnosticKind>);
template <class T> concept HasCategory = requires(T value) { value.category; };
static_assert(!HasCategory<lcars::RenderDiagnostic>);
namespace lcars::consumer_probe {
static_assert(Removed<DesktopOptions> && Removed<DesktopCreation> && Removed<DiagnosticCategory>);
}
#endif
// Handles compare within their own kind only (#94): equal when they control the same thing, a compile error across kinds.
static_assert(handle_kinds::equality_holds);
static_assert(handle_kinds::construction_holds);
static_assert(std::tuple_size_v<handle_kinds::All> == 14);
static_assert(std::equality_comparable<lcars::ComponentHandle> && std::equality_comparable<lcars::ConfirmField>);
static_assert(!std::equality_comparable_with<lcars::TextHandle, lcars::CheckboxHandle>);  // one case of the list's rule, spelled out
// Applications use typed handles; string operations belong to the semantic layer, not Ui.
template <class T> concept HasFind = requires(T& ui, std::string_view id) { ui.find(id); };
static_assert(!HasFind<lcars::Ui>);
template <class T> concept HasText = requires(T& ui, std::string_view id) { ui.text(id); };
static_assert(!HasText<lcars::Ui>);
template <class T> concept HasEdit = requires(T& ui, std::string_view id) { ui.edit(id); };
static_assert(!HasEdit<lcars::Ui>);
template <class T> concept HasSetValue = requires(T& ui, std::string_view id) { ui.set_value(id, std::u16string{}); };
static_assert(!HasSetValue<lcars::Ui>);
template <class T> concept HasInvoke = requires(T& ui, std::string_view id) { ui.invoke(id); };
static_assert(!HasInvoke<lcars::Ui>);
template <class T> concept HasBind = requires(T& ui, std::string_view id) { ui.bind(std::string(id), [] { return lcars::Result{}; }); };
static_assert(!HasBind<lcars::Ui>);
template <class T> concept HasBindText = requires(T& ui, std::string_view id) { ui.bind_text(id, [] { return std::u16string{}; }); };
static_assert(!HasBindText<lcars::Ui>);
template <class T> concept HasFocus = requires(T& ui, std::string_view id) { ui.focus(id); };
static_assert(!HasFocus<lcars::Ui>);
template <class T> concept HasPointerDown = requires(T& ui, std::string_view id) { ui.pointer_down(id); };
static_assert(!HasPointerDown<lcars::Ui>);
template <class T> concept HasPointerUp = requires(T& ui, std::string_view id) { ui.pointer_up(id); };
static_assert(!HasPointerUp<lcars::Ui>);
template <class T> concept HasUndo = requires(T& ui, std::string_view id) { ui.undo(id); };
static_assert(!HasUndo<lcars::Ui>);
template <class T> concept HasRedo = requires(T& ui, std::string_view id) { ui.redo(id); };
static_assert(!HasRedo<lcars::Ui>);
template <class T> concept HasPaste = requires(T& ui, std::string_view id) { ui.paste(id, std::u16string_view{}); };
static_assert(!HasPaste<lcars::Ui>);
template <class T> concept HasSelect = requires(T& ui, std::string_view id) { ui.select(id, 0, 0); };
static_assert(!HasSelect<lcars::Ui>);
template <class T> concept HasCopy = requires(T& ui, std::string_view id) { ui.copy(id); };
static_assert(!HasCopy<lcars::Ui>);
template <class T> concept HasCut = requires(T& ui, std::string_view id) { ui.cut(id); };
static_assert(!HasCut<lcars::Ui>);
template <class T> concept HasEnable = requires(T& ui, std::string_view id) { ui.enable(id, true); };
static_assert(!HasEnable<lcars::Ui>);
template <class T> concept HasReadOnly = requires(T& ui, std::string_view id) { ui.set_read_only(id, true); };
static_assert(!HasReadOnly<lcars::Ui>);
template <class T> concept HasSetError = requires(T& ui, std::string_view id) { ui.set_error(id, std::u16string{}); };
static_assert(!HasSetError<lcars::Ui>);
template <class T> concept HasReplaceText = requires(T& ui, std::string_view id) { ui.replace_text(id, std::u16string{}); };
static_assert(!HasReplaceText<lcars::Ui>);
template <class T> concept HasAvailable = requires(T& ui, std::string_view id) { ui.available(id); };
static_assert(!HasAvailable<lcars::Ui>);
template <class T> concept HasInput = requires(T& ui, std::string_view id) { ui.text_input(std::u16string_view{}); };
static_assert(!HasInput<lcars::Ui>);

template<class T> concept FindsByString = requires(T& frame, std::string_view id) { frame.find(id); };
static_assert(!FindsByString<lcars::Frame>);
template<class T> concept LaysOutString = requires(T id) { lcars::layout::component(id); };
static_assert(!LaysOutString<std::string>);

namespace {
// The application's pre-existing data model; LcarsUI never owns or copies it.
struct Profile {
    std::u16string alias = u"guest";
    int saves = 0;
};
// Monospace stand-in for a real shaping backend.
struct Monospace final : lcars::TextMeasure {
    lcars::TextRun measure(std::u16string_view text, int pixels) override {
        lcars::TextRun run;
        for (unsigned i = 0; i <= text.size(); ++i) run.stops.push_back({i, i * pixels * 0.6f});
        run.width = text.size() * pixels * 0.6f;
        return run;
    }
    int line_height(int pixels) override { return pixels + 4; }
};
// The component type a made component reports, or nothing when it was not made.
template <class Handle> std::optional<lcars::ComponentKind> kind_of(const lcars::Query<Handle>& made) {
    if (!made) return std::nullopt;
    const auto described = made->describe();
    return described ? std::optional<lcars::ComponentKind>(described->kind) : std::nullopt;
}
int failures = 0;
void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}

int main() {
    using namespace lcars;
    Profile profile;
    std::function<void()> changed;
    Ui ui;
    ui.add_label({.id = "profile.alias.caption", .value = u"Alias", .target = "profile.alias", .label_kind = LabelKind::name}).value.value();
    auto alias = ui.add_text({.id = "profile.alias", .max_length = 12}).value.value();
    ui.add_label({.id = "profile.alias.problem", .target = "profile.alias", .label_kind = LabelKind::error}).value.value();
    auto save = ui.add_button({.id = "profile.save", .label = u"Save alias"}).value.value();
    auto summary = ui.add_label({.id = "profile.summary"}).value.value();
    alias.connect_source({[&] { return profile.alias; }, [&](std::function<void()> callback) {
        changed = std::move(callback);
        return [&] { changed = {}; };
    }});
    summary.connect_source([&] { return u"Saved " + profile.alias; });
    ui.connect_confirmation(save, {alias}, [&](const std::vector<FieldChange>& changes) {
        const auto& change = std::get<Change<TextHandle>>(changes[0]);
        if (change.value.size() < 3) {
            return ConfirmationResult{{Status::invalid_value, u"At least three characters"}, {{change.field, u"At least three characters"}}};
        }
        profile.alias = change.value;
        ++profile.saves;
        if (changed) changed();
        return ConfirmationResult{};
    });

    alias.focus();
    alias.set_value(u"ab");
    auto handle_status = save.invoke().status;
    auto semantic_status = ui.semantics().perform({"profile.save"}, Verb::invoke).status;
    expect(handle_status == Status::invalid_value && semantic_status == handle_status && profile.saves == 0, "consumer validation agrees through both interfaces");
    expect(alias.describe()->error == u"At least three characters", "consumer error is attached to its own field");
    expect(ui.semantics().perform({"profile.alias"}, Verb::set_value, std::u16string(u"abcdef")).status == Status::ok &&
           ui.semantics().text("profile.alias").value == u"abcdef", "semantic editing updates the draft");
    expect(ui.semantics().perform({"profile.alias"}, Verb::select, Selection{1, 3}).status == Status::ok &&
           ui.semantics().perform({"profile.alias"}, Verb::undo).status == Status::ok &&
           ui.semantics().text("profile.alias").value == u"ab" &&
           ui.semantics().perform({"profile.alias"}, Verb::redo).status == Status::ok,
           "semantic selection and history use the field behavior");
    save.focus();
    save.invoke();
    expect(profile.alias == u"abcdef" && profile.saves == 1, "consumer callback writes its existing data model once");
    expect(!alias.describe()->focused && alias.describe()->value == u"abcdef",
           "focus moves to the save button and the field is untouched");
    alias.focus();
    expect(alias.paste(u"0123456789").status == Status::invalid_value, "consumer-chosen length limit is enforced");

    Monospace measure;
    ScrollState scroll;
    auto page = layout::column({layout::component(alias), layout::row({layout::component(save), layout::component(summary)})}, 8, 16);
    auto frame = arrange(page, ui, material_appearance(), measure, 480, 320, 1.f, scroll);
    const auto* field = frame.find(alias);
    expect(field && field->label_lines.size() == 1 && field->label_lines[0].text == u"Alias", "framework layout carries the consumer's label");
    const auto* placement = frame.find(save);
    const auto* hit = placement ? frame.hit(placement->bounds.x + 1, placement->bounds.y + 1) : nullptr;
    expect(hit && hit->id == "profile.save", "consumer IDs are hit-testable");
    // add_x gives the component its type (#96): no configuration names it, and each component is what its add_x makes.
    Ui typed;
    const std::vector<OptionItem> items = {{{"a"}, u"Alpha"}, {{"b"}, u"Beta"}};
    expect(kind_of(typed.add_text({.id = "typed.text"})) == ComponentKind::text, "add_text makes a text field");
    expect(kind_of(typed.add_button({.id = "typed.button", .label = u"Go"})) == ComponentKind::button, "add_button makes a button");
    expect(kind_of(typed.add_label({.id = "typed.label", .value = u"Note"})) == ComponentKind::label, "add_label makes a label");
    expect(kind_of(typed.add_checkbox({.id = "typed.checkbox", .label = u"On"})) == ComponentKind::checkbox, "add_checkbox makes a checkbox");
    expect(kind_of(typed.add_radio_group({.id = "typed.radio", .label = u"Pick"}, items)) == ComponentKind::radio_group, "add_radio_group makes a radio group");
    expect(kind_of(typed.add_slider({.id = "typed.slider", .label = u"Level"}, {0, 10, 1})) == ComponentKind::slider, "add_slider makes a slider");
    expect(kind_of(typed.add_progress({.id = "typed.progress", .label = u"Done"})) == ComponentKind::progress, "add_progress makes a progress display");
    expect(kind_of(typed.add_dropdown({.id = "typed.dropdown", .label = u"Region"}, items)) == ComponentKind::dropdown, "add_dropdown makes a dropdown");
    expect(kind_of(typed.add_list({.id = "typed.list", .label = u"Rows"}, items)) == ComponentKind::list, "add_list makes a list");
    expect(kind_of(typed.add_tabs({.id = "typed.tabs", .label = u"Pages"}, {{{"one"}, u"One"}})) == ComponentKind::tabs, "add_tabs makes tabs");
    expect(kind_of(typed.add_navigation_list({.id = "typed.navigation", .label = u"Go to"}, {{{"home"}, u"Home"}}, {"home"})) ==
           ComponentKind::navigation_list, "add_navigation_list makes a navigation list");
    std::printf("%s\n", failures ? "consumer FAILED" : "consumer OK");
    return failures ? 1 : 0;
}
