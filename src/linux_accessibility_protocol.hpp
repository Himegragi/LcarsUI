#pragma once

namespace isoduo::detail::atspi {
inline constexpr auto prefix = "org.a11y.atspi.";
inline constexpr auto objectBase = "/org/a11y/atspi/accessible";
inline constexpr auto applicationPath = "/org/a11y/atspi/accessible/root";
inline constexpr auto cachePath = "/org/a11y/atspi/cache";
inline constexpr auto nullPath = "/org/a11y/atspi/null";
inline constexpr auto registry = "org.a11y.atspi.Registry";

// AT-SPI wire values. These are shared with clients independently of libatspi.
enum Role : unsigned {
    checkBox = 7, comboBox = 11, dialog = 16, frame = 23, label = 29, list = 31, listItem = 32,
    menuItem = 35, pageTab = 37, pageTabList = 38, panel = 39, popupMenu = 41, progressBar = 42,
    button = 43, radioButton = 44, separator = 50, slider = 51, toolTip = 64, application = 75, entry = 79
};
enum State : unsigned {
    checked = 4, defunct = 6, editable = 7, enabled = 8, expandable = 9, expanded = 10,
    focusable = 11, focused = 12, modal = 16, multiLine = 17, selectable = 22, selected = 23,
    sensitive = 24, showing = 25, singleLine = 26, visible = 30, invalidEntry = 36, readOnly = 43
};
enum Coordinate : unsigned { screen = 0, window = 1, parent = 2 };
enum Relation : unsigned { labelFor = 1, labelledBy = 2, descriptionFor = 17, describedBy = 18 };
// The first seven values are the legacy AtspiTextBoundaryType wire values.
enum class TextBoundaryKind : unsigned {
    character, wordStart, wordEnd, sentenceStart, sentenceEnd, lineStart, lineEnd, paragraphStart
};

#define ISO_IFACE(name) "<interface name='org.a11y.atspi." #name "'>"
#define ISO_PROPERTY(name, type, access) "<property name='" #name "' type='" type "' access='" access "'/>"
#define ISO_READ(name, type) ISO_PROPERTY(name, type, "read")
#define ISO_VERSION ISO_READ(version, "u")
#define ISO_METHOD(name, args) "<method name='" #name "'>" args "</method>"
#define ISO_IN(type) "<arg direction='in' type='" type "'/>"
#define ISO_OUT(type) "<arg direction='out' type='" type "'/>"

inline constexpr auto interfaces =
    "<node>"
    ISO_IFACE(Accessible) ISO_VERSION
        ISO_READ(Name, "s") ISO_READ(Description, "s") ISO_READ(Parent, "(so)")
        ISO_READ(ChildCount, "i") ISO_READ(AccessibleId, "s") ISO_READ(Locale, "s")
        ISO_METHOD(GetChildAtIndex, ISO_IN("i") ISO_OUT("(so)"))
        ISO_METHOD(GetChildren, ISO_OUT("a(so)"))
        ISO_METHOD(GetIndexInParent, ISO_OUT("i"))
        ISO_METHOD(GetRelationSet, ISO_OUT("a(ua(so))"))
        ISO_METHOD(GetRole, ISO_OUT("u")) ISO_METHOD(GetRoleName, ISO_OUT("s"))
        ISO_METHOD(GetLocalizedRoleName, ISO_OUT("s")) ISO_METHOD(GetState, ISO_OUT("au"))
        ISO_METHOD(GetAttributes, ISO_OUT("a{ss}")) ISO_METHOD(GetApplication, ISO_OUT("(so)"))
        ISO_METHOD(GetInterfaces, ISO_OUT("as"))
    "</interface>"
    ISO_IFACE(Application)
        ISO_READ(ToolkitName, "s") ISO_READ(Version, "s") ISO_READ(ToolkitVersion, "s")
        ISO_READ(AtspiVersion, "s") ISO_READ(InterfaceVersion, "u") ISO_PROPERTY(Id, "i", "readwrite")
        ISO_METHOD(GetLocale, ISO_IN("u") ISO_OUT("s"))
        ISO_METHOD(GetApplicationBusAddress, ISO_OUT("s"))
    "</interface>"
    ISO_IFACE(Cache) ISO_VERSION
        ISO_METHOD(GetItems, ISO_OUT("a((so)(so)(so)iiassusau)"))
    "</interface>"
    ISO_IFACE(Action) ISO_VERSION ISO_READ(NActions, "i")
        ISO_METHOD(GetDescription, ISO_IN("i") ISO_OUT("s"))
        ISO_METHOD(GetName, ISO_IN("i") ISO_OUT("s"))
        ISO_METHOD(GetLocalizedName, ISO_IN("i") ISO_OUT("s"))
        ISO_METHOD(GetKeyBinding, ISO_IN("i") ISO_OUT("s"))
        ISO_METHOD(GetActions, ISO_OUT("a(sss)"))
        ISO_METHOD(DoAction, ISO_IN("i") ISO_OUT("b"))
    "</interface>"
    ISO_IFACE(Component) ISO_VERSION
        ISO_METHOD(Contains, ISO_IN("i") ISO_IN("i") ISO_IN("u") ISO_OUT("b"))
        ISO_METHOD(GetAccessibleAtPoint, ISO_IN("i") ISO_IN("i") ISO_IN("u") ISO_OUT("(so)"))
        ISO_METHOD(GetExtents, ISO_IN("u") ISO_OUT("(iiii)"))
        ISO_METHOD(GetPosition, ISO_IN("u") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetSize, ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetLayer, ISO_OUT("u"))
        ISO_METHOD(GetMDIZOrder, ISO_OUT("n"))
        ISO_METHOD(GetAlpha, ISO_OUT("d"))
        ISO_METHOD(GrabFocus, ISO_OUT("b"))
    "</interface>"
    ISO_IFACE(Selection) ISO_VERSION ISO_READ(NSelectedChildren, "i")
        ISO_METHOD(GetSelectedChild, ISO_IN("i") ISO_OUT("(so)"))
        ISO_METHOD(SelectChild, ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(IsChildSelected, ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(DeselectChild, ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(DeselectSelectedChild, ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(ClearSelection, ISO_OUT("b"))
        ISO_METHOD(SelectAll, ISO_OUT("b"))
    "</interface>"
    ISO_IFACE(Value) ISO_VERSION
        ISO_READ(MinimumValue, "d") ISO_READ(MaximumValue, "d") ISO_READ(MinimumIncrement, "d")
        ISO_PROPERTY(CurrentValue, "d", "readwrite") ISO_READ(Text, "s")
    "</interface>"
    ISO_IFACE(Text) ISO_VERSION ISO_READ(CharacterCount, "i") ISO_READ(CaretOffset, "i")
        ISO_METHOD(GetText, ISO_IN("i") ISO_IN("i") ISO_OUT("s"))
        ISO_METHOD(GetCharacterAtOffset, ISO_IN("i") ISO_OUT("i"))
        ISO_METHOD(GetStringAtOffset, ISO_IN("i") ISO_IN("u") ISO_OUT("s") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetTextAtOffset, ISO_IN("i") ISO_IN("u") ISO_OUT("s") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetTextBeforeOffset, ISO_IN("i") ISO_IN("u") ISO_OUT("s") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetTextAfterOffset, ISO_IN("i") ISO_IN("u") ISO_OUT("s") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetNSelections, ISO_OUT("i"))
        ISO_METHOD(GetSelection, ISO_IN("i") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(SetSelection, ISO_IN("i") ISO_IN("i") ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(AddSelection, ISO_IN("i") ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(RemoveSelection, ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(SetCaretOffset, ISO_IN("i") ISO_OUT("b"))
        ISO_METHOD(GetCharacterExtents, ISO_IN("i") ISO_IN("u") ISO_OUT("i") ISO_OUT("i") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetRangeExtents, ISO_IN("i") ISO_IN("i") ISO_IN("u") ISO_OUT("i") ISO_OUT("i") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetOffsetAtPoint, ISO_IN("i") ISO_IN("i") ISO_IN("u") ISO_OUT("i"))
        ISO_METHOD(GetAttributes, ISO_IN("i") ISO_OUT("a{ss}") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetAttributeRun, ISO_IN("i") ISO_IN("b") ISO_OUT("a{ss}") ISO_OUT("i") ISO_OUT("i"))
        ISO_METHOD(GetAttributeValue, ISO_IN("i") ISO_IN("s") ISO_OUT("s"))
        ISO_METHOD(GetDefaultAttributes, ISO_OUT("a{ss}")) ISO_METHOD(GetDefaultAttributeSet, ISO_OUT("a{ss}"))
        ISO_METHOD(ScrollSubstringTo, ISO_IN("i") ISO_IN("i") ISO_IN("u") ISO_OUT("b"))
    "</interface>"
    ISO_IFACE(EditableText) ISO_VERSION
        ISO_METHOD(SetTextContents, ISO_IN("s") ISO_OUT("b"))
    "</interface>"
    "</node>";

#undef ISO_IFACE
#undef ISO_PROPERTY
#undef ISO_READ
#undef ISO_VERSION
#undef ISO_METHOD
#undef ISO_IN
#undef ISO_OUT
}
