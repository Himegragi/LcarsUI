#pragma once
#include "lcars/core.hpp"
#include "lcars/appearance.hpp"

namespace lcars {
struct NodeRef { std::string id; std::optional<OptionValue> item = {}; bool operator==(const NodeRef&) const = default; };
enum class Role { text, button, label, checkbox, radio_group, radio_option, slider, dropdown, dropdown_option, list, list_item, tabs, tab_item, progress, dialog, tooltip };
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus, undo, redo, copy, cut, paste, confirm };
using Argument = std::variant<std::monostate, std::u16string, int, Selection>;
struct SemanticNode {
    NodeRef ref; Role role = Role::label;
    std::u16string name, value, description, error;
    bool enabled = true, read_only = false, focusable = false, focused = false, selected = false, expanded = false, invalid = false, executing = false;
    bool hidden = false, visible = false, blocked_by_modal = false;
    std::optional<bool> checked;
    std::optional<SliderRange> range; std::optional<int> numeric_value;
    std::optional<Rect> bounds;
    std::vector<NodeRef> labelled_by, described_by;
    std::optional<Selection> text_selection;
};
// UI-thread-only live queries; the tree does not own its Ui.
class SemanticTree {
public:
    explicit SemanticTree(detail::UiState& state) : state_(&state) {}
    Query<SemanticNode> get(const NodeRef& ref) const;
    Query<std::vector<NodeRef>> children(const NodeRef& parent) const;
    Query<std::u16string> text(std::string_view id) const;
    Result perform(const NodeRef& ref, Verb verb, Argument argument = {});
private:
    detail::UiState* state_;
};
}
