// A consumer form unrelated to the shipped edit form: its own IDs, data model, validation
// and callback, composed from C01-C03 and laid out with its own text measurement.
#include "isoduo/layout.hpp"
#include "isoduo/semantics.hpp"
#include "isoduo/version.hpp"
#include "handle_kinds.hpp"
#include "camelcase_names.hpp"
static_assert(isoduo::frameworkVersion == ISODUO_PACKAGE_VERSION);
static_assert(ISODUO_VERSION_MAJOR == 0 && ISODUO_VERSION_MINOR == 4 && ISODUO_VERSION_PATCH == 0);
static_assert(isoduo::frameworkVersionMajor == 0 && isoduo::frameworkVersionMinor == 4 && isoduo::frameworkVersionPatch == 0);
static_assert(!std::is_constructible_v<isoduo::SemanticTree, isoduo::detail::UiState&>);
#include <concepts>
#include <cstdio>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// Removal belongs to application configuration, and is available on these nine component handles only (#104).
template <class H> concept OwnerRemovable = requires(const H& h) { { h.remove() } -> std::same_as<isoduo::Result>; };
static_assert(OwnerRemovable<isoduo::ButtonHandle> && OwnerRemovable<isoduo::LabelHandle> && OwnerRemovable<isoduo::TextFieldHandle> &&
              OwnerRemovable<isoduo::CheckboxHandle> && OwnerRemovable<isoduo::RadioGroupHandle> && OwnerRemovable<isoduo::SliderHandle> &&
              OwnerRemovable<isoduo::ProgressHandle> && OwnerRemovable<isoduo::DropdownHandle> && OwnerRemovable<isoduo::ListHandle>);
static_assert(!OwnerRemovable<isoduo::TabsHandle> && !OwnerRemovable<isoduo::NavigationListHandle> && !OwnerRemovable<isoduo::DialogHandle> &&
              !OwnerRemovable<isoduo::TooltipHandle> && !OwnerRemovable<isoduo::ContextMenuHandle> && !OwnerRemovable<isoduo::SemanticTree>);

// Read sources are one template told apart by value type (#95), and stay strongly typed (ADR 0004).
static_assert(!std::is_convertible_v<isoduo::Source<bool>, isoduo::Source<int>>);
static_assert(!std::is_convertible_v<isoduo::Source<bool>, isoduo::Source<std::optional<isoduo::OptionValue>>>);
static_assert(!std::is_convertible_v<isoduo::Source<std::u16string>, isoduo::Source<int>>);
static_assert(!std::is_convertible_v<isoduo::Source<std::u16string>, isoduo::Source<bool>>);
static_assert(!std::is_convertible_v<bool, isoduo::OptionValue>);
// Change records are told apart by field kind and value type: a radio group's and a dropdown's share a value type, a
// text field's plain and rich records share a field kind, and none of them converts into another.
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::RadioGroupHandle>, isoduo::Change<isoduo::DropdownHandle>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::DropdownHandle>, isoduo::Change<isoduo::RadioGroupHandle>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::TextFieldHandle>, isoduo::Change<isoduo::TextFieldHandle, isoduo::RichText>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::TextFieldHandle, isoduo::RichText>, isoduo::Change<isoduo::TextFieldHandle>>);
// Each field kind's default value type, and the only combinations that can be spelled: those the framework sends (the
// list's is checked below).
template <class Field, class... Value> concept SpellsChange = requires { typename isoduo::Change<Field, Value...>; };
template <class Field, class Value> constexpr bool changeHolds =
    SpellsChange<Field> && std::same_as<decltype(isoduo::Change<Field>::value), Value> && std::same_as<decltype(isoduo::Change<Field>::origin), Value> &&
    std::same_as<decltype(isoduo::Change<Field>::field), Field> && std::same_as<isoduo::Change<Field>, isoduo::Change<Field, Value>>;
static_assert(changeHolds<isoduo::TextFieldHandle, std::u16string>);
static_assert(changeHolds<isoduo::CheckboxHandle, bool>);
static_assert(changeHolds<isoduo::RadioGroupHandle, std::optional<isoduo::OptionValue>>);
static_assert(changeHolds<isoduo::SliderHandle, int>);
static_assert(changeHolds<isoduo::DropdownHandle, std::optional<isoduo::OptionValue>>);
static_assert(SpellsChange<isoduo::TextFieldHandle, isoduo::RichText> && std::same_as<decltype(isoduo::Change<isoduo::TextFieldHandle, isoduo::RichText>::value), isoduo::RichText>);
static_assert(!SpellsChange<isoduo::ButtonHandle>);
static_assert(!SpellsChange<isoduo::SliderHandle, bool>);
static_assert(std::same_as<isoduo::ConfirmFieldChange, std::variant<isoduo::Change<isoduo::TextFieldHandle>, isoduo::Change<isoduo::CheckboxHandle>,
    isoduo::Change<isoduo::RadioGroupHandle>, isoduo::Change<isoduo::SliderHandle>, isoduo::Change<isoduo::DropdownHandle>, isoduo::Change<isoduo::TextFieldHandle, isoduo::RichText>>>);
// A list's write hands a change record too (#99): the seventh combination, written immediately only, so it is in neither
// ConfirmFieldChange (above) nor ConfirmField. It shares its value type with the radio group and the dropdown and converts into
// neither; it has no other value type; and the bare-value write callback of 0.2 is no longer accepted.
static_assert(changeHolds<isoduo::ListHandle, std::optional<isoduo::OptionValue>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::ListHandle>, isoduo::Change<isoduo::RadioGroupHandle>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::RadioGroupHandle>, isoduo::Change<isoduo::ListHandle>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::ListHandle>, isoduo::Change<isoduo::DropdownHandle>>);
static_assert(!std::is_convertible_v<isoduo::Change<isoduo::DropdownHandle>, isoduo::Change<isoduo::ListHandle>>);
static_assert(!SpellsChange<isoduo::ListHandle, int>);
static_assert(!std::is_convertible_v<isoduo::ListHandle, isoduo::ConfirmField>);
template <class List> concept ListWritesBareValue = requires(const List& list) {
    list.connectImmediate([](std::optional<isoduo::OptionValue>) { return isoduo::Result{}; });
};
template <class List> concept ListWritesChange = requires(const List& list) {
    list.connectImmediate([](const isoduo::Change<isoduo::ListHandle>&) { return isoduo::Result{}; });
};
static_assert(!ListWritesBareValue<isoduo::ListHandle> && ListWritesChange<isoduo::ListHandle>);
// A label takes either a read function or a full read source (#99). Braces around a single lambda match both and are
// ambiguous by design; every other spelling picks one.
template <class Label> concept LabelTakesFunction = requires(const Label& label) { label.connectSource([] { return std::u16string(); }); };
template <class Label> concept LabelTakesSource = requires(const Label& label, std::function<std::u16string()> read,
                                                             std::function<std::function<void()>(std::function<void()>)> subscribe,
                                                             isoduo::Source<std::u16string> source) {
    label.connectSource({read, subscribe});
    label.connectSource({{}, {}});
    label.connectSource(source);
};
template <class Label> concept LabelTakesBracedFunction = requires(const Label& label) { label.connectSource({[] { return std::u16string(); }}); };
static_assert(LabelTakesFunction<isoduo::LabelHandle> && LabelTakesSource<isoduo::LabelHandle> && !LabelTakesBracedFunction<isoduo::LabelHandle>);
// The pre-0.3.0 names are gone without aliases (#95). Unqualified lookup inside a namespace nested in isoduo finds isoduo's
// own declaration first and reaches these global stand-ins only when isoduo has none, so a check fails while an old name exists.
namespace removedIn03 { struct Name {}; }
using TextSource = removedIn03::Name;
using RichTextSource = removedIn03::Name;
using BoolSource = removedIn03::Name;
using NumericSource = removedIn03::Name;
using OptionSource = removedIn03::Name;
using TextChange = removedIn03::Name;
using RichTextChange = removedIn03::Name;
using BoolChange = removedIn03::Name;
using OptionChange = removedIn03::Name;
using NumericChange = removedIn03::Name;
using DropdownChange = removedIn03::Name;
namespace isoduo::consumerProbe {
template <class T> concept Removed = std::same_as<T, ::removedIn03::Name>;
static_assert(Removed<TextSource> && Removed<RichTextSource> && Removed<BoolSource> && Removed<NumericSource> && Removed<OptionSource>);
static_assert(Removed<TextChange> && Removed<RichTextChange> && Removed<BoolChange> && Removed<OptionChange> && Removed<NumericChange> && Removed<DropdownChange>);
}
// The text-only confirmation group is gone; text fields join the confirmation group that takes every field kind. The
// stand-in result converts to whatever the callback must return, so only the field list decides the check: the same call
// with the list of every field kind compiles (#96 gave that group the name).
struct AnyResult { template <class R> operator R() const { return R{}; } };
template <class T> concept HasTextOnlyConfirmation = requires(T& ui, isoduo::ButtonHandle button, std::vector<isoduo::TextFieldHandle> fields) {
    ui.connectConfirmation(button, fields, [](const auto&) { return AnyResult{}; });
};
template <class T> concept HasConfirmation = requires(T& ui, isoduo::ButtonHandle button, std::vector<isoduo::ConfirmField> fields) {
    ui.connectConfirmation(button, fields, [](const std::vector<isoduo::ConfirmFieldChange>&) { return AnyResult{}; });
};
static_assert(!HasTextOnlyConfirmation<isoduo::Ui>);
static_assert(HasConfirmation<isoduo::Ui>);
static_assert(std::same_as<std::invoke_result_t<decltype(&isoduo::Ui::connectConfirmation), isoduo::Ui&, isoduo::ButtonHandle, std::vector<isoduo::ConfirmField>,
    std::function<isoduo::ConfirmationResult(const std::vector<isoduo::ConfirmFieldChange>&)>>, isoduo::Result>);
static_assert(std::same_as<decltype(isoduo::ConfirmationResult::errors), std::vector<isoduo::ConfirmFieldError>>);
static_assert(std::same_as<decltype(isoduo::ConfirmFieldError::field), isoduo::ConfirmField> && std::same_as<decltype(isoduo::ConfirmFieldError::message), std::u16string>);

// Public names follow the glossary (#96). Each new name, and the pre-0.3.0 name it replaces, which is gone without an alias.
// A component type is a ComponentKind; add_x sets it, so the configuration has no kind field.
static_assert(std::is_enum_v<isoduo::ComponentKind> && std::same_as<decltype(isoduo::ComponentSnapshot::kind), isoduo::ComponentKind> &&
              std::same_as<decltype(isoduo::Placement::kind), isoduo::ComponentKind>);
template <class T> concept HasKind = requires(T value) { value.kind; };
static_assert(!HasKind<isoduo::ComponentConfig>);
// What a label says about its target is its LabelKind, on the configuration and the snapshot alike; role is the accessibility Role.
static_assert(std::same_as<decltype(isoduo::ComponentConfig::labelKind), isoduo::LabelKind> &&
              std::same_as<decltype(isoduo::ComponentSnapshot::labelKind), isoduo::LabelKind>);
static_assert(isoduo::LabelKind{} == isoduo::LabelKind::plain && isoduo::LabelKind::name != isoduo::LabelKind::description &&
              isoduo::LabelKind::description != isoduo::LabelKind::error);
template <class T> concept HasRole = requires(T value) { value.role; };
static_assert(!HasRole<isoduo::ComponentConfig> && !HasRole<isoduo::ComponentSnapshot> && HasRole<isoduo::NodeSnapshot>);
// One option of a radio group, a dropdown or a list is an OptionItem; its snapshot an OptionSnapshot, listed as items.
static_assert(std::same_as<decltype(isoduo::OptionItem::value), isoduo::OptionValue> && std::same_as<decltype(isoduo::OptionItem::label), std::u16string>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::items), std::vector<isoduo::OptionSnapshot>>);
template <class T> concept HasOptions = requires(T value) { value.options; };
static_assert(!HasOptions<isoduo::ComponentSnapshot>);
template <class T> concept SetsItems = requires(const T& handle, std::vector<isoduo::OptionItem> items) { { handle.setItems(items) } -> std::same_as<isoduo::Result>; };
template <class T> concept SetsOptions = requires(const T& handle, std::vector<isoduo::OptionItem> items) { handle.set_options(items); };
static_assert(SetsItems<isoduo::DropdownHandle> && SetsItems<isoduo::ListHandle> && !SetsOptions<isoduo::DropdownHandle>);
template <class T> concept HasAnchorItem = requires(T state) { { state.anchorItem } -> std::same_as<std::string&>; };
template <class T> concept HasAnchorOption = requires(T state) { state.anchor_option; };
static_assert(HasAnchorItem<isoduo::ScrollState> && !HasAnchorOption<isoduo::ScrollState>);
// Read-only snapshots are XxxSnapshot.
static_assert(std::same_as<decltype(isoduo::MenuSnapshot::items), std::vector<isoduo::MenuItemSnapshot>>);
static_assert(std::same_as<decltype(std::declval<isoduo::Ui&>().contextMenu()), isoduo::Query<isoduo::MenuSnapshot>>);
static_assert(std::same_as<decltype(std::declval<isoduo::ContextMenuHandle&>().items()), isoduo::Query<std::vector<isoduo::MenuItemSnapshot>>>);
static_assert(std::same_as<decltype(std::declval<isoduo::SemanticTree&>().get(isoduo::NodeRef{})), isoduo::Query<isoduo::NodeSnapshot>>);
static_assert(std::same_as<decltype(std::declval<isoduo::TextFieldHandle&>().describe()), isoduo::Query<isoduo::ComponentSnapshot>>);
// #101 keeps two snapshots, with one public action vocabulary and matching field shapes.
static_assert(!std::same_as<isoduo::ComponentSnapshot, isoduo::NodeSnapshot>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::verbs), std::vector<isoduo::Verb>>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::verbs), decltype(isoduo::NodeSnapshot::verbs)>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::range), std::optional<isoduo::SliderRange>>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::scroll), std::optional<isoduo::TextFieldScroll>>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::writingMode), std::optional<isoduo::WritingMode>>);
static_assert(std::same_as<decltype(isoduo::ComponentSnapshot::verticalDigits), std::optional<int>>);
template <class T> concept HasOperations = requires(T s) { s.operations; };
template <class T> concept HasSliderRange = requires(T s) { s.slider_range; };
template <class T> concept HasOldScroll = requires(T s) { s.visibleLines; } || requires(T s) { s.scrollLine; } || requires(T s) { s.lineCount; };
template <class T> concept HasBounds = requires(T s) { s.bounds; };
template <class T> concept HasInstance = requires(T s) { s.instance; };
template <class T> concept HasLabels = requires(T s) { s.labels; };
template <class T> concept HasLabelledBy = requires(T s) { s.labelledBy; };
static_assert(!HasOperations<isoduo::ComponentSnapshot> && !HasSliderRange<isoduo::ComponentSnapshot> && !HasOldScroll<isoduo::ComponentSnapshot>);
static_assert(!HasBounds<isoduo::ComponentSnapshot> && !HasInstance<isoduo::ComponentSnapshot> && !HasLabelledBy<isoduo::ComponentSnapshot>);
static_assert(HasLabels<isoduo::ComponentSnapshot> && !HasLabels<isoduo::NodeSnapshot> && HasLabelledBy<isoduo::NodeSnapshot>);
namespace removedIn04 { struct Name {}; }
using Operation = removedIn04::Name;
namespace isoduo::consumerProbe { static_assert(std::same_as<Operation, ::removedIn04::Name>); }
// A layout node's kind is a Node::Kind, and the node that places one component is made by layout::component.
static_assert(std::same_as<decltype(isoduo::layout::Node::kind), isoduo::layout::Node::Kind>);
static_assert(std::same_as<decltype(isoduo::layout::component(isoduo::ComponentHandle{})), isoduo::layout::Node>);
template <class T> concept HasType = requires(T node) { node.type; };
static_assert(!HasType<isoduo::layout::Node>);
template <class T> concept HasMixedConfirmation = requires { &T::connect_mixed_confirmation; };
static_assert(!HasMixedConfirmation<isoduo::Ui>);
namespace removedIn03 { inline Name control(const isoduo::ComponentHandle&, int = 0) { return {}; } }
using Kind = removedIn03::Name;
using Control = removedIn03::Name;
using TextRole = removedIn03::Name;
using RadioOption = removedIn03::Name;
using Semantics = removedIn03::Name;
using OptionSemantics = removedIn03::Name;
using MenuInfo = removedIn03::Name;
using MenuItemInfo = removedIn03::Name;
using SemanticNode = removedIn03::Name;
using MixedFieldError = removedIn03::Name;
using MixedConfirmationResult = removedIn03::Name;
using DesktopOptions = removedIn03::Name;
using DesktopCreation = removedIn03::Name;
using DiagnosticCategory = removedIn03::Name;
using removedIn03::control;
namespace isoduo::consumerProbe {
static_assert(Removed<Kind> && Removed<Control> && Removed<TextRole> && Removed<RadioOption>);
static_assert(Removed<Semantics> && Removed<OptionSemantics> && Removed<MenuInfo> && Removed<MenuItemInfo> && Removed<SemanticNode>);
static_assert(Removed<MixedFieldError> && Removed<MixedConfirmationResult>);
}
namespace isoduo::layout::consumerProbe {
static_assert(std::same_as<decltype(control(isoduo::ComponentHandle{})), ::removedIn03::Name>);
}
// #100 separates writable fields from the text-field component, without keeping the old public names.
// As above, lookup reaches a global stand-in only after the old declaration has been removed.
namespace removedBy100 { struct Name {}; }
using FieldParts = removedBy100::Name;
using FieldText = removedBy100::Name;
using FieldLayout = removedBy100::Name;
using FieldLine = removedBy100::Name;
using FieldView = removedBy100::Name;
using FieldChange = removedBy100::Name;
using FieldError = removedBy100::Name;
using TextHandle = removedBy100::Name;
using TextScroll = removedBy100::Name;
namespace isoduo::consumerProbe {
template <class T> concept RemovedBy100 = std::same_as<T, ::removedBy100::Name>;
static_assert(RemovedBy100<FieldParts> && RemovedBy100<FieldText> && RemovedBy100<FieldLayout>);
static_assert(RemovedBy100<FieldLine> && RemovedBy100<FieldView> && RemovedBy100<TextHandle> && RemovedBy100<TextScroll>);
static_assert(RemovedBy100<FieldChange> && RemovedBy100<FieldError>);
}
template <class T> concept HasOldTextEnumerator = requires { T::text; };
template <class T> concept HasOldAddText = requires(T& ui, isoduo::ComponentConfig config) { ui.add_text(config); };
template <class T> concept HasOldFieldMember = requires(T& value) { value.field; };
template <class T> concept HasOldFieldAppearance = requires(const T& look) { look.field(isoduo::Rect{}, 16); };
template <class T> concept HasOldArrangeField = requires(const T& look) { look.arrange_field(240, {}, 1.f); };
template <class T> concept HasOldArrangeFieldDensity = requires(const T& look) { look.arrange_field(240, {}, 1.f, isoduo::Density::compact); };
static_assert(!HasOldTextEnumerator<isoduo::ComponentKind> && !HasOldTextEnumerator<isoduo::Role>);
static_assert(!HasOldAddText<isoduo::Ui>);
static_assert(!HasOldFieldMember<isoduo::Placement> && !HasOldFieldMember<isoduo::ScrollRegion>);
static_assert(!HasOldFieldAppearance<isoduo::Appearance> && !HasOldArrangeField<isoduo::Appearance> && !HasOldArrangeFieldDensity<isoduo::Appearance>);
static_assert(std::same_as<decltype(std::declval<isoduo::Ui&>().addTextField(isoduo::ComponentConfig{})), isoduo::Query<isoduo::TextFieldHandle>>);
static_assert(isoduo::ComponentKind::textField != isoduo::ComponentKind::label && isoduo::Role::textField != isoduo::Role::label);
static_assert(std::same_as<decltype(isoduo::Placement::textField), isoduo::TextFieldLayout> &&
              std::same_as<decltype(isoduo::Placement::view), isoduo::TextFieldView>);
static_assert(std::same_as<decltype(isoduo::TextFieldView::lines), std::vector<isoduo::TextFieldLine>> &&
              std::same_as<decltype(isoduo::NodeSnapshot::scroll), std::optional<isoduo::TextFieldScroll>>);
static_assert(std::same_as<decltype(isoduo::ScrollRegion::textField), std::string>);
static_assert(std::same_as<decltype(std::declval<const isoduo::Appearance&>().arrangeTextField(240, isoduo::TextFieldHeights{}, 1.f)), isoduo::TextFieldLayout> &&
              std::same_as<decltype(std::declval<const isoduo::Appearance&>().arrangeTextField(240, isoduo::TextFieldHeights{}, 1.f, isoduo::Density::compact)), isoduo::TextFieldLayout>);
// The desktop module (when the package has it): creation parameters are a Config, creation hands back a Result, and a
// diagnostic says which kind of problem it is.
#if __has_include("isoduo/desktop.hpp")
#include "isoduo/desktop.hpp"
static_assert(std::same_as<decltype(isoduo::Desktop::create), isoduo::CreateResult(isoduo::Ui&, const isoduo::Appearance&, std::function<isoduo::layout::Node()>,
                                                                                 isoduo::ScrollState&, isoduo::DesktopConfig)>);
static_assert(std::same_as<decltype(isoduo::DesktopConfig::title), std::u16string>);
static_assert(std::same_as<decltype(isoduo::RenderDiagnostic::kind), isoduo::DiagnosticKind>);
template <class T> concept HasCategory = requires(T value) { value.category; };
static_assert(!HasCategory<isoduo::RenderDiagnostic>);
namespace isoduo::consumerProbe {
static_assert(Removed<DesktopOptions> && Removed<DesktopCreation> && Removed<DiagnosticCategory>);
}
#endif
// Handles compare within their own kind only (#94): equal when they control the same thing, a compile error across kinds.
static_assert(handleKinds::equalityHolds);
static_assert(handleKinds::constructionHolds);
static_assert(std::tuple_size_v<handleKinds::All> == 14);
static_assert(std::equality_comparable<isoduo::ComponentHandle> && std::equality_comparable<isoduo::ConfirmField>);
static_assert(!std::equality_comparable_with<isoduo::TextFieldHandle, isoduo::CheckboxHandle>);  // one case of the list's rule, spelled out
// Applications use typed handles; string operations belong to the semantic layer, not Ui.
template <class T> concept HasFind = requires(T& ui, std::string_view id) { ui.find(id); };
static_assert(!HasFind<isoduo::Ui>);
template <class T> concept HasText = requires(T& ui, std::string_view id) { ui.text(id); };
static_assert(!HasText<isoduo::Ui>);
template <class T> concept HasEdit = requires(T& ui, std::string_view id) { ui.edit(id); };
static_assert(!HasEdit<isoduo::Ui>);
template <class T> concept HasSetValue = requires(T& ui, std::string_view id) { ui.setValue(id, std::u16string{}); };
static_assert(!HasSetValue<isoduo::Ui>);
template <class T> concept HasInvoke = requires(T& ui, std::string_view id) { ui.invoke(id); };
static_assert(!HasInvoke<isoduo::Ui>);
template <class T> concept HasBind = requires(T& ui, std::string_view id) { ui.bind(std::string(id), [] { return isoduo::Result{}; }); };
static_assert(!HasBind<isoduo::Ui>);
template <class T> concept HasBindText = requires(T& ui, std::string_view id) { ui.bindText(id, [] { return std::u16string{}; }); };
static_assert(!HasBindText<isoduo::Ui>);
template <class T> concept HasFocus = requires(T& ui, std::string_view id) { ui.focus(id); };
static_assert(!HasFocus<isoduo::Ui>);
template <class T> concept HasPointerDown = requires(T& ui, std::string_view id) { ui.pointerDown(id); };
static_assert(!HasPointerDown<isoduo::Ui>);
template <class T> concept HasPointerUp = requires(T& ui, std::string_view id) { ui.pointerUp(id); };
static_assert(!HasPointerUp<isoduo::Ui>);
template <class T> concept HasUndo = requires(T& ui, std::string_view id) { ui.undo(id); };
static_assert(!HasUndo<isoduo::Ui>);
template <class T> concept HasRedo = requires(T& ui, std::string_view id) { ui.redo(id); };
static_assert(!HasRedo<isoduo::Ui>);
template <class T> concept HasPaste = requires(T& ui, std::string_view id) { ui.paste(id, std::u16string_view{}); };
static_assert(!HasPaste<isoduo::Ui>);
template <class T> concept HasSelect = requires(T& ui, std::string_view id) { ui.select(id, 0, 0); };
static_assert(!HasSelect<isoduo::Ui>);
template <class T> concept HasCopy = requires(T& ui, std::string_view id) { ui.copy(id); };
static_assert(!HasCopy<isoduo::Ui>);
template <class T> concept HasCut = requires(T& ui, std::string_view id) { ui.cut(id); };
static_assert(!HasCut<isoduo::Ui>);
template <class T> concept HasEnable = requires(T& ui, std::string_view id) { ui.enable(id, true); };
static_assert(!HasEnable<isoduo::Ui>);
template <class T> concept HasReadOnly = requires(T& ui, std::string_view id) { ui.setReadOnly(id, true); };
static_assert(!HasReadOnly<isoduo::Ui>);
template <class T> concept HasSetError = requires(T& ui, std::string_view id) { ui.setError(id, std::u16string{}); };
static_assert(!HasSetError<isoduo::Ui>);
template <class T> concept HasReplaceText = requires(T& ui, std::string_view id) { ui.replaceText(id, std::u16string{}); };
static_assert(!HasReplaceText<isoduo::Ui>);
template <class T> concept HasAvailable = requires(T& ui, std::string_view id) { ui.available(id); };
static_assert(!HasAvailable<isoduo::Ui>);
template <class T> concept HasInput = requires(T& ui, std::string_view id) { ui.textInput(std::u16string_view{}); };
static_assert(!HasInput<isoduo::Ui>);

template<class T> concept FindsByString = requires(T& frame, std::string_view id) { frame.find(id); };
static_assert(!FindsByString<isoduo::Frame>);
template<class T> concept LaysOutString = requires(T id) { isoduo::layout::component(id); };
static_assert(!LaysOutString<std::string>);

namespace {
// The application's pre-existing data model; Isoduo never owns or copies it.
struct Profile {
    std::u16string alias = u"guest";
    int saves = 0;
};
// Monospace stand-in for a real shaping backend.
struct Monospace final : isoduo::TextMeasure {
    isoduo::TextRun measure(std::u16string_view text, int pixels) override {
        isoduo::TextRun run;
        for (unsigned i = 0; i <= text.size(); ++i) run.stops.push_back({i, i * pixels * 0.6f});
        run.width = text.size() * pixels * 0.6f;
        return run;
    }
    int lineHeight(int pixels) override { return pixels + 4; }
};
// The component type a made component reports, or nothing when it was not made.
template <class Handle> std::optional<isoduo::ComponentKind> kindOf(const isoduo::Query<Handle>& made) {
    if (!made) return std::nullopt;
    const auto described = made->describe();
    return described ? std::optional<isoduo::ComponentKind>(described->kind) : std::nullopt;
}
int failures = 0;
void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}

int main() {
    using namespace isoduo;
    Profile profile;
    std::function<void()> changed;
    Ui ui;
    ui.addLabel({.id = "profile.alias.caption", .value = u"Alias", .target = "profile.alias", .labelKind = LabelKind::name}).value.value();
    auto alias = ui.addTextField({.id = "profile.alias", .maxLength = 12}).value.value();
    ui.addLabel({.id = "profile.alias.problem", .target = "profile.alias", .labelKind = LabelKind::error}).value.value();
    auto save = ui.addButton({.id = "profile.save", .label = u"Save alias"}).value.value();
    auto summary = ui.addLabel({.id = "profile.summary"}).value.value();
    alias.connectSource({[&] { return profile.alias; }, [&](std::function<void()> callback) {
        changed = std::move(callback);
        return [&] { changed = {}; };
    }});
    summary.connectSource([&] { return u"Saved " + profile.alias; });
    ui.connectConfirmation(save, {alias}, [&](const std::vector<ConfirmFieldChange>& changes) {
        const auto& change = std::get<Change<TextFieldHandle>>(changes[0]);
        if (change.value.size() < 3) {
            return ConfirmationResult{{Status::invalidValue, u"At least three characters"}, {{change.field, u"At least three characters"}}};
        }
        profile.alias = change.value;
        ++profile.saves;
        if (changed) changed();
        return ConfirmationResult{};
    });

    alias.focus();
    alias.setValue(u"ab");
    auto handleStatus = save.invoke().status;
    auto semanticStatus = ui.semantics().perform({"profile.save"}, Verb::invoke).status;
    expect(handleStatus == Status::invalidValue && semanticStatus == handleStatus && profile.saves == 0, "consumer validation agrees through both interfaces");
    expect(alias.describe()->error == u"At least three characters", "consumer error is attached to its own field");
    expect(ui.semantics().perform({"profile.alias"}, Verb::setValue, std::u16string(u"abcdef")).status == Status::ok &&
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
    expect(alias.paste(u"0123456789").status == Status::invalidValue, "consumer-chosen length limit is enforced");

    Monospace measure;
    ScrollState scroll;
    auto page = layout::column({layout::component(alias), layout::row({layout::component(save), layout::component(summary)})}, 8, 16);
    auto frame = arrange(page, ui, materialAppearance(), measure, 480, 320, 1.f, scroll);
    const auto* field = frame.find(alias);
    expect(field && field->labelLines.size() == 1 && field->labelLines[0].text == u"Alias", "framework layout carries the consumer's label");
    const auto* placement = frame.find(save);
    const auto* hit = placement ? frame.hit(placement->bounds.x + 1, placement->bounds.y + 1) : nullptr;
    expect(hit && hit->id == "profile.save", "consumer IDs are hit-testable");
    // add_x gives the component its type (#96): no configuration names it, and each component is what its add_x makes.
    Ui typed;
    const std::vector<OptionItem> items = {{{"a"}, u"Alpha"}, {{"b"}, u"Beta"}};
    expect(kindOf(typed.addTextField({.id = "typed.text"})) == ComponentKind::textField, "add_text_field makes a text field");
    expect(kindOf(typed.addButton({.id = "typed.button", .label = u"Go"})) == ComponentKind::button, "add_button makes a button");
    expect(kindOf(typed.addLabel({.id = "typed.label", .value = u"Note"})) == ComponentKind::label, "add_label makes a label");
    expect(kindOf(typed.addCheckbox({.id = "typed.checkbox", .label = u"On"})) == ComponentKind::checkbox, "add_checkbox makes a checkbox");
    expect(kindOf(typed.addRadioGroup({.id = "typed.radio", .label = u"Pick"}, items)) == ComponentKind::radioGroup, "add_radio_group makes a radio group");
    expect(kindOf(typed.addSlider({.id = "typed.slider", .label = u"Level"}, {0, 10, 1})) == ComponentKind::slider, "add_slider makes a slider");
    expect(kindOf(typed.addProgress({.id = "typed.progress", .label = u"Done"})) == ComponentKind::progress, "add_progress makes a progress display");
    expect(kindOf(typed.addDropdown({.id = "typed.dropdown", .label = u"Region"}, items)) == ComponentKind::dropdown, "add_dropdown makes a dropdown");
    expect(kindOf(typed.addList({.id = "typed.list", .label = u"Rows"}, items)) == ComponentKind::list, "add_list makes a list");
    expect(kindOf(typed.addTabs({.id = "typed.tabs", .label = u"Pages"}, {{{"one"}, u"One"}})) == ComponentKind::tabs, "add_tabs makes tabs");
    expect(kindOf(typed.addNavigationList({.id = "typed.navigation", .label = u"Go to"}, {{{"home"}, u"Home"}}, {"home"})) ==
           ComponentKind::navigationList, "add_navigation_list makes a navigation list");
    std::printf("%s\n", failures ? "consumer FAILED" : "consumer OK");
    return failures ? 1 : 0;
}
