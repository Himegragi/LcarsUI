#pragma once
#include "lcars/core.hpp"
#include "lcars/appearance.hpp"

namespace lcars {
struct NodeRef { std::string id; std::optional<OptionValue> item = {}; bool operator==(const NodeRef&) const = default; };
enum class Role { text_field, button, label, checkbox, radio_group, radio_option, slider, dropdown, dropdown_option, list, list_item, tabs, tab_item, progress, dialog, tooltip, menu, menu_item, menu_separator };
// The text units a semantic query can ask for: what a double click selects and what a triple click selects.
enum class TextUnitKind { word, paragraph };
// Rich text fields: apply a style change to the range [begin, end) of the committed text (Verb::set_style).
struct StyleEdit { unsigned begin = 0, end = 0; StyleChange change; };
// Rich text fields also take a RichText for set_value, a StyleEdit for set_style and a TextStyle for set_typing_style.
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
// Option nodes describe only the option itself; properties it does not have keep their default values.
struct NodeSnapshot : CommonState {
    NodeRef ref; Role role = Role::label;
    std::uint64_t instance = 0; // Changes when an identifier is removed and reused.
    std::u16string accelerator_key, access_key;
    bool selected = false;
    // Options share their component's hidden state. A collapsed dropdown's options are not hidden;
    // they have visible == false and no bounds instead.
    bool hidden = false;
    // Options share their component's modal blocking state.
    bool blocked_by_modal = false;
    bool selection_required = false;
    std::optional<Rect> bounds;
    std::vector<NodeRef> labelled_by, described_by;
    std::optional<Selection> text_selection;
    std::vector<StyleSpan> styles;     // rich text fields: the styled runs of value, in normal form
    std::optional<TextStyle> typing_style;  // rich text fields: the style the next typed text takes
    std::optional<WordSegmentation> word_segmentation, word_segmentation_in_use;  // text fields: as set, and what works now
    std::uint64_t text_revision = 0; // changes when committed text changes
    bool operator==(const NodeSnapshot&) const = default;
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
    // previous word stop (where Ctrl+Right / Ctrl+Left go). The same answers as the typed TextFieldHandle.
    Query<TextSpan> text_unit(std::string_view id, TextUnitKind unit, unsigned offset) const;
    Query<unsigned> word_stop(std::string_view id, unsigned offset, bool forward) const;
    // Read-only: the colors currently painted for the bound appearance and the current theme variant,
    // overrides applied. not_found until an appearance is bound (see Ui::bind_appearance).
    Query<Palette> effective_palette() const;
    // The node (and item) the pointer hovers, from the real mouse or from Verb::hover; empty when none.
    // Same answer as Ui::hover. UIA never reports hover, so neither node properties nor events change with it.
    Query<std::optional<NodeRef>> hover() const;
    Result clear_simulated_hover();
    // Rich text fields: the color a color name is painted in right now: the text field's color for the current theme
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
