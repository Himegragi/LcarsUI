#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <thread>
#include <vector>

namespace lcars {
using Id = std::string;
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
    Id id;
    Kind kind;
    std::u16string label = {};  // fallback accessible name when no name label targets this control
    std::string page = {};
    std::u16string value = {};  // text field content, or label display text
    bool enabled = true;
    bool read_only = false;
    bool checked = false;
    Id target = {};         // labels: the control this text names, describes or reports errors for
    TextRole role = TextRole::plain;
    std::size_t max_length = default_max_text_length;  // text fields only
};
// Public operations a control currently admits; restricted ones are omitted, not just flagged.
enum class Operation { focus, activate, select, copy, edit, cut, paste, undo, redo };
// Snapshot answered by Ui::describe for tests, assistive adapters and future AI operation.
struct Semantics {
    Id id;
    Kind kind = Kind::label;
    std::u16string name, value, description, error;
    bool enabled = true, read_only = false, focusable = false, focused = false, invalid = false;
    Id target;
    TextRole role = TextRole::plain;
    std::vector<Id> labels;  // labels whose target is this control, in declaration order
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
// UI-thread-owned controls addressed by logical IDs, never visual part paths.
class Ui {
public:
    using Action = std::function<Result()>;
    explicit Ui(std::vector<Control> controls);
    Ui(const Ui&)=delete;
    Ui& operator=(const Ui&)=delete;
    const std::vector<Control>& controls() const { return controls_; }
    const Control* find(std::string_view id) const;
    std::optional<Semantics> describe(std::string_view id) const;
    // Current display text: field content, or label text (bound provider wins over static value).
    std::u16string text(std::string_view id) const;
    EditSession* edit(std::string_view id);
    Result set_value(std::string_view id, std::u16string value);
    Result invoke(std::string_view id);
    Result available(std::string_view id) const;
    void bind(Id id, Action action);
    void on_change(std::function<void()> callback) { changed_ = std::move(callback); }
    // Called after each focus move with the previous and new focus (either may be empty).
    void on_focus(std::function<void(const Id& from, const Id& to)> callback) { focus_moved_ = std::move(callback); }
    // A label whose text is read from owner data on demand, so the label never holds a copy.
    void bind_text(std::string_view id, std::function<std::u16string()> provider);
    // Focus. Declaration order is the Tab order; labels and unavailable controls are skipped.
    const Id& focused() const { return focus_; }
    Result focus(std::string_view id);
    std::vector<Id> focus_order() const;
    // Input routed to the focused control. Hosts call these after translating native messages.
    // Buttons: Enter activates on press, Space on release, pointer on release over the pressed
    // control. A press belongs to the control that received it and dies with focus moves.
    Result key_down(KeyPress key);
    Result key_up(Key key);
    // The host hit-tests and passes the control under the pointer (empty when none).
    Result pointer_down(std::string_view id);
    Result pointer_up(std::string_view id);
    // Native capture loss or window deactivation: drop every pending press.
    void cancel_input();
    // C03 text field operations. Keyboard, pointer and these semantic calls share admission rules.
    // Offsets are UTF-16 and never split a surrogate pair. Each successful edit is one undo entry.
    Result text_input(std::u16string_view text);  // committed characters for the focused field
    Result select(std::string_view id, unsigned anchor, unsigned caret);
    Result undo(std::string_view id);
    Result redo(std::string_view id);
    Result paste(std::string_view id, std::u16string_view text);
    Result copy(std::string_view id);  // writes the selection to the clipboard
    Result cut(std::string_view id);
    void set_clipboard(Clipboard* clipboard) { clipboard_ = clipboard; }
    // IME composition targets the field focused when it starts. Preedit stays separate from
    // committed text; one commit is one undo entry; editing keys and commands wait while composing.
    // Focus leaving drops any leftover preedit: hosts must finalize with the IME before moving focus.
    Result composition_update(std::u16string preedit, unsigned caret);
    Result composition_commit(std::u16string_view text);
    Result composition_cancel();
    // Horizontal display offset of a field in DIP. View state owned here so owner resets clear it.
    float text_offset(std::string_view id) const;
    void set_text_offset(std::string_view id, float offset);
    // Owner (application) configuration. These are not user operations and skip admission checks.
    Result set_error(std::string_view id, std::u16string message);  // empty clears
    void enable(std::string_view id, bool enabled);  // disabling the focused control releases focus
    void set_read_only(std::string_view id, bool read_only);
    void check(std::string_view id, bool checked);
    // Owner replaces a field's content: composition, history and display offset start over.
    void replace_text(std::string_view id, std::u16string value);
    void modal(bool active) { modal_ = active; }
    bool modal() const { return modal_; }
    bool stopped() const { return stopped_; }
private:
    std::thread::id owner_ = std::this_thread::get_id();
    std::vector<Control> controls_;
    struct Snapshot { std::u16string text; Selection selection; };
    struct Field {
        EditSession session;
        std::vector<Snapshot> past, future;  // field history; never shared between fields
        float offset = 0;
    };
    std::map<Id, Field, std::less<>> edits_;
    std::map<Id, Action, std::less<>> actions_;
    std::map<Id, std::u16string, std::less<>> errors_;  // the one source for field errors and error labels
    Id focus_, key_armed_, pointer_armed_, composing_;
    Clipboard* clipboard_ = nullptr;
    Result focusable(const Control& control) const;
    void move_focus(Id next);
    Result writable(std::string_view id, Field*& field);
    Result replace_selection(std::string_view id, Field& field, std::u16string_view insertion);
    Result travel(std::string_view id, bool forward);
    Result edit_key(KeyPress key);
    Result notify();
    std::function<void()> changed_;
    std::function<void(const Id&, const Id&)> focus_moved_;
    std::map<Id, std::function<std::u16string()>, std::less<>> providers_;
    bool modal_ = false, stopped_ = false, dispatching_ = false, invoking_ = false;
};
}
