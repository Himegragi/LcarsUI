#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcars {
// label is C01 display text: never focusable, may span lines, may describe another control.
enum class Kind { text, toggle, button, navigation, label };
// What a label says about its target. Association is by logical ID, never by position or appearance.
enum class TextRole { plain, name, description, error };
enum class Status { ok, not_found, disabled, read_only, blocked_by_modal, busy, invalid_value, wrong_thread, failed, not_supported };
struct Result {
    Status status = Status::ok;
    std::u16string message;
    explicit operator bool() const { return status == Status::ok; }
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
// Semantic snapshot from a handle or a host-side adapter.
struct Semantics {
    std::string id;
    Kind kind = Kind::label;
    std::u16string name, value, description, error;
    bool enabled = true, read_only = false, focusable = false, focused = false, invalid = false, executing = false;
    std::string target;
    TextRole role = TextRole::plain;
    std::vector<std::string> labels;  // labels whose target is this control, in declaration order
    std::vector<Operation> operations;
};
// Platform-neutral keys. Hosts translate native events; Ctrl shortcuts carry their letter.
enum class Key { other, tab, enter, space, escape, left, right, home, end, backspace, del };
struct KeyPress {
    Key key = Key::other;
    bool shift = false, ctrl = false, repeat = false;
    char16_t letter = 0;  // uppercase ASCII letter for Ctrl shortcuts, e.g. u'Z'
};
struct Selection { unsigned anchor = 0, caret = 0; };
// Host-provided system clipboard. Core never touches OS services directly.
class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual bool write(std::u16string_view text) = 0;
    virtual std::optional<std::u16string> read() = 0;
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
class Ui;
namespace detail { class UiState; UiState& access(Ui&); const UiState& access(const Ui&); }
struct TextChange;
// A handle identifies a component without owning it. Each operation validates its lifetime.
class TextHandle {
public:
    TextHandle() = default;
    Result set_value(std::u16string value) const;
    Result undo() const;
    Result paste(std::u16string_view text) const;
    Result focus() const;
    Result on_blur(std::function<void()> callback) const;
    Result set_error(std::u16string message) const;
    Result reset_to_source() const;
    Result enable(bool enabled) const;
    Result read_only(bool enabled) const;
    Result connect_source(TextSource source) const;
    Result connect_immediate(std::function<Result(const TextChange&)> write) const;
    Result connect_confirmed(std::function<Result(const TextChange&)> write) const;
    Result confirm() const;
    bool source_conflict() const;
    Result use_latest() const;
    std::optional<Semantics> describe() const;
private:
    friend class detail::UiState;
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
    bool executing() const;
private:
    friend class detail::UiState;
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
private:
    friend class detail::UiState;
    LabelHandle(detail::UiState* ui, std::weak_ptr<void> lifetime, std::string id)
        : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    detail::UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
struct TextChange {
    TextHandle field;
    std::u16string value, origin;
};
struct FieldError { std::size_t index; std::u16string message; };
struct ConfirmationResult {
    Result result;
    std::vector<FieldError> errors;
};
// Owns the component tree. Components are operated through the handles returned at creation.
class Ui {
public:
    explicit Ui(std::vector<Control> controls);
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    TextHandle add_text(Control control);
    ButtonHandle add_button(Control control);
    LabelHandle add_label(Control control);
    Result connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
                                std::function<ConfirmationResult(const std::vector<TextChange>&)> submit);
    void on_change(std::function<void()> callback);
private:
    friend detail::UiState& detail::access(Ui&);
    friend const detail::UiState& detail::access(const Ui&);
    std::unique_ptr<detail::UiState> state_;
};
}
