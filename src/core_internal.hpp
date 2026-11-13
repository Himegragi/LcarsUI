#pragma once
// Internal host, layout, legacy prototype and regression driver interface.
// Applications use lcars/core.hpp and typed handles instead.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "animation.hpp"
#include "word_break.hpp"
#include <array>
#include <map>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>

namespace lcars {
using Id = std::string;
namespace detail {
struct TextHit { unsigned offset = 0; bool upstream = false; };
// Whether a text control's initial value, visible lines and rich option are admissible (the rule of Ui::add_text and of a field supplied as data).
bool valid_text_control(const Control& control);
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
    // The not_found texts of a handle whose lifetime has ended. Each operation keeps the text it has always given (#94
    // decision 7): component handles, context menus and most others say component_gone, dialogs and tooltips ui_gone,
    // and ListHandle::focus(OptionValue) answers without text (gone_without_text).
    static constexpr std::u16string_view component_gone = u"Component no longer exists";
    static constexpr std::u16string_view ui_gone = u"UI no longer exists";
    static constexpr std::u16string_view gone_without_text = u"";
    // The one liveness preamble of every typed handle operation (#94). The body runs inside this Ui's operation scope; a
    // handle whose lifetime has ended (its Ui is gone, its button removed, a tooltip's source removed) runs nothing and
    // answers not_found with `gone`. R is the operation's own return type (Result or a Query).
    template <class Handle, class Body, class R = std::invoke_result_t<Body&>>
    static R operate(const Handle& handle, Body&& body, std::u16string_view gone = component_gone) {
        const bool alive = !handle.lifetime_.expired();
        OperationScope operation(alive ? handle.ui_ : nullptr);
        if (!alive) {
            R answer;
            answer.status = Status::not_found;
            answer.message = std::u16string(gone);
            return answer;
        }
        return body();
    }
    explicit UiState(std::vector<Control> controls);
    ~UiState();
    TextHandle add_text(Control control);
    ButtonHandle add_button(Control control);
    LabelHandle add_label(Control control);
    CheckboxHandle add_checkbox(Control control);
    RadioGroupHandle add_radio_group(Control control, std::vector<RadioOption> options);
    SliderHandle add_slider(Control control, SliderRange range);
    ProgressHandle add_progress(Control control);
    DropdownHandle add_dropdown(Control control, std::vector<RadioOption> options);
    TabsHandle add_tabs(Control control, std::vector<TabPage> pages);
    NavigationListHandle add_navigation_list(Control control, std::vector<NavigationPage> pages, OptionValue initial);
    Result activate_tab(const Id& id, OptionValue page);
    Result add_page_member(const Id& id, OptionValue page, ComponentHandle member);
    Result enable_page(const Id& id, const OptionValue& page, bool enabled);
    Result on_page_change(const Id& id, std::function<void(OptionValue, OptionValue)> callback);
    Query<OptionValue> active_page(const Id& id) const;
    Query<Semantics> handle_describe(std::string_view id) const;
    Result focus_error(const Id& id);
    ListHandle add_list(Control control, std::vector<RadioOption> items);
    TooltipHandle add_tooltip(ComponentHandle source, std::u16string text, unsigned delay_ms);
    void tooltip_time(std::uint64_t now);
    void tooltip_clock(std::function<std::uint64_t()> clock) { tooltip_clock_ = std::move(clock); }
    std::optional<std::uint64_t> tooltip_deadline() const;
    void tooltip_hover(std::string_view id);
    const std::u16string* tooltip_text(std::string_view id) const;
    void window_active(bool active);
    // Pointer hover. The host (or a test) reports the real mouse in window coordinates; touch and pen
    // messages (touch = true) are ignored. One call moves both the hover target and the tooltip dwell.
    void pointer_over(const Frame& frame, int x, int y, bool touch = false);
    void pointer_left();  // the pointer left the client area
    // A drag that must keep the hover where it started (text selection, scrollbar thumb). An id first
    // makes that component the target; an empty id freezes the current one. A slider drag holds it by itself.
    void hover_capture(std::string_view id);
    void hover_release();
    // Re-evaluates the real pointer at its last position; arrange calls it after every layout.
    void hover_refresh(const Frame& frame);
    // The admitted target (see Ui::hover) and whether the overlay is drawn for it.
    std::optional<HoverTarget> hover_target() const;
    bool hover_drawn(std::string_view id, const OptionValue* item) const;
    Result simulate_hover(std::string_view id, const std::optional<OptionValue>& item);
    Result clear_simulated_hover();
    Query<std::optional<HoverTarget>> hover_query() const;
    void refresh_tooltips();
    void scrolled(); // input moved a viewport outside popup content
    ScrollState& dropdown_scroll(const Id& id) { return dropdowns_.at(id).scroll; }
    bool dropdown_reveal(const Id& id) { return std::exchange(dropdowns_.at(id).reveal, false); }
    void popup_scrolled() { if (invalidate_) invalidate_(); }
    DialogHandle add_dialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial, ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name);
    Result open_dialog(const Id& id, const ComponentHandle* opener);
    Result settle_dialog(const Id& id, bool confirmed);
    void close_popup();
    Result slider_set_value(const Id& id, int value);
    // A pointer drag converts the mouse x to a value against the track captured at press, so a layout
    // that moves under the pointer cannot change the value. The thumb is drawn at the pointer, not the value.
    Result slider_pointer_down(const Id& id, int x, Rect track, float scale = 1.f);
    Result slider_pointer(const Id& id, int x);
    void slider_track_refresh(const Frame& frame);  // end of every arrange: where the held thumb is drawn now
    std::optional<int> slider_drag_x(std::string_view id) const;
    // #75 shared animation clock. The host (or a test) supplies the time source in milliseconds; with
    // none installed every change is instant. Motion plays only when the source exists, presentation
    // motion is automatic, the system animation preference is on and the window is visible.
    void animation_time(std::function<std::uint64_t()> source) { animations_.time_source(std::move(source)); }
    void set_system_animations(bool enabled);  // Windows "Show animations in Windows"; false ends running ones
    void set_window_hidden(bool hidden);       // minimized / fully covered; true ends running ones
    bool motion_enabled() const;
    void finish_animations();                  // every running animation jumps to its end
    // The one query behind Ui::animating and SemanticTree::animating (same admission and statuses).
    Query<bool> animating() const;
    bool animation_running() const;            // host: schedule the next frame only while true
    // Thumb position as a 0..1 share of the track while a transition runs; nothing otherwise.
    std::optional<double> slider_animated_ratio(std::string_view id) const;
    // Pointer x the thumb is drawn at while the pointer holds it (a drag, or any press with motion off).
    std::optional<int> slider_held_x(std::string_view id) const;
    Result reset_to_source(const Id& id);
    Result use_latest(const Id& id);
    Result connect_write(const Id& id, std::function<Result(const TextChange&)> write, bool immediate);
    Result connect_rich_write(const Id& id, std::function<Result(const RichTextChange&)> write, bool immediate);
    bool is_rich(std::string_view id) const;
    Result confirm(const Id& id);
    ComponentHandle layout_handle(std::string_view id);
    std::optional<Id> layout_id(const ComponentHandle& handle) const;
    bool retired_button(const ComponentHandle& handle) const;
    // What registered component types (src/component_type.cpp) use to act on the state around them.
    // The typed handle of a component of the given handle type; a type's own handle() returns it.
    template <class Handle> ComponentHandle typed_handle(const Control& control) {
        OperationScope operation(this);
        if constexpr (std::is_same_v<Handle, ButtonHandle>) return Handle(this, button_lifetimes_.at(control.id), control.id);
        else return Handle(this, lifetime_, control.id);
    }
    // A pointer press arms its component with the row under the press (and that row's value); the release over the
    // same component then reaches pointer_up of its type. A key press arms the component for the release of Space.
    void arm_pointer(const Id& id, std::optional<std::size_t> option = std::nullopt, std::optional<OptionValue> value = std::nullopt) {
        pointer_armed_ = id; pointer_option_ = option; pointer_option_value_ = std::move(value);
    }
    void arm_key(const Id& id) { key_armed_ = id; }
    bool popup_open(std::string_view id) const { return popup_source_ == id; }
    void invalidate() { if (invalidate_) invalidate_(); }
    bool renderer_failed() const { return renderer_failed_; }
    bool has_action(std::string_view id) const { return actions_.contains(id); }
    bool text_composing(std::string_view id) const { auto it = edits_.find(id); return it != edits_.end() && it->second.session.composing; }
    // The per-type state and the engines a component type reaches into (the maps and helpers declared private below).
    // Only declared here: the definition lives in component_type.cpp, so no other file can use it.
    struct Internals;
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
    bool confirmation_group_contains(std::string_view button, std::string_view field) const {
        auto group = confirmation_members_.find(button);
        return group != confirmation_members_.end() &&
            std::find(group->second.begin(), group->second.end(), field) != group->second.end();
    }
    void frame_visibility(std::map<Id, bool, std::less<>> visibility);
    // Current display text: field content, or label text (bound provider wins over static value).
    std::u16string text(std::string_view id) const;
    EditSession* edit(std::string_view id);
    Result set_value(std::string_view id, std::u16string value);
    Result invoke(std::string_view id);
    Result available(std::string_view id, bool ignore_renderer_failure = false) const;
    Result available_option(std::string_view id, const OptionValue& value) const;

    void bind(Id id, Action action);
    void on_change(std::function<void()> callback) { changed_ = std::move(callback); }
    // Host requests a new frame when a source notification changes the view.
    Result set_presentation(const PresentationSettings& settings);
    const PresentationSettings& presentation() const { return presentation_; }
    // Focus indicator display state. Only real input moves it: key_down and the host's pointer press.
    // Programmatic focus, semantic operations and UIA never change it.
    // `never` is checked first and wins over everything, the system's "always show keyboard cues" setting included.
    bool focus_indicator_visible() const { return presentation_.focus_indicator != FocusIndicatorMode::never && keyboard_focus_cues(); }
    // Whether the user is working with the keyboard (or the application forces the display on). Drawing is decided by
    // focus_indicator_visible(); this one also drives what is not drawing, such as the tooltip that follows keyboard focus.
    bool keyboard_focus_cues() const { return keyboard_cues_ || system_cues_ || presentation_.focus_indicator == FocusIndicatorMode::always; }
    // The per-component answer the paint code uses: focused, and either shown or an editable text field.
    bool draws_focus_indicator(const Semantics& semantics) const;
    // A pointer press anywhere in the client area, including empty space and inert components.
    void pointer_pressed();
    // The host mirrors Windows SPI_GETKEYBOARDCUES here (at startup and on WM_SETTINGCHANGE).
    void set_system_keyboard_cues(bool enabled);
    void on_presentation(std::function<void()> callback) { presentation_changed_ = std::move(callback); }
    Result set_palette_overrides(std::vector<PaletteOverride> overrides);
    const std::vector<PaletteOverride>& palette_overrides() const { return palette_overrides_; }
    // The single color entry for painting: built-in palette of this appearance for the current
    // theme variant (or an explicit one), with the override list applied.
    Palette effective_palette(const Appearance& appearance, const ThemeVariant& variant) const;
    Palette effective_palette(const Appearance& appearance) const { return effective_palette(appearance, presentation_.variant); }
    void bind_appearance(const Appearance* appearance) { bound_appearance_ = appearance; }
    const Appearance* bound_appearance() const { return bound_appearance_; }
    void on_invalidate(std::function<void()> callback) { invalidate_ = std::move(callback); }
    void on_relayout(std::function<void()> callback) { relayout_ = std::move(callback); }
    void set_scrollable_text(std::string id, ScrollState& state, int maximum, int line_height);
    // Sideways scrolling of a vertical-writing label. The state belongs to the label; the offset counts pixels
    // from the start of the text (right edge). A label that does not scroll is not focusable.
    ScrollState& vertical_scroll(const Id& id) { return vertical_scrolls_[id]; }
    void set_vertical_scroll(const Id& id, int maximum, int column_width);
    void clear_vertical_scroll(const Id& id);
    void on_semantic_pending(std::function<void()> callback) {
        semantic_schedule_ = std::move(callback);
        if (semantic_schedule_ && !semantic_pending_.empty()) semantic_schedule_();
    }
    void on_reveal(std::function<void(const Id&)> callback) { reveal_requested_ = std::move(callback); }
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
    Result add_shortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope);
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
    Result paste_clipboard(std::string_view id);  // Ctrl+V: styles come along when the clipboard holds a matching payload
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
    unsigned text_anchor(std::string_view id, int text_size) const;
    void set_text_anchor(std::string_view id, int text_size, unsigned anchor);
    // Multi-line view state. arrange hands over the lines it measured and gets the settled first
    // visible line back (view.first): layout is the only place that knows where lines break.
    void settle_lines(std::string_view id, FieldView& view, int text_size, int width);
    const FieldView* field_view(std::string_view id) const;  // lines as last arranged; null for other kinds
    ScrollState& line_scroll(const Id& id) { return edits_.at(id).multi.scroll; }
    bool scroll_lines_to(std::string_view id, unsigned first);  // clamps; true when the position changed
    // A pointer click or drag: like select, but may place the caret at the end of a wrapped line.
    Result select_pointer(std::string_view id, unsigned anchor, unsigned caret, bool upstream);
    // Multi-line configuration and view position, shared by the typed handle and the semantic verb.
    Result set_visible_lines(std::string_view id, unsigned lines);
    Result set_scroll_line(std::string_view id, unsigned first);
    Query<unsigned> scroll_line(std::string_view id) const;
    // Text flow (#80), labels and text fields; shared by the typed handles and the semantic verbs.
    Result set_text_align(std::string_view id, TextAlign align);
    Result set_justify_method(std::string_view id, JustifyMethod method);
    Result set_line_spacing(std::string_view id, double multiple);
    Result set_paragraph_spacing(std::string_view id, int dip);
    // Words, paragraphs and multi-click (#83). Queries and selections share select()'s admission rules.
    void set_word_breaker(std::shared_ptr<const WordBreaker> breaker) { word_breaker_ = std::move(breaker); }  // null: the system ICU
    Result set_word_segmentation(std::string_view id, WordSegmentation mode);
    Query<WordSegmentation> word_segmentation(std::string_view id) const;
    Query<WordSegmentation> word_segmentation_in_use(std::string_view id) const;
    Query<TextSpan> word_at(std::string_view id, unsigned offset) const;
    Query<TextSpan> paragraph_at(std::string_view id, unsigned offset) const;
    Query<unsigned> word_stop(std::string_view id, unsigned offset, bool forward) const;
    // The UIA word and paragraph unit boundaries of a field's committed text (empty for anything that is not a text field).
    std::vector<unsigned> word_unit_boundaries(std::string_view id) const;
    std::vector<unsigned> paragraph_boundaries(std::string_view id) const;
    Result select_word(std::string_view id, unsigned offset);
    Result select_paragraph(std::string_view id, unsigned offset);
    Result double_click(std::string_view id, unsigned offset);
    Result triple_click(std::string_view id, unsigned offset);
    // The host reads the system double-click settings and a clock and lets core count the clicks, so tests
    // can inject both. Without a clock every press counts as a single click.
    struct ClickSettings { std::uint64_t interval_ms = 500; int width = 4, height = 4; };
    void set_click_settings(ClickSettings settings) { click_settings_ = settings; }
    void set_click_clock(std::function<std::uint64_t()> clock) { click_clock_ = std::move(clock); }
    // A primary-button press on a text field: `caret` is the nearest caret position (single click), `cell` the
    // character under the pointer (double and triple click), x/y the press position in the click's coordinates.
    Result text_pointer_down(std::string_view id, TextHit caret, unsigned cell, int x, int y, bool extend);
    // A drag with the button held: extends by the unit the press started with (character, word or paragraph).
    Result text_pointer_drag(std::string_view id, TextHit caret, unsigned cell);
    void end_text_drag();
    // Owner (application) configuration. These are not user operations and skip admission checks.
    Result set_error(std::string_view id, std::u16string message);  // empty clears
    void enable(std::string_view id, bool enabled);  // disabling the focused control releases focus
    void set_read_only(std::string_view id, bool read_only);
    void check(std::string_view id, bool checked);
    // Label owner configuration (vertical writing). A change asks the host for a new frame.
    static Result writing_support(const Control& control);
    void set_label_writing(std::string_view id, std::optional<WritingMode> mode, std::optional<int> digits);
    // Owner replaces a field's content: composition, history and display offset start over.
    void replace_text(std::string_view id, std::u16string value);
    void modal(bool active) { modal_ = active; if (active) { close_popup(); close_menu(); } hover_clear(); refresh_tooltips(); }
    void replace_rich(std::string_view id, RichText value);  // owner refresh of a rich field: text and spans
    // Rich text (Control::rich fields; others answer not_supported). Shared by the typed handle, the semantic
    // layer, Ctrl+B/I/U and any menu: they all run these same functions.
    Result set_rich_value(std::string_view id, RichText value);
    Query<RichText> rich_value(std::string_view id) const;
    Result set_style(std::string_view id, unsigned begin, unsigned end, const StyleChange& change);
    Result toggle_style(std::string_view id, StyleAttribute attribute);  // selection, or the typing style at a caret
    Result apply_style(std::string_view id, const StyleChange& change);  // selection, or the typing style at a caret
    Query<TextStyle> typing_style(std::string_view id) const;
    Result set_typing_style(std::string_view id, TextStyle style);
    Result set_colors(std::string_view id, std::vector<NamedColor> colors);
    Query<std::vector<NamedColor>> colors(std::string_view id) const;
    // The committed spans of a rich field (null for others) and the spans of the visible text, which an
    // IME composition is part of (the preedit takes the typing style), for layout and painting.
    const std::vector<StyleSpan>* style_spans(std::string_view id) const;
    std::vector<StyleSpan> visible_style_spans(std::string_view id) const;
    // The color of a name for the current theme variant, nullopt for an empty or unknown name.
    std::optional<Color> named_color(std::string_view id, std::string_view name) const;
    bool modal() const { return modal_; }
    bool stopped() const { return stopped_; }
    Result creation_access() const;
    void renderer_failed(bool failed);
    // ---- Context menu (src/context_menu.cpp). One menu is open per window; it never changes focus_.
    struct MenuEntry { std::string id; std::u16string label, hint; bool enabled = true, separator = false; char16_t access_key = 0; };
    struct MenuView {
        Id target;
        std::optional<OptionValue> item;      // list row the menu was opened for
        std::optional<OptionValue> anchor_item;  // row a keyboard-opened menu hangs from
        std::optional<MenuPosition> at;       // none: the keyboard position of the target
        std::vector<MenuEntry> entries;
        std::string highlighted;
    };
    bool has_menu(const Control& control) const;
    bool menu_open() const { return menu_.has_value(); }
    Query<bool> menu_open_query() const;
    Query<MenuInfo> menu_info() const;
    Query<ContextMenuHandle> attach_menu(const Id& target, std::vector<MenuItem> items, MenuMode mode);
    std::optional<MenuView> menu_view() const;
    ScrollState* menu_scroll() { return menu_ ? &menu_->scroll : nullptr; }
    bool menu_reveal() { return menu_ && std::exchange(menu_->reveal, false); }
    // at: where to open (window pixels); none for the keyboard position. text_offset: the pointer's caret offset.
    Result open_menu(std::string_view id, std::optional<MenuPosition> at, std::optional<OptionValue> item,
                     std::optional<unsigned> text_offset = std::nullopt);
    // Right-button release over a component: option is the pointer's row (lists).
    Result menu_pointer_open(std::string_view id, std::optional<std::size_t> option, std::optional<unsigned> text_offset, MenuPosition at);
    Result close_menu();
    Result menu_execute(std::string_view item_id);
    Result menu_highlight(std::string_view item_id);
    Result menu_pointer_down(std::string_view item_id);
    Result menu_pointer_up(std::string_view item_id);
    Result menu_key_down(KeyPress key);
    Result menu_text(std::u16string_view text);
    void menu_frame(const Frame& frame);
    std::optional<Query<SemanticNode>> menu_node(const NodeRef& ref);
    std::optional<std::vector<NodeRef>> menu_children(const NodeRef& parent) const;
private:
    friend class ::lcars::ContextMenuHandle;
    friend class ::lcars::TabsHandle;
    friend class ::lcars::NavigationListHandle;
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
    Result configuration_access(std::string_view id, bool read_only = false) const;
    Result connect_source(const Id& id, TextSource source, std::function<RichText()> rich_read = {});
    void source_changed(const Id& id);
    std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
    PresentationSettings presentation_;
    std::function<void()> presentation_changed_;
    std::vector<PaletteOverride> palette_overrides_;
    const Appearance* bound_appearance_ = nullptr;
    std::thread::id owner_ = std::this_thread::get_id();
    std::vector<Control> controls_;
    std::map<Id, std::uint64_t, std::less<>> semantic_instances_;
    std::uint64_t next_semantic_instance_ = 0;
    struct Snapshot { std::u16string text; Selection selection; std::vector<StyleSpan> spans; };
    // The data connection of every field that writes back to the application (#94): text field, checkbox, radio group,
    // dropdown and slider. One copy of the state and of the rules on it; the public connect_*, confirm, use_latest and
    // draft keep their names and parameters. Change is the field's public change type (TextChange, BoolChange, ...).
    template <class ChangeType> struct FieldConnection {
        using Change = ChangeType;
        bool connected = false;            // the source has delivered a valid value
        bool submitting = false;           // a write of this field is running
        bool confirmation_member = false;  // a confirmation group's button writes it
        std::function<Result(const Change&)> immediate, confirmed;
        std::function<void()> disconnect;
        // How the value gets written: an immediate or a confirmed write, or a confirmation group.
        bool has_write_timing() const { return confirmation_member || immediate || confirmed; }
        // What every change of the value needs (the "can be written" rule): a source and a write timing.
        bool writable() const { return connected && has_write_timing(); }
        // Whether a write of this timing may be connected: a source, no group, and not the other timing.
        bool accepts_write(bool immediate_timing) const {
            return connected && !confirmation_member && !(immediate_timing ? bool(confirmed) : bool(immediate));
        }
        // Whether the field may join a confirmation group: a source and no write timing yet.
        bool joinable() const { return connected && !has_write_timing(); }
    };
    struct Field : FieldConnection<TextChange> {
        EditSession session;
        std::vector<Snapshot> past, future;  // field history; never shared between fields
        float offset = 0;
        int offset_text_size = 0;
        unsigned offset_anchor = 0;
        struct MultiView {
            FieldView view;               // lines as last arranged (geometry rectangles are unused here)
            std::u16string text;          // the visible text those lines were laid out from
            std::vector<StyleSpan> styles; // and its styled runs (rich fields): a style change can re-wrap too
            int size = 0, width = 0;      // text size and wrap width they were laid out at
            unsigned revision = 0;        // layout_revision_ they were laid out at: a property change makes them stale
            unsigned first = 0;           // first visible line
            unsigned anchor = 0;          // visible-text offset where that line starts: survives a re-wrap
            unsigned caret = 0;           // caret seen by the last arrange: a different one reveals itself
            bool upstream = false, placed = false;
            std::optional<unsigned> pending_first;  // set before the first arrange
            float want_x = 0;             // the column an Up/Down run keeps, in pixels from the line start
            unsigned want_from = ~0u;     // the caret that run last left it at: any other caret starts a new run
            ScrollState scroll;           // pixel offset = first * line height, for the field's scrollbar region
        } multi;
        WordSegmentation segmentation = WordSegmentation::dictionary;
        // A double or triple click arms a drag that grows by that unit while the selection stays as the click left it.
        struct UnitDrag { enum class Unit { character, word, paragraph } unit = Unit::character; TextSpan origin; Selection last; } drag;
        TextSource source;
        std::u16string source_value, origin;
        bool rich = false;                     // Control::rich
        std::vector<StyleSpan> spans, source_spans, origin_spans;  // committed styles, last source value, last written value
        std::function<RichText()> rich_read;   // the rich source (the TextSource subscribe half is shared)
        std::optional<TextStyle> typing;       // the temporary typing style, valid while the selection has not moved
        std::uint64_t typing_moves = 0;
        std::vector<NamedColor> colors;
    };
    std::map<Id, Field, std::less<>> edits_;
    struct BoolField : FieldConnection<BoolChange> {
        BoolSource source;
        bool value = false, source_value = false, origin = false;
    };
    std::map<Id, BoolField, std::less<>> bools_;
    struct RadioField : FieldConnection<OptionChange> {
        OptionSource source;
        std::vector<RadioOption> options;
        std::optional<OptionValue> value, source_value, origin;
    };
    std::map<Id, RadioField, std::less<>> radios_;
    struct DropdownField : FieldConnection<DropdownChange> {
        OptionSource source;
        std::vector<RadioOption> options;
        std::optional<OptionValue> value, source_value, origin, candidate;
        ScrollState scroll; bool reveal = true;
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
    struct MenuAttachment { MenuMode mode = MenuMode::append; std::vector<MenuItem> items; };
    std::map<Id, MenuAttachment, std::less<>> menu_attachments_;
    // The opening every ContextMenuHandle operation shares: the configuration check (read_only for a query), then the
    // attachment on this target, not_found "Menu no longer exists" without one. On ok, `attachment` points at it.
    Result menu_attachment(std::string_view target, MenuAttachment*& attachment, bool read_only = false);
    struct OpenMenu {
        Id target;
        std::optional<OptionValue> item;
        std::optional<MenuPosition> at;
        std::string highlighted, pressed;
        ScrollState scroll;
        bool reveal = false, clipboard_text = false;
    };
    std::optional<OpenMenu> menu_;
    std::map<std::string, Rect, std::less<>> semantic_menu_rows_;
    std::vector<MenuEntry> menu_entries(const Id& target) const;
    Result menu_run_builtin(const Id& target, std::string_view command);
    Result validate_menu_items(const std::vector<MenuItem>& items) const;
    void menu_items_changed(const Id& target);
    Result delete_selection(std::string_view id);  // Delete and the Delete menu item share it
    std::map<Id, TooltipState, std::less<>> tooltips_;
    Id tooltip_hover_;
    // Pointer hover. hover_ is the raw target; hover_target() filters it against the live state.
    std::optional<HoverTarget> hover_;
    bool hover_simulated_ = false, hover_capture_ = false;
    std::optional<std::pair<int, int>> hover_position_;  // last real pointer position
    bool hover_locked() const;
    bool hover_admitted(const HoverTarget& target) const;
    void hover_set(std::optional<HoverTarget> target);
    void hover_from_frame(const Frame& frame, int x, int y);
    void hover_clear();
    std::uint64_t tooltip_now_ = 0;
    std::optional<std::uint64_t> tooltip_paused_at_;
    std::function<std::uint64_t()> tooltip_clock_;
    bool window_active_ = false, keyboard_cues_ = false, system_cues_ = false;
    void focus_cues_changed();
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
        Id initial, fallback, opener, name_label;
        std::function<void(bool)> result;
        bool configured = false, transitioning = false;
    };
    std::map<Id, DialogState, std::less<>> dialogs_;
    Id active_dialog_;
    bool dialog_transitioning_ = false;
    bool dialog_member(std::string_view id) const;
    Result select_dropdown(const Id& id, OptionValue value);
    void dropdown_source_changed(const Id& id);
    struct NumericField : FieldConnection<NumericChange> {
        SliderRange range;
        NumericSource source;
        bool source_invalid = false;
        std::u16string error_before_source;
        int value, source_value, origin;
    };
    std::map<Id, NumericField, std::less<>> numerics_;
    // The field state behind a confirmable handle, by handle kind (the one place that pairs a kind with its map).
    template <class Handle> auto& fields_of() {
        if constexpr (std::is_same_v<Handle, TextHandle>) return edits_;
        else if constexpr (std::is_same_v<Handle, CheckboxHandle>) return bools_;
        else if constexpr (std::is_same_v<Handle, RadioGroupHandle>) return radios_;
        else if constexpr (std::is_same_v<Handle, DropdownHandle>) return dropdowns_;
        else { static_assert(std::is_same_v<Handle, SliderHandle>); return numerics_; }
    }
    // Runs f on the connection of the field with this id, whichever kind of field it is.
    template <class F> void with_connection(std::string_view id, F&& f) {
        if (auto it = edits_.find(id); it != edits_.end()) f(it->second);
        if (auto it = bools_.find(id); it != bools_.end()) f(it->second);
        if (auto it = radios_.find(id); it != radios_.end()) f(it->second);
        if (auto it = dropdowns_.find(id); it != dropdowns_.end()) f(it->second);
        if (auto it = numerics_.find(id); it != numerics_.end()) f(it->second);
    }
    // The write paths the checkbox, radio group, dropdown and slider share (a text field adds history and composition).
    template <class Connection> static Result connect_field_write(Connection& field, std::function<Result(const typename Connection::Change&)> write, bool immediate);
    template <class Handle> Result confirm_field(const Handle& handle);
    template <class FieldType> Result write_immediately(const Id& id, FieldType& field, const typename FieldType::Change& change);
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
    std::map<std::size_t, std::function<void(const SemanticEvent&)>> semantic_subscribers_;
    std::size_t next_semantic_subscriber_ = 0;
    std::vector<SemanticEvent> semantic_pending_;
    std::vector<SemanticNode> semantic_before_;
    bool semantic_dispatching_ = false;
    bool semantic_inspecting_ = false;
    std::function<void()> semantic_schedule_;
    std::function<void(const Id&)> reveal_requested_;
    std::vector<SemanticNode> semantic_snapshot();
    void semantic_diff();
    bool committing_removals_ = false;
    void commit_removals();
    std::map<Id, std::u16string, std::less<>> errors_;  // the one source for field errors and error labels
    struct ShortcutBinding { Shortcut chord; Id target, scope; };
    std::vector<ShortcutBinding> shortcuts_;
    Id focus_, key_armed_, pointer_armed_, composing_, executing_;
    // origin/width: the track captured at press (the value basis). seen_*: the track the latest frame drew the thumb on.
    struct SliderDrag { Id id; double origin = 0, width = 1; int x = 0, press_x = 0; bool held = false; double seen_origin = 0, seen_width = 1; int threshold = 4; };
    std::optional<SliderDrag> slider_drag_;
    AnimationClock animations_;
    bool system_animations_ = true, window_hidden_ = false;
    std::map<Id, int, std::less<>> slider_targets_;  // value each running thumb transition is heading to
    static std::string slider_animation_key(std::string_view id) { return "slider:" + std::string(id); }
    bool slider_transition_live(std::string_view id) const;
    double slider_display_ratio(std::string_view id) const;
    void slider_started_moving(const Id& id, double from, int value_before);
    void end_slider_drag(bool slide);  // slide: the release of a drag eases to the tick; an interruption lands at once
    void slider_land(std::string_view id);  // drop a running thumb transition: the thumb is at its value now
    std::optional<std::size_t> pointer_option_;
    std::optional<OptionValue> pointer_option_value_;
    Clipboard* clipboard_ = nullptr;
    std::shared_ptr<const WordBreaker> word_breaker_;
    const WordBreaker& dictionary() const { return word_breaker_ ? *word_breaker_ : system_icu_breaker(); }
    Words words_of(const Field& field) const { return segment_words(field.session.text, field.segmentation, dictionary()); }
    ClickSettings click_settings_;
    std::function<std::uint64_t()> click_clock_;
    struct ClickRun { Id id; int x = 0, y = 0; std::uint64_t time = 0; unsigned count = 0; } click_run_;
    unsigned count_click(const Id& id, int x, int y);
    Result select_unit(std::string_view id, unsigned offset, Field::UnitDrag::Unit unit, bool arm);
    Result focusable(const Control& control) const;
    Result move_focus(Id next);
    std::vector<std::pair<Id, Id>> pending_focus_;  // focus notifications queued behind the current one
    bool notifying_focus_ = false;
    Result writable(std::string_view id, Field*& field);
    Result rich_writable(std::string_view id, Field*& field);
    Result replace_selection(std::string_view id, Field& field, std::u16string_view insertion);
    // Replaces [first, last) of the text; inserted_spans (relative to the insertion) override the typing style.
    Result replace_range(std::string_view id, Field& field, unsigned first, unsigned last, std::u16string_view insertion,
                         const std::vector<StyleSpan>* inserted_spans);
    Result commit_style(std::string_view id, Field& field, std::vector<StyleSpan> spans);
    TextStyle insertion_style(const Field& field, unsigned first, unsigned last) const;
    bool source_matches(const Field& field) const { return field.session.text == field.source_value && field.spans == field.source_spans; }
    Result finish_edit(std::string_view id, Field& field);
    Result submit_field(std::string_view id, Field& field,
                        const std::function<Result(const TextChange&)>& write, bool command);
    Result travel(std::string_view id, bool forward);
    Result edit_key(KeyPress key);  // the keys a text field takes
    // Multi-line cursor movement over the lines the last arrange settled. A host that can lay out
    // again on demand registers on_relayout so keys typed faster than frames never see stale lines.
    FieldView vertical_view(const Id& id);
    // Bumped by every property that changes how text lines up (alignment, justification, spacing, the
    // justify-as-left setting): the lines cursor keys use are stale until an arrange sees the new value.
    unsigned layout_revision_ = 0;
    Result move_lines(const Id& id, int direction, unsigned count, bool shift);
    Result move_line_edge(const Id& id, bool end, bool shift);
    Result notify();
    std::function<void()> changed_, invalidate_, relayout_;
    struct TextScroll { ScrollState* state; int maximum, line_height; bool horizontal = false; };
    std::map<std::string, TextScroll, std::less<>> text_scrolls_;
    std::map<Id, ScrollState, std::less<>> vertical_scrolls_;
    std::function<void(const Id&, const Id&)> focus_moved_;
    std::function<void()> before_focus_;
    std::map<Id, std::function<std::u16string()>, std::less<>> providers_;
    bool modal_ = false, stopped_ = false, dispatching_ = false, invoking_ = false, renderer_failed_ = false;
};
const Placement* find_placement(const Frame& frame, std::string_view id);
unsigned placement_text_offset(const Frame& frame, std::string_view id, int x);
// The caret position a pointer at (x, y) means; y only matters for multi-line fields.
TextHit placement_text_hit(const Frame& frame, std::string_view id, int x, int y);
// The character under a pointer rather than the nearest caret stop: what a double or triple click means.
unsigned placement_text_cell(const Frame& frame, std::string_view id, int x, int y);
UiState& access(Ui& ui);
const UiState& access(const Ui& ui);
// What a typed handle operation is written as: `return detail::operate(*this, [&]() -> Result { ... });`.
template <class Handle, class Body> auto operate(const Handle& handle, Body&& body, std::u16string_view gone = UiState::component_gone) {
    return UiState::operate(handle, std::forward<Body>(body), gone);
}
}
}
