#pragma once
// Internal host, layout, legacy prototype and regression driver interface.
// Applications use lcars/core.hpp and typed handles instead.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include <array>
#include <map>
#include <set>
#include <thread>

namespace lcars {
using Id = std::string;
namespace detail {
// UI-thread-owned controls addressed by logical IDs, never visual part paths.
class UiState {
    friend class lcars::SemanticTree;
public:
    using Action = std::function<Result()>;
    // Nest public operations and callbacks; structural changes commit after the outermost returns.
    class OperationScope {
    public:
        explicit OperationScope(UiState* state);
        ~OperationScope();
        OperationScope(const OperationScope&) = delete;
        OperationScope& operator=(const OperationScope&) = delete;
    private:
        UiState* state_;
    };
    explicit UiState(std::vector<Control> controls);
    ~UiState();
    TextHandle add_text(Control control);
    ButtonHandle add_button(Control control);
    LabelHandle add_label(Control control);
    CheckboxHandle add_checkbox(Control control);
    RadioGroupHandle add_radio_group(Control control, std::vector<RadioOption> options);
    SliderHandle add_slider(Control control, SliderRange range);
    ProgressHandle add_progress(Control control);
    ComponentHandle progress_layout_handle(const Control& control);
    DropdownHandle add_dropdown(Control control, std::vector<RadioOption> options);
    TabsHandle add_tabs(Control control, std::vector<TabPage> pages);
    ComponentHandle tabs_layout_handle(const Control& control);
    Result tabs_key_down(KeyPress key);
    Result activate_tab(const Id& id, OptionValue page);
    Result add_page_member(const Id& id, OptionValue page, ComponentHandle member);
    Result focus_error(const Id& id);
    ListHandle add_list(Control control, std::vector<RadioOption> items);
    TooltipHandle add_tooltip(ComponentHandle source, std::u16string text, unsigned delay_ms);
    void tooltip_time(std::uint64_t now);
    void tooltip_clock(std::function<std::uint64_t()> clock) { tooltip_clock_ = std::move(clock); }
    std::optional<std::uint64_t> tooltip_deadline() const;
    void tooltip_hover(std::string_view id);
    const std::u16string* tooltip_text(std::string_view id) const;
    void window_active(bool active);
    void refresh_tooltips();
    void scrolled(); // input moved a viewport outside popup content
    DialogHandle add_dialog(std::vector<ComponentHandle> members, ButtonHandle initial, ButtonHandle fallback, std::function<void(bool)> result);
    Result open_dialog(const ComponentHandle& opener);
    Result settle_dialog(bool confirmed);
    ComponentHandle list_layout_handle(const Control& control);
    Result list_key_down(KeyPress key);
    ComponentHandle dropdown_layout_handle(const Control& control);
    Result dropdown_key_down(KeyPress key);
    void close_popup();
    ComponentHandle slider_layout_handle(const Control& control);
    Result slider_key_down(KeyPress key);
    Result slider_set_value(const Id& id, int value);
    Result slider_pointer(const Id& id, int x, Rect track);
    ComponentHandle radio_layout_handle(const Control& control);
    Result radio_key_down(KeyPress key);
    ComponentHandle checkbox_layout_handle(const Control& control);
    Result reset_to_source(const Id& id);
    Result use_latest(const Id& id);
    Result connect_write(const Id& id, std::function<Result(const TextChange&)> write, bool immediate);
    Result confirm(const Id& id);
    ComponentHandle layout_handle(std::string_view id);
    std::optional<Id> layout_id(const ComponentHandle& handle) const;
    bool retired_button(const ComponentHandle& handle) const;
    // Internal adapters used by registered component types.
    ComponentHandle text_layout_handle(const Control& control);
    ComponentHandle label_layout_handle(const Control& control);
    ComponentHandle button_layout_handle(const Control& control);
    Result text_key_down(KeyPress key);
    Result command_key_down(KeyPress key);
    void command_pointer_down();
    Result connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
                                std::function<ConfirmationResult(const std::vector<TextChange>&)> submit);
    Result connect_mixed_confirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<MixedConfirmationResult(const std::vector<FieldChange>&)> submit);
    UiState(const UiState&)=delete;
    UiState& operator=(const UiState&)=delete;
    const std::vector<Control>& controls() const { return controls_; }
    // Internal inspection seam for reclamation regressions.
    std::array<std::size_t, 10> storage_counts() const {
        std::size_t members = 0;
        for (const auto& [id, tabs] : tabs_)
            for (const auto& page : tabs.pages) members += page.members.size();
        return {controls_.size(), page_members_.size(), members, actions_.size(), errors_.size(),
                tooltips_.size(), button_lifetimes_.size(), confirmation_buttons_.size(),
                confirmation_members_.size(), pending_removals_.size()};
    }
    const Control* find(std::string_view id) const;
    bool registerable(std::string_view id);
    void seed_test_controls(std::vector<Control> controls);
    std::map<Id, std::string, std::less<>> overlay_ids_;
    std::map<Id, Id, std::less<>> tooltip_ids_;
    std::vector<std::string> diagnostics_;
    std::optional<Semantics> describe(std::string_view id) const;
    void semantic_frame(const Frame& frame);
    std::string semantic_page(std::string_view id) const;
    void frame_visibility(std::map<Id, bool, std::less<>> visibility);
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
    // Native input methods must finalize before core changes the focused editing target.
    void before_focus(std::function<void()> callback) { before_focus_ = std::move(callback); }
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
    Result pointer_down(std::string_view id, std::optional<std::size_t> option = std::nullopt);
    Result pointer_up(std::string_view id, std::optional<std::size_t> option = std::nullopt);
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
    void modal(bool active) { modal_ = active; if (active) close_popup(); refresh_tooltips(); }
    bool modal() const { return modal_; }
    bool stopped() const { return stopped_; }
private:
    friend class ::lcars::TabsHandle;
    friend class ::lcars::TextHandle;
    friend class ::lcars::ButtonHandle;
    friend class ::lcars::LabelHandle;
    friend class ::lcars::CheckboxHandle;
    friend class ::lcars::RadioGroupHandle;
    friend class ::lcars::SliderHandle;
    friend class ::lcars::ProgressHandle;
    friend class ::lcars::DropdownHandle;
    friend class ::lcars::ListHandle;
    friend class ::lcars::DialogHandle;
    friend class ::lcars::TooltipHandle;
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
    struct BoolField {
        BoolSource source;
        bool value = false, source_value = false, origin = false, connected = false, submitting = false, confirmation_member = false;
        std::function<Result(const BoolChange&)> immediate, confirmed;
        std::function<void()> disconnect;
    };
    std::map<Id, BoolField, std::less<>> bools_;
    struct RadioField {
        OptionSource source;
        std::vector<RadioOption> options;
        std::optional<OptionValue> value, source_value, origin;
        bool connected = false, submitting = false, confirmation_member = false;
        std::function<Result(const OptionChange&)> immediate, confirmed;
        std::function<void()> disconnect;
    };
    std::map<Id, RadioField, std::less<>> radios_;
    struct DropdownField {
        OptionSource source;
        std::vector<RadioOption> options;
        std::optional<OptionValue> value, source_value, origin, candidate;
        bool connected = false, submitting = false, confirmation_member = false;
        std::function<Result(const DropdownChange&)> immediate, confirmed;
        std::function<void()> disconnect;
    };
    std::map<Id, DropdownField, std::less<>> dropdowns_;
    struct TabsState {
        std::vector<TabPage> pages;
        std::size_t active = 0, candidate = 0;
        bool switching = false;
        std::function<void(OptionValue, OptionValue)> changed;
    };
    std::map<Id, TabsState, std::less<>> tabs_;
    std::map<Id, std::pair<Id, std::size_t>, std::less<>> page_members_;
    struct ListField {
        std::vector<RadioOption> items;
        OptionSource source;
        std::optional<OptionValue> cursor;
        bool writing = false;
        std::function<Result(std::optional<OptionValue>)> immediate;
        std::function<void()> disconnect;
    };
    std::map<Id, ListField, std::less<>> lists_;
    struct TooltipState {
        std::u16string text;
        unsigned delay_ms = 500;
        std::uint64_t deadline = 0;
        bool eligible = false, visible = false, dismissed = false;
    };
    std::map<Id, TooltipState, std::less<>> tooltips_;
    Id tooltip_hover_;
    std::uint64_t tooltip_now_ = 0;
    std::function<std::uint64_t()> tooltip_clock_;
    bool window_active_ = false, pointer_focus_ = false;
    Id popup_source_;
    std::map<Id, bool, std::less<>> visibility_;
    bool has_frame_visibility_ = false;
    std::map<Id, Rect, std::less<>> semantic_bounds_;
    std::map<Id, std::vector<Rect>, std::less<>> semantic_options_;
    std::vector<Id> semantic_order_;
    std::map<Id, Rect, std::less<>> semantic_positions_;
    std::set<Id, std::less<>> diagnosed_names_;
    struct DialogState {
        std::vector<Id> members;
        Id initial, fallback, opener;
        std::function<void(bool)> result;
        bool configured = false, transitioning = false;
    } dialog_;
    Result select_dropdown(const Id& id, OptionValue value);
    void dropdown_source_changed(const Id& id);
    struct NumericField {
        SliderRange range;
        NumericSource source;
        bool source_invalid = false;
        std::u16string error_before_source;
        int value, source_value, origin;
        bool connected = false, submitting = false, confirmation_member = false;
        std::function<Result(const NumericChange&)> immediate, confirmed;
        std::function<void()> disconnect;
    };
    std::map<Id, NumericField, std::less<>> numerics_;
    struct ProgressField { NumericSource source; std::function<void()> disconnect; };
    std::map<Id, ProgressField, std::less<>> progresses_;
    void numeric_source_changed(const Id& id);
    Result select_option(const Id& id, OptionValue value);
    void option_source_changed(const Id& id);
    Result toggle_checkbox(const Id& id);
    void bool_source_changed(const Id& id);
    std::map<Id, Action, std::less<>> actions_;
    std::set<Id, std::less<>> confirmation_buttons_;
    std::map<Id, std::shared_ptr<void>, std::less<>> button_lifetimes_;
    std::map<Id, std::vector<Id>, std::less<>> confirmation_members_;
    std::vector<Id> pending_removals_;
    Id retiring_;
    bool removal_pending(std::string_view id) const;
    unsigned operation_depth_ = 0;
    bool committing_removals_ = false;
    void commit_removals();
    std::map<Id, std::u16string, std::less<>> errors_;  // the one source for field errors and error labels
    Id focus_, key_armed_, pointer_armed_, composing_, executing_;
    std::optional<std::size_t> pointer_option_;
    std::optional<OptionValue> pointer_option_value_;
    Clipboard* clipboard_ = nullptr;
    Result focusable(const Control& control) const;
    Result move_focus(Id next);
    std::vector<std::pair<Id, Id>> pending_focus_;  // focus notifications queued behind the current one
    bool notifying_focus_ = false;
    Result writable(std::string_view id, Field*& field);
    Result replace_selection(std::string_view id, Field& field, std::u16string_view insertion);
    Result finish_edit(std::string_view id, Field& field);
    Result submit_field(std::string_view id, Field& field,
                        const std::function<Result(const TextChange&)>& write, bool command);
    Result travel(std::string_view id, bool forward);
    Result edit_key(KeyPress key);
    Result notify();
    std::function<void()> changed_, invalidate_;
    std::function<void(const Id&, const Id&)> focus_moved_;
    std::function<void()> before_focus_;
    std::map<Id, std::function<std::u16string()>, std::less<>> providers_;
    bool modal_ = false, stopped_ = false, dispatching_ = false, invoking_ = false;
};
const Placement* find_placement(const Frame& frame, std::string_view id);
unsigned placement_text_offset(const Frame& frame, std::string_view id, int x);
UiState& access(Ui& ui);
const UiState& access(const Ui& ui);
}
}
