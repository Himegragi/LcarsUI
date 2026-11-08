#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <variant>

namespace lcars {
// label is C01 display text: never focusable, may span lines, may describe another control.
enum class Kind { text, button, label, checkbox, radio_group, slider, dropdown, list, tabs, progress, navigation_list };
// What a label says about its target. Association is by logical ID, never by position or appearance.
enum class TextRole { plain, name, description, error };
enum class Status { ok, not_found, disabled, read_only, blocked_by_modal, busy, invalid_value, wrong_thread, failed, not_supported, empty_history, hidden };
struct Result {
    Status status = Status::ok;
    std::u16string message;
    explicit operator bool() const { return status == Status::ok; }
};
// A query reports failure separately from a legitimate false or empty value.
template<class T> struct Query {
    Status status = Status::ok;
    std::u16string message;
    std::optional<T> value;
    explicit operator bool() const { return status == Status::ok && value.has_value(); }
    const T* operator->() const { return &value.value(); }
    const T& operator*() const { return value.value(); }
};
// Public text field limit in UTF-16 units. A field may choose its own; an edit that would
// exceed it is rejected whole, leaving text and history untouched.
inline constexpr std::size_t default_max_text_length = 4096;
struct Control {
    std::string id;
    Kind kind;
    std::u16string label = {};  // fallback accessible name when no name label targets this control
    std::string page = {};
    std::u16string value = {};  // text field content, or label display text
    bool enabled = true;
    bool read_only = false;
    bool checked = false;
    std::string target = {};         // labels: the control this text names, describes or reports errors for
    TextRole role = TextRole::plain;
    std::size_t max_length = default_max_text_length;  // text fields only
};
// Public operations a control currently admits; restricted ones are omitted, not just flagged.
enum class Operation { focus, activate, select, copy, edit, cut, paste, undo, redo };
struct OptionValue { std::string id; bool operator==(const OptionValue&) const = default; };
struct RadioOption { OptionValue value; std::u16string label; bool enabled = true; };
struct SliderRange { int minimum, maximum, step; };
struct OptionSemantics { OptionValue value; std::u16string label; bool enabled = true, selected = false, invalid = false; };
// Semantic snapshot from a handle or a host-side adapter.
struct Semantics {
    std::string id;
    Kind kind = Kind::label;
    std::u16string name, value, description, error;
    bool enabled = true, read_only = false, focusable = false, focused = false, invalid = false, executing = false, source_conflict = false;
    bool visible = false; // intersects the clip in the latest arranged frame
    std::vector<OptionSemantics> options;
    std::optional<SliderRange> slider_range;
    std::optional<int> numeric_value;
    std::optional<bool> checked;  // present for a checkbox
    bool expanded = false;
    std::optional<OptionValue> candidate;
    std::string target;
    TextRole role = TextRole::plain;
    std::vector<std::string> labels;  // labels whose target is this control, in declaration order
    std::vector<Operation> operations;
};
// Platform-neutral keys. Hosts translate native events; Ctrl shortcuts carry their letter.
enum class Key { other, tab, enter, space, escape, left, right, up, down, home, end, backspace, del };
struct KeyPress {
    Key key = Key::other;
    bool shift = false, ctrl = false, repeat = false;
    char16_t letter = 0;  // uppercase ASCII letter for Ctrl shortcuts, e.g. u'Z'
};
struct Shortcut { Key key = Key::other; char16_t letter = 0; bool ctrl = false, shift = false; };
struct Selection { unsigned anchor = 0, caret = 0; };
// Host-provided system clipboard. Core never touches OS services directly.
class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual bool write(std::u16string_view text) = 0;
    // not_supported means no text; failed means the clipboard could not be read.
    virtual Query<std::u16string> read() = 0;
};
// Pasted CRLF counts as one line break, lone CR and LF as one each; every break becomes a space.
std::u16string paste_as_single_line(std::u16string_view text);
// One session owns text and transient composition. Offsets are UTF-16.
// The text backend supplies navigation stops; this is not a Unicode segmenter.
class EditSession {
public:
    std::u16string text;
    Selection selection;
    bool composing = false;
    std::u16string preedit;
    unsigned preedit_caret = 0;
    unsigned begin() const { return std::min(selection.anchor, selection.caret); }
    unsigned end() const { return std::max(selection.anchor, selection.caret); }
    void select(unsigned anchor, unsigned caret);
    void start_composition();
    void update_composition(std::u16string value, unsigned caret);
    void cancel_composition();
    std::u16string visible() const;
    unsigned visible_caret() const { return composing ? begin() + preedit_caret : selection.caret; }
};
// Well-formed UTF-16, no control characters, within the given length.
bool valid_single_line(std::u16string_view text, std::size_t max_length = default_max_text_length);
// The application owns the model. Notifications run on the UI thread; the source outlives the
// connection. Its subscription returns a disconnection function.
struct TextSource {
    std::function<std::u16string()> read;
    std::function<std::function<void()>(std::function<void()>)> subscribe;
};
struct BoolSource {
    std::function<bool()> read;
    std::function<std::function<void()>(std::function<void()>)> subscribe;
};
// Integer sources also feed read-only progress displays, which accept values outside 0-100.
// Integer slider values are the minimum plus whole steps, with the maximum also valid.
// In-range edits snap to the closest valid value; ties choose the higher value.
// Programmatic values outside the range are rejected; pointer and keyboard input clamp.
// Arrow keys move to the adjacent valid value, including the shorter final interval; Home and End
// select the exact endpoints.
// The maximum is reachable even when the range is not divisible by the step. Source values
// must already be valid; an invalid initial value rejects connection and later invalid notifications
// leave the last valid value in place and expose an error.
struct NumericSource {
    std::function<int()> read;
    std::function<std::function<void()>(std::function<void()>)> subscribe;
};
struct OptionSource {
    std::function<std::optional<OptionValue>()> read;
    std::function<std::function<void()>(std::function<void()>)> subscribe;
};
class Ui;
struct Frame;
namespace detail { class UiState; UiState& access(Ui&); const UiState& access(const Ui&); }
struct TextChange;
// A handle identifies a component without owning it. Each operation validates its lifetime.
class TextHandle {
public:
    TextHandle() = default;
    Result set_value(std::u16string value) const;
    Result undo() const;
    Result redo() const;
    Result select(unsigned anchor, unsigned caret) const;
    Result copy() const;
    Result cut() const;
    Result paste(std::u16string_view text) const;
    Result focus() const;
    Result on_blur(std::function<void()> callback) const;
    Result set_error(std::u16string message) const;
    Result reset_to_source() const;
    Result enable(bool enabled) const;
    Result read_only(bool is_read_only) const;
    Result connect_source(TextSource source) const;
    Result connect_immediate(std::function<Result(const TextChange&)> write) const;
    Result connect_confirmed(std::function<Result(const TextChange&)> write) const;
    Result confirm() const;
    Query<bool> source_conflict() const;
    Result use_latest() const;
    Query<Semantics> describe() const;
    Query<std::u16string> draft() const;
    bool operator==(const TextHandle& other) const {
        return ui_ == other.ui_ && id_ == other.id_ &&
               !lifetime_.owner_before(other.lifetime_) && !other.lifetime_.owner_before(lifetime_);
    }
private:
    friend class detail::UiState;
    friend struct Frame;
    TextHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
class ButtonHandle {
public:
    ButtonHandle() = default;
    Result invoke() const;
    Result focus() const;
    Result bind(std::function<Result()> action) const;
    Result enable(bool enabled) const;
    // Owner configuration: permanently remove this button; existing handles become invalid.
    Result remove() const;
    Query<bool> executing() const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    ButtonHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
class LabelHandle {
public:
    LabelHandle() = default;
    Result connect_source(std::function<std::u16string()> read) const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    LabelHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct BoolChange;
class CheckboxHandle {
public:
    CheckboxHandle() = default;
    Result toggle() const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(BoolSource source) const;
    Result connect_immediate(std::function<Result(const BoolChange&)> write) const;
    Result connect_confirmed(std::function<Result(const BoolChange&)> write) const;
    Result confirm() const;
    Query<bool> draft() const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    CheckboxHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct BoolChange { CheckboxHandle field; bool value, origin; };
struct OptionChange;
class RadioGroupHandle {
public:
    RadioGroupHandle() = default;
    Result select(OptionValue value) const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result enable_option(OptionValue value, bool enabled) const;
    Result connect_source(OptionSource source) const;
    Result connect_immediate(std::function<Result(const OptionChange&)> write) const;
    Result connect_confirmed(std::function<Result(const OptionChange&)> write) const;
    Result confirm() const;
    Query<std::optional<OptionValue>> draft() const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    RadioGroupHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct OptionChange { RadioGroupHandle field; std::optional<OptionValue> value, origin; };
struct NumericChange;
class SliderHandle {
public:
    SliderHandle() = default;
    Result set_value(int value) const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(NumericSource source) const;
    Result connect_immediate(std::function<Result(const NumericChange&)> write) const;
    Result connect_confirmed(std::function<Result(const NumericChange&)> write) const;
    Result confirm() const;
    Query<int> draft() const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    SliderHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct NumericChange { SliderHandle field; int value, origin; };
// A display-only percentage. Source values are never written; display is clamped to 0-100.
class ProgressHandle {
public:
    ProgressHandle() = default;
    Result connect_source(NumericSource source) const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    ProgressHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct DropdownChange;
class DropdownHandle {
public:
    DropdownHandle() = default;
    Result open() const;
    Result cancel() const;
    Result select(OptionValue value) const;
    Result set_options(std::vector<RadioOption> options) const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(OptionSource source) const;
    Result connect_immediate(std::function<Result(const DropdownChange&)> write) const;
    Result connect_confirmed(std::function<Result(const DropdownChange&)> write) const;
    Result confirm() const;
    Query<std::optional<OptionValue>> draft() const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    DropdownHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct DropdownChange { DropdownHandle field; std::optional<OptionValue> value, origin; };
class ListHandle;
class TabsHandle;
class NavigationListHandle;
using ComponentHandle = std::variant<std::monostate, TextHandle, ButtonHandle, LabelHandle, CheckboxHandle, RadioGroupHandle, SliderHandle, DropdownHandle, ListHandle, TabsHandle, ProgressHandle, NavigationListHandle>;
class TabsHandle {
public:
    TabsHandle() = default;
    Result activate(OptionValue page) const;
    Result enable_page(OptionValue page, bool enabled) const;
    // Owner configuration: attach an existing component to a page.
    Result add_member(OptionValue page, ComponentHandle member) const;
    Result focus() const;
    Query<OptionValue> active() const;
    Query<Semantics> describe() const;
    Result on_page_change(std::function<void(OptionValue, OptionValue)> callback) const;
private:
    friend class detail::UiState;
    friend struct Frame;
    TabsHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
class NavigationListHandle {
public:
    NavigationListHandle() = default;
    Result select(OptionValue page) const;
    Result enable_page(OptionValue page, bool enabled) const;
    // Owner configuration: attach an existing component to a page.
    Result add_member(OptionValue page, ComponentHandle member) const;
    Result focus() const;
    Query<OptionValue> selected() const;
    Query<Semantics> describe() const;
    Result on_page_change(std::function<void(OptionValue, OptionValue)> callback) const;
private:
    friend class detail::UiState;
    friend struct Frame;
    NavigationListHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
class ListHandle {
public:
    ListHandle() = default;
    Result select(OptionValue value) const;
    Result set_items(std::vector<RadioOption> items) const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(OptionSource source) const;
    Result connect_immediate(std::function<Result(std::optional<OptionValue>)> write) const;
    Query<Semantics> describe() const;
private:
    friend class detail::UiState;
    friend struct Frame;
    ListHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
// A single window-modal dialog, assembled from ordinary typed components. The opener and
// fallback are non-owning handles; the result callback runs once for each successful opening.
class DialogHandle {
public:
    DialogHandle() = default;
    Result open(ComponentHandle opener) const;
    Result open() const;
    Result settle(bool confirmed) const;
    Query<bool> is_open() const;
private:
    friend class detail::UiState;
    DialogHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id) : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
// Non-owning reference to explanatory content attached to a source component.
class TooltipHandle {
public:
    TooltipHandle() = default;
    Query<bool> visible() const;
private:
    friend class detail::UiState;
    TooltipHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string source)
        : ui_(ui), lifetime_(std::move(lifetime)), source_(std::move(source)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string source_;
};
// Page membership is explicit, using component handles. Page identities remain stable on reorder.
struct TabPage { OptionValue value; std::u16string label; bool enabled = true; std::vector<ComponentHandle> members; };
using NavigationPage = TabPage;
struct TextChange {
    TextHandle field;
    std::u16string value, origin;
};
struct FieldError { TextHandle field; std::u16string message; };
struct ConfirmationResult {
    Result result;
    std::vector<FieldError> errors;
};
using ConfirmField = std::variant<TextHandle, CheckboxHandle, RadioGroupHandle, SliderHandle, DropdownHandle>;
using FieldChange = std::variant<TextChange, BoolChange, OptionChange, NumericChange, DropdownChange>;
struct MixedFieldError { ConfirmField field; std::u16string message; };
struct MixedConfirmationResult { Result result; std::vector<MixedFieldError> errors; };
// Owns the component tree. Components are operated through the handles returned at creation.
class Ui {
public:
    Ui();
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    Query<TextHandle> add_text(Control control);
    Query<ButtonHandle> add_button(Control control);
    Query<LabelHandle> add_label(Control control);
    Query<CheckboxHandle> add_checkbox(Control control);
    Query<RadioGroupHandle> add_radio_group(Control control, std::vector<RadioOption> options);
    // Invalid ranges and steps return invalid_value without creating a component.
    Query<SliderHandle> add_slider(Control control, SliderRange range);
    Query<ProgressHandle> add_progress(Control control);
    Query<DropdownHandle> add_dropdown(Control control, std::vector<RadioOption> options);
    Query<TabsHandle> add_tabs(Control control, std::vector<TabPage> pages);
    Query<NavigationListHandle> add_navigation_list(Control control, std::vector<NavigationPage> pages, OptionValue initial);
    Query<ListHandle> add_list(Control control, std::vector<RadioOption> items);
    // Delay is in milliseconds; the source remains the focus and lifetime owner.
    Query<TooltipHandle> add_tooltip(std::string id, ComponentHandle source, std::u16string text, unsigned delay_ms = 500);
    Query<DialogHandle> add_dialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial_focus,
                            ButtonHandle fallback, std::function<void(bool)> result);
    Result add_shortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope = {});
    Result connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
                                std::function<ConfirmationResult(const std::vector<TextChange>&)> submit);
    Result connect_mixed_confirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<MixedConfirmationResult(const std::vector<FieldChange>&)> submit);
    void on_change(std::function<void()> callback);
    const std::vector<std::string>& diagnostics() const;
private:
    friend detail::UiState& detail::access(Ui&);
    friend const detail::UiState& detail::access(const Ui&);
    std::unique_ptr<detail::UiState> state_;
};
}
