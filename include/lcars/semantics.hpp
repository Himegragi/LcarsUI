#pragma once
#include "lcars/core.hpp"
#include "lcars/appearance.hpp"

namespace lcars {
struct NodeRef { std::string id; std::optional<OptionValue> item = {}; bool operator==(const NodeRef&) const = default; };
enum class Role { text, button, label, checkbox, radio_group, radio_option, slider, dropdown, dropdown_option, list, list_item, tabs, tab_item, progress, dialog, tooltip };
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus, undo, redo, copy, cut, paste, confirm, scroll, set_style, set_typing_style };
// Rich text fields: apply a style change to the range [begin, end) of the committed text (Verb::set_style).
struct StyleEdit { unsigned begin = 0, end = 0; StyleChange change; };
// Rich fields also take a RichText for set_value, a StyleEdit for set_style and a TextStyle for set_typing_style.
using Argument = std::variant<std::monostate, std::u16string, int, Selection, OptionValue, RichText, StyleEdit, TextStyle>;
enum class SemanticEventKind { focus_changed, property_changed, structure_changed, text_changed, text_selection_changed, layout_changed };
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
struct SemanticNode {
    NodeRef ref; Role role = Role::label;
    std::uint64_t instance = 0; // Changes when an identifier is removed and reused.
    std::u16string name, value, description, error, accelerator_key;
    bool enabled = true, read_only = false, focusable = false, focused = false, selected = false, expanded = false, invalid = false, executing = false;
    bool hidden = false, visible = false, blocked_by_modal = false;
    bool selection_required = false;
    std::optional<bool> checked;
    std::optional<SliderRange> range; std::optional<int> numeric_value;
    std::optional<Rect> bounds;
    std::vector<NodeRef> labelled_by, described_by;
    std::vector<Verb> verbs;
    std::optional<Selection> text_selection;
    std::optional<TextScroll> scroll;  // multi-line text fields
    bool rich = false;                 // text fields created with Control::rich
    std::vector<StyleSpan> styles;     // rich fields: the styled runs of value, in normal form
    std::optional<TextStyle> typing_style;  // rich fields: the style the next typed text takes
    std::uint64_t text_revision = 0; // changes when committed text changes
};
// UI-thread-only live queries; the tree does not own its Ui.
class SemanticTree {
public:
    Query<SemanticNode> get(const NodeRef& ref) const;
    Query<std::vector<NodeRef>> children(const NodeRef& parent) const;
    Query<std::u16string> text(std::string_view id) const;
    // Read-only: the colors currently painted for the bound appearance and the current theme variant,
    // overrides applied. not_found until an appearance is bound (see Ui::bind_appearance).
    Query<Palette> effective_palette() const;
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
