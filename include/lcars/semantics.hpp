#pragma once
#include "lcars/core.hpp"
#include "lcars/appearance.hpp"

namespace lcars {
struct NodeRef { std::string id; std::optional<OptionValue> item = {}; bool operator==(const NodeRef&) const = default; };
enum class Role { text, button, label, checkbox, radio_group, radio_option, slider, dropdown, dropdown_option, list, list_item, tabs, tab_item, progress, dialog, tooltip, menu, menu_item, menu_separator };
// set_text_align takes a TextAlign, set_justify_method a JustifyMethod, set_line_spacing a double, set_paragraph_spacing an int (DIP); labels and text fields.
// set_writing_mode takes a WritingMode and set_vertical_digits an int (0-3); labels only.
// context_menu opens a component's menu (the argument may be an OptionValue naming a list row, or a
// MenuPosition); cancel on the menu node closes it; invoke on a menu item runs it.
// hover simulates the pointer resting on the node (or on one of its items); it is accepted whenever the
// typed Ui::simulate_hover would be, and is never listed in NodeSnapshot::verbs (hover is not an operation of the node).
// select_word / select_paragraph / double_click / triple_click take an int UTF-16 offset (the character starting there);
// set_word_segmentation takes a WordSegmentation. All are text fields only.
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus, undo, redo, copy, cut, paste, confirm, scroll, set_text_align, set_line_spacing, set_paragraph_spacing, context_menu, set_writing_mode, set_vertical_digits, hover, set_style, set_typing_style,
                  set_word_segmentation, select_word, select_paragraph, double_click, triple_click, set_justify_method };
// The text units a semantic query can ask for: what a double click selects and what a triple click selects.
enum class TextUnitKind { word, paragraph };
// Rich text fields: apply a style change to the range [begin, end) of the committed text (Verb::set_style).
struct StyleEdit { unsigned begin = 0, end = 0; StyleChange change; };
// Rich fields also take a RichText for set_value, a StyleEdit for set_style and a TextStyle for set_typing_style.
using Argument = std::variant<std::monostate, std::u16string, int, Selection, OptionValue, TextAlign, double, MenuPosition, WritingMode, RichText, StyleEdit, TextStyle, WordSegmentation, JustifyMethod>;
enum class SemanticEventKind { focus_changed, property_changed, structure_changed, text_changed, text_selection_changed, layout_changed, menu_opened, menu_closed };
// An open menu is a child node of its component with this id; its items are {menu id, item id}.
inline std::string menu_node_id(std::string_view target) { return std::string(target) + "/menu"; }
inline std::optional<std::string> menu_target_id(std::string_view node) {
    constexpr std::string_view suffix = "/menu";
    if (node.size() <= suffix.size() || node.substr(node.size() - suffix.size()) != suffix) return std::nullopt;
    return std::string(node.substr(0, node.size() - suffix.size()));
}
struct SemanticEvent {
    SemanticEventKind kind; NodeRef ref; std::string property;
    // Overlay visibility is captured at the transition, not at delivery time.
    std::optional<bool> hidden;
};
class Subscription {
public:
    explicit Subscription(std::function<void()> disconnect = {}) : disconnect_(std::move(disconnect)) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept : disconnect_(std::exchange(other.disconnect_, {})) {}
    Subscription& operator=(Subscription&& other) noexcept { if (this != &other) { reset(); disconnect_ = std::exchange(other.disconnect_, {}); } return *this; }
    ~Subscription() { reset(); }
    void reset() { if (auto f = std::exchange(disconnect_, {})) f(); }
private:
    std::function<void()> disconnect_;
};
// Multi-line text fields: the vertical view in display lines. scroll takes the first visible line as an int.
struct TextScroll { unsigned first_line = 0, visible_lines = 0, line_count = 0; bool operator==(const TextScroll&) const = default; };
struct NodeSnapshot {
    NodeRef ref; Role role = Role::label;
    std::uint64_t instance = 0; // Changes when an identifier is removed and reused.
    std::u16string name, value, description, error, accelerator_key, access_key;
    bool enabled = true, read_only = false, focusable = false, focused = false, selected = false, expanded = false, invalid = false, executing = false;
    bool hidden = false, visible = false, blocked_by_modal = false;
    bool selection_required = false;
    std::optional<bool> checked;
    std::optional<SliderRange> range; std::optional<int> numeric_value;
    std::optional<Rect> bounds;
    std::optional<WritingMode> writing_mode;  // labels only
    std::optional<int> vertical_digits;       // labels only
    std::vector<NodeRef> labelled_by, described_by;
    std::vector<Verb> verbs;
    std::optional<Selection> text_selection;
    std::optional<TextScroll> scroll;  // multi-line text fields
    TextAlign text_align = TextAlign::left;  // labels and text fields; the defaults elsewhere. As set: the user's "justified text as left" setting does not change it
    JustifyMethod justify_method = JustifyMethod::automatic;
    double line_spacing = 1.0;
    int paragraph_spacing = 0;
    bool rich = false;                 // text fields created with ComponentConfig::rich
    std::vector<StyleSpan> styles;     // rich fields: the styled runs of value, in normal form
    std::optional<TextStyle> typing_style;  // rich fields: the style the next typed text takes
    std::optional<WordSegmentation> word_segmentation, word_segmentation_in_use;  // text fields: as set, and what works now
    std::uint64_t text_revision = 0; // changes when committed text changes
};
// UI-thread-only live queries; the tree does not own its Ui.
class SemanticTree {
public:
    Query<NodeSnapshot> get(const NodeRef& ref) const;
    Query<std::vector<NodeRef>> children(const NodeRef& parent) const;
    Query<std::u16string> text(std::string_view id) const;
    // Same answer and admission as Ui::animating: is any on-screen transition still running?
    Query<bool> animating() const;
    // Text fields: the range of the word or paragraph holding the character at a UTF-16 offset, and the next or
    // previous word stop (where Ctrl+Right / Ctrl+Left go). The same answers as the typed TextHandle.
    Query<TextSpan> text_unit(std::string_view id, TextUnitKind unit, unsigned offset) const;
    Query<unsigned> word_stop(std::string_view id, unsigned offset, bool forward) const;
    // Read-only: the colors currently painted for the bound appearance and the current theme variant,
    // overrides applied. not_found until an appearance is bound (see Ui::bind_appearance).
    Query<Palette> effective_palette() const;
    // The node (and item) the pointer hovers, from the real mouse or from Verb::hover; empty when none.
    // Same answer as Ui::hover. UIA never reports hover, so neither node properties nor events change with it.
    Query<std::optional<NodeRef>> hover() const;
    Result clear_simulated_hover();
    // Rich fields: the color a color name is painted in right now: the field's color for the current theme
    // variant, or the theme's normal text color for an empty or unknown name (which needs a bound appearance).
    Query<Color> text_color(const NodeRef& ref, std::string_view name) const;
    Result perform(const NodeRef& ref, Verb verb, Argument argument = {});
    // Callbacks run on the UI thread. The desktop host dispatches queued notifications;
    // core-only hosts call dispatch_events() at their own safe event-loop boundary.
    Subscription subscribe(std::function<void(const SemanticEvent&)> callback);
    void dispatch_events();
private:
    friend class Ui;
    friend class detail::UiState;
    explicit SemanticTree(detail::UiState& state) : state_(&state) {}
    detail::UiState* state_;
};
}
