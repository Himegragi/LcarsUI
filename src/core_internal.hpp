#pragma once
// Internal host, layout, legacy prototype and regression driver interface.
// Applications use lcars/core.hpp and typed handles instead.
#include "lcars/core.hpp"
#include <map>
#include <set>
#include <thread>

namespace lcars {
using Id = std::string;
namespace detail {
// UI-thread-owned controls addressed by logical IDs, never visual part paths.
class UiState {
public:
    using Action = std::function<Result()>;
    explicit UiState(std::vector<Control> controls);
    ~UiState();
    TextHandle add_text(Control control);
    ButtonHandle add_button(Control control);
    LabelHandle add_label(Control control);
    Result connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
                                std::function<ConfirmationResult(const std::vector<TextChange>&)> submit);
    UiState(const UiState&)=delete;
    UiState& operator=(const UiState&)=delete;
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
    // Host requests a new frame when a source notification changes the view.
    void on_invalidate(std::function<void()> callback) { invalidate_ = std::move(callback); }
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
    // E01 default: focus leaving commits leftover preedit once, as Microsoft Bopomofo does on blur.
    // Hosts finalize with the IME first, so the IME and core never both commit.
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
    friend class ::lcars::TextHandle;
    friend class ::lcars::ButtonHandle;
    friend class ::lcars::LabelHandle;
    std::map<Id, std::function<void()>, std::less<>> blur_callbacks_;
    Result configuration_access(std::string_view id) const;
    Result connect_source(const Id& id, TextSource source);
    void source_changed(const Id& id);
    std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
    std::thread::id owner_ = std::this_thread::get_id();
    std::vector<Control> controls_;
    struct Snapshot { std::u16string text; Selection selection; };
    struct Field {
        EditSession session;
        std::vector<Snapshot> past, future;  // field history; never shared between fields
        float offset = 0;
        TextSource source;
        std::u16string source_value, origin;
        bool has_source_value = false;
        bool submitting = false;
        bool confirmation_member = false;
        std::function<Result(const TextChange&)> immediate, confirmed;
        std::function<void()> disconnect;
    };
    std::map<Id, Field, std::less<>> edits_;
    std::map<Id, Action, std::less<>> actions_;
    std::set<Id, std::less<>> confirmation_buttons_;
    std::map<Id, std::u16string, std::less<>> errors_;  // the one source for field errors and error labels
    Id focus_, key_armed_, pointer_armed_, composing_, executing_;
    Clipboard* clipboard_ = nullptr;
    Result focusable(const Control& control) const;
    Result move_focus(Id next);
    std::vector<std::pair<Id, Id>> pending_focus_;  // focus notifications queued behind the current one
    bool notifying_focus_ = false;
    Result writable(std::string_view id, Field*& field);
    Result replace_selection(std::string_view id, Field& field, std::u16string_view insertion);
    Result finish_edit(std::string_view id, Field& field);
    Result travel(std::string_view id, bool forward);
    Result edit_key(KeyPress key);
    Result notify();
    std::function<void()> changed_, invalidate_;
    std::function<void(const Id&, const Id&)> focus_moved_;
    std::map<Id, std::function<std::u16string()>, std::less<>> providers_;
    bool modal_ = false, stopped_ = false, dispatching_ = false, invoking_ = false;
};
UiState& access(Ui& ui);
const UiState& access(const Ui& ui);
}
}
