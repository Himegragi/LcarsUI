#pragma once
#include "async_clipboard.hpp"
// Internal host, layout, legacy prototype and regression driver interface.
// Applications use isoduo/core.hpp and typed handles instead.
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"
#include "isoduo/semantics.hpp"
#include "animation.hpp"
#include "word_break.hpp"
#include <array>
#include <map>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>

namespace isoduo {
using Id = std::string;
namespace detail {
struct TextHit { unsigned offset = 0; bool upstream = false; };
// A component as the framework keeps it: the application's configuration plus its component type (#96). The public
// ComponentConfig names no type; Ui::add_x sets it, and a component created as data (the UiState constructor,
// seedTestControls) names it here: `{ComponentKind::label, {.id = "intro", .value = u"..."}}`.
struct Component : ComponentConfig {
    ComponentKind kind;
    Component(ComponentKind type, ComponentConfig config) : ComponentConfig(std::move(config)), kind(type) {}
};
// Whether a text control's initial value, visible lines and rich option are admissible (the rule of Ui::addTextField and of a field supplied as data).
bool validTextControl(const Component& control);
// UI-thread-owned controls addressed by logical IDs, never visual part paths.
class UiState {
    friend class isoduo::SemanticTree;
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
    // The notFound texts of a handle whose lifetime has ended. Each operation keeps the text it has always given (#94
    // decision 7): component handles, context menus and most others say componentGone, dialogs and tooltips uiGone,
    // and ListHandle::focus(OptionValue) answers without text (goneWithoutText).
    static constexpr std::u16string_view componentGone = u"Component no longer exists";
    static constexpr std::u16string_view uiGone = u"UI no longer exists";
    static constexpr std::u16string_view goneWithoutText = u"";
    // The one liveness preamble of every typed handle operation (#94). The body runs inside this Ui's operation scope; a
    // handle whose lifetime has ended (its Ui is gone, its component removed, a tooltip's source removed) runs nothing and
    // answers notFound with `gone`. R is the operation's own return type (Result or a Query). A typed handle operation
    // is written as `return detail::UiState::operate(*this, [&]() -> Result { ... });`.
    template <class Handle, class Body, class R = std::invoke_result_t<Body&>>
    static R operate(const Handle& handle, Body&& body, std::u16string_view gone = componentGone) {
        const bool alive = !handle.lifetime_.expired();
        OperationScope operation(alive ? handle.ui_ : nullptr);
        bool pending = false;
        if constexpr (std::is_constructible_v<ComponentHandle, Handle>)
            pending = alive && std::this_thread::get_id() == handle.ui_->owner_ && handle.ui_->removalPending(handle.id_);
        if (!alive || pending) {
            R answer;
            answer.status = Status::notFound;
            answer.message = std::u16string(gone);
            return answer;
        }
        return body();
    }
    explicit UiState(std::vector<Component> controls);
    ~UiState();
    // Called only by Ui::add_x (through addComponent in core.cpp), which builds `control` with the matching kind (#96).
    TextFieldHandle addTextField(Component control);
    ButtonHandle addButton(Component control);
    LabelHandle addLabel(Component control);
    CheckboxHandle addCheckbox(Component control);
    RadioGroupHandle addRadioGroup(Component control, std::vector<OptionItem> options);
    SliderHandle addSlider(Component control, SliderRange range);
    ProgressHandle addProgress(Component control);
    DropdownHandle addDropdown(Component control, std::vector<OptionItem> options);
    TabsHandle addTabs(Component control, std::vector<TabPage> pages);
    NavigationListHandle addNavigationList(Component control, std::vector<NavigationPage> pages, OptionValue initial);
    Result activateTab(const Id& id, OptionValue page);
    Result focusPage(const Id& id, OptionValue page);
    Result addPageMember(const Id& id, OptionValue page, ComponentHandle member);
    Result enablePage(const Id& id, const OptionValue& page, bool enabled);
    Result onPageChange(const Id& id, std::function<void(OptionValue, OptionValue)> callback);
    Query<OptionValue> activePage(const Id& id) const;
    Query<ComponentSnapshot> handleDescribe(std::string_view id) const;
    Result focusError(const Id& id);
    ListHandle addList(Component control, std::vector<OptionItem> items);
    TooltipHandle addTooltip(ComponentHandle source, std::u16string text, unsigned delayMs);
    void tooltipTime(std::uint64_t now);
    void tooltipClock(std::function<std::uint64_t()> clock) { tooltipClock_ = std::move(clock); }
    std::optional<std::uint64_t> tooltipDeadline() const;
    void tooltipHover(std::string_view id);
    const std::u16string* tooltipText(std::string_view id) const;
    void windowActive(bool active);
    // Pointer hover. The host (or a test) reports the real mouse in window coordinates; touch and pen
    // messages (touch = true) are ignored. One call moves both the hover target and the tooltip dwell.
    void pointerOver(const Frame& frame, int x, int y, bool touch = false);
    void pointerLeft();  // the pointer left the client area
    // A drag that must keep the hover where it started (text selection, scrollbar thumb). An id first
    // makes that component the target; an empty id freezes the current one. A slider drag holds it by itself.
    void hoverCapture(std::string_view id);
    void hoverRelease();
    // Re-evaluates the real pointer at its last position; arrange calls it after every layout.
    void hoverRefresh(const Frame& frame);
    // The admitted target (see Ui::hover) and whether the overlay is drawn for it.
    std::optional<HoverTarget> hoverTarget() const;
    bool hoverDrawn(std::string_view id, const OptionValue* item) const;
    Result simulateHover(std::string_view id, const std::optional<OptionValue>& item);
    Result clearSimulatedHover();
    Query<std::optional<HoverTarget>> hoverQuery() const;
    void refreshTooltips();
    void scrolled(); // input moved a viewport outside popup content
    ScrollState& dropdownScroll(const Id& id) { return dropdowns_.at(id).scroll; }
    bool dropdownReveal(const Id& id) { return std::exchange(dropdowns_.at(id).reveal, false); }
    void popupScrolled() { if (invalidate_) invalidate_(); }
    DialogHandle addDialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial, ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name);
    Result openDialog(const Id& id, const ComponentHandle* opener);
    Result settleDialog(const Id& id, bool confirmed);
    void closePopup();
    Result sliderSetValue(const Id& id, int value);
    // A pointer drag converts the mouse x to a value against the track captured at press, so a layout
    // that moves under the pointer cannot change the value. The thumb is drawn at the pointer, not the value.
    Result sliderPointerDown(const Id& id, int x, Rect track, float scale = 1.f);
    Result sliderPointer(const Id& id, int x);
    void sliderTrackRefresh(const Frame& frame);  // end of every arrange: where the held thumb is drawn now
    std::optional<int> sliderDragX(std::string_view id) const;
    // #75 shared animation clock. The host (or a test) supplies the time source in milliseconds; with
    // none installed every change is instant. Motion plays only when the source exists, presentation
    // motion is automatic, the system animation preference is on and the window is visible.
    void animationTime(std::function<std::uint64_t()> source) { animations_.timeSource(std::move(source)); }
    void setSystemAnimations(bool enabled);  // Windows "Show animations in Windows"; false ends running ones
    void setWindowHidden(bool hidden);       // minimized / fully covered; true ends running ones
    bool motionEnabled() const;
    void finishAnimations();                  // every running animation jumps to its end
    // The one query behind Ui::animating and SemanticTree::animating (same admission and statuses).
    Query<bool> animating() const;
    bool animationRunning() const;            // host: schedule the next frame only while true
    // Thumb position as a 0..1 share of the track while a transition runs; nothing otherwise.
    std::optional<double> sliderAnimatedRatio(std::string_view id) const;
    // Pointer x the thumb is drawn at while the pointer holds it (a drag, or any press with motion off).
    std::optional<int> sliderHeldX(std::string_view id) const;
    Result resetToSource(const Id& id);
    Result useLatest(const Id& id);
    Result connectWrite(const Id& id, std::function<Result(const Change<TextFieldHandle>&)> write, bool immediate);
    Result connectRichWrite(const Id& id, std::function<Result(const Change<TextFieldHandle, RichText>&)> write, bool immediate);
    bool isRich(std::string_view id) const;
    Result confirm(const Id& id);
    ComponentHandle layoutHandle(std::string_view id);
    std::optional<Id> layoutId(const ComponentHandle& handle) const;
    bool retiredComponent(const ComponentHandle& handle) const;
    // What registered component types (src/component_type.cpp) use to act on the state around them.
    // The typed handle of a component of the given handle type; a type's own handle() returns it.
    template <class Handle> ComponentHandle typedHandle(const Component& control) {
        OperationScope operation(this);
        return Handle(this, componentLifetimes_.at(control.id), control.id);
    }
    // A pointer press arms its component with the row under the press (and that row's value); the release over the
    // same component then reaches pointerUp of its type. A key press arms the component for the release of Space.
    void armPointer(const Id& id, std::optional<std::size_t> option = std::nullopt, std::optional<OptionValue> value = std::nullopt) {
        pointerArmed_ = id; pointerOption_ = option; pointerOptionValue_ = std::move(value);
    }
    void armKey(const Id& id) { keyArmed_ = id; }
    bool popupOpen(std::string_view id) const { return popupSource_ == id; }
    void invalidate() { if (invalidate_) invalidate_(); }
    bool rendererFailed() const { return rendererFailed_; }
    bool hasAction(std::string_view id) const { return actions_.contains(id); }
    bool textComposing(std::string_view id) const { auto it = edits_.find(id); return it != edits_.end() && it->second.session.composing; }
    // The per-type state and the engines a component type reaches into (the maps and helpers declared private below).
    // Only declared here: the definition lives in component_type.cpp, so no other file can use it.
    struct Internals;
    Result connectConfirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<ConfirmationResult(const std::vector<ConfirmFieldChange>&)> submit);
    UiState(const UiState&)=delete;
    UiState& operator=(const UiState&)=delete;
    const std::vector<Component>& controls() const { return controls_; }
    // Internal inspection seam for reclamation regressions.
    std::array<std::size_t, 10> storageCounts() const {
        std::size_t members = 0;
        for (const auto& [id, tabs] : tabs_)
            for (const auto& page : tabs.pages) members += page.members.size();
        return {controls_.size(), pageMembers_.size(), members, actions_.size(), errors_.size(),
                tooltips_.size(), componentLifetimes_.size(), confirmationButtons_.size(),
                confirmationMembers_.size(), pendingRemovals_.size()};
    }
    const Component* find(std::string_view id) const;
    bool registerable(std::string_view id);
    void seedTestControls(std::vector<Component> controls);
    std::map<Id, std::string, std::less<>> overlayIds_;
    std::map<Id, Id, std::less<>> tooltipIds_;
    std::vector<std::string> diagnostics_;
    std::optional<ComponentSnapshot> describe(std::string_view id) const;
    std::vector<Verb> snapshotVerbs(const Component&, const ComponentSnapshot&, bool item = false) const;
    void semanticFrame(const Frame& frame);
    std::string semanticPage(std::string_view id) const;
    bool confirmationGroupContains(std::string_view button, std::string_view field) const {
        auto group = confirmationMembers_.find(button);
        return group != confirmationMembers_.end() &&
            std::find(group->second.begin(), group->second.end(), field) != group->second.end();
    }
    void frameVisibility(std::map<Id, bool, std::less<>> visibility);
    // Current display text: field content, or label text (bound provider wins over static value).
    std::u16string text(std::string_view id) const;
    EditSession* edit(std::string_view id);
    Result setValue(std::string_view id, std::u16string value);
    Result invoke(std::string_view id);
    Result available(std::string_view id, bool ignoreRendererFailure = false) const;
    Result availableOption(std::string_view id, const OptionValue& value) const;

    void bind(Id id, Action action);
    void onChange(std::function<void()> callback) { changed_ = std::move(callback); }
    // Host requests a new frame when a source notification changes the view.
    Result setPresentation(const PresentationSettings& settings);
    const PresentationSettings& presentation() const { return presentation_; }
    // Focus indicator display state. Only real input moves it: keyDown and the host's pointer press.
    // Programmatic focus, semantic operations and UIA never change it.
    // `never` is checked first and wins over everything, the system's "always show keyboard cues" setting included.
    bool focusIndicatorVisible() const { return presentation_.focusIndicator != FocusIndicatorMode::never && keyboardFocusCues(); }
    // Whether the user is working with the keyboard (or the application forces the display on). Drawing is decided by
    // focusIndicatorVisible(); this one also drives what is not drawing, such as the tooltip that follows keyboard focus.
    bool keyboardFocusCues() const { return keyboardCues_ || systemCues_ || presentation_.focusIndicator == FocusIndicatorMode::always; }
    // The per-component answer the paint code uses: focused, and either shown or an editable text field.
    bool drawsFocusIndicator(const ComponentSnapshot& semantics) const;
    // A pointer press anywhere in the client area, including empty space and inert components.
    void pointerPressed();
    // The host mirrors Windows SPI_GETKEYBOARDCUES here (at startup and on WM_SETTINGCHANGE).
    void setSystemKeyboardCues(bool enabled);
    void onPresentation(std::function<void()> callback) { presentationChanged_ = std::move(callback); }
    Result setPaletteOverrides(std::vector<PaletteOverride> overrides);
    const std::vector<PaletteOverride>& paletteOverrides() const { return paletteOverrides_; }
    // The single color entry for painting: built-in palette of this appearance for the current
    // theme variant (or an explicit one), with the override list applied.
    Palette effectivePalette(const Appearance& appearance, const ThemeVariant& variant) const;
    Palette effectivePalette(const Appearance& appearance) const { return effectivePalette(appearance, presentation_.variant); }
    void bindAppearance(const Appearance* appearance) { boundAppearance_ = appearance; }
    const Appearance* boundAppearance() const { return boundAppearance_; }
    void onInvalidate(std::function<void()> callback) { invalidate_ = std::move(callback); }
    void onRelayout(std::function<void()> callback) { relayout_ = std::move(callback); }
    void setScrollableText(std::string id, ScrollState& state, int maximum, int lineHeight);
    // Sideways scrolling of a vertical-writing label. The state belongs to the label; the offset counts pixels
    // from the start of the text (right edge). A label that does not scroll is not focusable.
    ScrollState& verticalScroll(const Id& id) { return verticalScrolls_[id]; }
    void setVerticalScroll(const Id& id, int maximum, int columnWidth);
    void clearVerticalScroll(const Id& id);
    void onSemanticPending(std::function<void()> callback) {
        semanticSchedule_ = std::move(callback);
        if (semanticSchedule_ && !semanticPending_.empty()) semanticSchedule_();
    }
    void onReveal(std::function<void(const Id&)> callback) { revealRequested_ = std::move(callback); }
    // Called after each focus move with the previous and new focus (either may be empty).
    void onFocus(std::function<void(const Id& from, const Id& to)> callback) { focusMoved_ = std::move(callback); }
    // Native input methods must finalize before core changes the focused editing target.
    void beforeFocus(std::function<void()> callback) { beforeFocus_ = std::move(callback); }
    // A label whose text is read from owner data on demand, so the label never holds a copy. With `subscribe`, the
    // owner's notifications ask for a repaint until the Ui ends or the label is bound again; binding again cancels the
    // previous subscription once the new one exists (replaceSource: a throwing subscribe keeps the old binding).
    Result bindText(std::string_view id, std::function<std::u16string()> provider,
                     decltype(Source<std::u16string>::subscribe) subscribe = {});
    // Focus. Declaration order is the Tab order; labels and unavailable controls are skipped.
    const Id& focused() const { return focus_; }
    Result focus(std::string_view id);
    std::vector<Id> focusOrder() const;
    // Input routed to the focused control. Hosts call these after translating native messages.
    // Buttons: Enter activates on press, Space on release, pointer on release over the pressed
    // control. A press belongs to the control that received it and dies with focus moves.
    Result addShortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope);
    Result keyDown(KeyPress key);
    Result keyUp(Key key);
    // The host hit-tests and passes the control under the pointer (empty when none).
    Result pointerDown(std::string_view id, std::optional<std::size_t> option = std::nullopt);
    Result pointerUp(std::string_view id, std::optional<std::size_t> option = std::nullopt);
    // Native capture loss or window deactivation: drop every pending press.
    void cancelInput();
    void cancelPointer();
    // C03 text field operations. Keyboard, pointer and these semantic calls share admission rules.
    // Offsets are UTF-16 and never split a surrogate pair. Each successful edit is one undo entry.
    Result textInput(std::u16string_view text);  // committed characters for the focused field
    Result select(std::string_view id, unsigned anchor, unsigned caret);
    Result undo(std::string_view id);
    Result redo(std::string_view id);
    Result paste(std::string_view id, std::u16string_view text);
    Result pasteClipboard(std::string_view id);  // Ctrl+V: styles come along when the clipboard holds a matching payload
    Result copy(std::string_view id);  // writes the selection to the clipboard
    Result cut(std::string_view id);
    void setClipboard(Clipboard* clipboard) { pendingClipboard_.reset(); clipboard_ = clipboard; }
    void setAsyncClipboard(AsyncClipboard* clipboard) { pendingClipboard_.reset(); asyncClipboard_ = clipboard; }
    Result completeClipboard(ClipboardDelivery delivery);
    // IME composition targets the field focused when it starts. Preedit stays separate from
    // committed text; one commit is one undo entry; editing keys and commands wait while composing.
    // E01 default: focus leaving commits leftover preedit once, as Microsoft Bopomofo does on blur.
    // Hosts finalize with the IME first, so the IME and core never both commit.
    Result compositionUpdate(std::u16string preedit, unsigned caret);
    Result compositionCommit(std::u16string_view text);
    Result compositionCancel();
    // Horizontal display offset of a field in DIP. View state owned here so owner resets clear it.
    float textOffset(std::string_view id) const;
    void setTextOffset(std::string_view id, float offset);
    unsigned textAnchor(std::string_view id, int textSize) const;
    void setTextAnchor(std::string_view id, int textSize, unsigned anchor);
    // Multi-line view state. arrange hands over the lines it measured and gets the settled first
    // visible line back (view.first): layout is the only place that knows where lines break.
    void settleLines(std::string_view id, TextFieldView& view, int textSize, int width);
    const TextFieldView* fieldView(std::string_view id) const;  // lines as last arranged; null for other kinds
    ScrollState& lineScroll(const Id& id) { return edits_.at(id).multi.scroll; }
    bool scrollLinesTo(std::string_view id, unsigned first);  // clamps; true when the position changed
    // A pointer click or drag: like select, but may place the caret at the end of a wrapped line.
    Result selectPointer(std::string_view id, unsigned anchor, unsigned caret, bool upstream);
    // Multi-line configuration and view position, shared by the typed handle and the semantic verb.
    Result setVisibleLines(std::string_view id, unsigned lines);
    Result setScrollLine(std::string_view id, unsigned first);
    Query<unsigned> scrollLine(std::string_view id) const;
    // Text flow (#80), labels and text fields; shared by the typed handles and the semantic verbs.
    Result setTextAlign(std::string_view id, TextAlign align);
    Result setJustifyMethod(std::string_view id, JustifyMethod method);
    Result setLineSpacing(std::string_view id, double multiple);
    Result setParagraphSpacing(std::string_view id, int dip);
    // Words, paragraphs and multi-click (#83). Queries and selections share select()'s admission rules.
    void setWordBreaker(std::shared_ptr<const WordBreaker> breaker) { wordBreaker_ = std::move(breaker); }  // null: the system ICU
    Result setWordSegmentation(std::string_view id, WordSegmentation mode);
    Query<WordSegmentation> wordSegmentation(std::string_view id) const;
    Query<WordSegmentation> wordSegmentationInUse(std::string_view id) const;
    Query<TextSpan> wordAt(std::string_view id, unsigned offset) const;
    Query<TextSpan> paragraphAt(std::string_view id, unsigned offset) const;
    Query<unsigned> wordStop(std::string_view id, unsigned offset, bool forward) const;
    // The UIA word and paragraph unit boundaries of a field's committed text (empty for anything that is not a text field).
    std::vector<unsigned> wordUnitBoundaries(std::string_view id) const;
    std::vector<unsigned> paragraphBoundaries(std::string_view id) const;
    Result selectWord(std::string_view id, unsigned offset);
    Result selectParagraph(std::string_view id, unsigned offset);
    Result doubleClick(std::string_view id, unsigned offset);
    Result tripleClick(std::string_view id, unsigned offset);
    // The host reads the system double-click settings and a clock and lets core count the clicks, so tests
    // can inject both. Without a clock every press counts as a single click.
    struct ClickSettings { std::uint64_t intervalMs = 500; int width = 4, height = 4; };
    void setClickSettings(ClickSettings settings) { clickSettings_ = settings; }
    void setClickClock(std::function<std::uint64_t()> clock) { clickClock_ = std::move(clock); }
    // A primary-button press on a text field: `caret` is the nearest caret position (single click), `cell` the
    // character under the pointer (double and triple click), x/y the press position in the click's coordinates.
    Result textPointerDown(std::string_view id, TextHit caret, unsigned cell, int x, int y, bool extend);
    // A drag with the button held: extends by the unit the press started with (character, word or paragraph).
    Result textPointerDrag(std::string_view id, TextHit caret, unsigned cell);
    void endTextDrag();
    // Owner (application) configuration. These are not user operations and skip admission checks.
    Result setError(std::string_view id, std::u16string message);  // empty clears
    void enable(std::string_view id, bool enabled);  // disabling the focused control releases focus
    void setReadOnly(std::string_view id, bool readOnly);
    void check(std::string_view id, bool checked);
    // Label owner configuration (vertical writing). A change asks the host for a new frame.
    static Result writingSupport(const Component& control);
    void setLabelWriting(std::string_view id, std::optional<WritingMode> mode, std::optional<int> digits);
    // Owner replaces a field's content: composition, history and display offset start over.
    void replaceText(std::string_view id, std::u16string value);
    void modal(bool active) { modal_ = active; if (active) { closePopup(); closeMenu(); } hoverClear(); refreshTooltips(); }
    void replaceRich(std::string_view id, RichText value);  // owner refresh of a rich field: text and spans
    // Rich text (Component::rich fields; others answer notSupported). Shared by the typed handle, the semantic
    // layer, Ctrl+B/I/U and any menu: they all run these same functions.
    Result setRichValue(std::string_view id, RichText value);
    Query<RichText> richValue(std::string_view id) const;
    Result setStyle(std::string_view id, unsigned begin, unsigned end, const StyleChange& change);
    Result toggleStyle(std::string_view id, StyleAttribute attribute);  // selection, or the typing style at a caret
    Result applyStyle(std::string_view id, const StyleChange& change);  // selection, or the typing style at a caret
    Query<TextStyle> typingStyle(std::string_view id) const;
    Result setTypingStyle(std::string_view id, TextStyle style);
    Result setColors(std::string_view id, std::vector<NamedColor> colors);
    Query<std::vector<NamedColor>> colors(std::string_view id) const;
    // The committed spans of a rich field (null for others) and the spans of the visible text, which an
    // IME composition is part of (the preedit takes the typing style), for layout and painting.
    const std::vector<StyleSpan>* styleSpans(std::string_view id) const;
    std::vector<StyleSpan> visibleStyleSpans(std::string_view id) const;
    // The color of a name for the current theme variant, nullopt for an empty or unknown name.
    std::optional<Color> namedColor(std::string_view id, std::string_view name) const;
    bool modal() const { return modal_; }
    bool stopped() const { return stopped_; }
    Result creationAccess() const;
    void rendererFailed(bool failed);
    // ---- Context menu (src/context_menu.cpp). One menu is open per window; it never changes focus_.
    struct MenuEntry { std::string id; std::u16string label, hint; bool enabled = true, separator = false; char16_t accessKey = 0; };
    struct MenuView {
        Id target;
        std::optional<OptionValue> item;      // list row the menu was opened for
        std::optional<OptionValue> anchorItem;  // row a keyboard-opened menu hangs from
        std::optional<MenuPosition> at;       // none: the keyboard position of the target
        std::vector<MenuEntry> entries;
        std::string highlighted;
    };
    bool hasMenu(const Component& control) const;
    bool menuOpen() const { return menu_.has_value(); }
    Query<bool> menuOpenQuery() const;
    Query<MenuSnapshot> menuInfo() const;
    Query<ContextMenuHandle> attachMenu(const Id& target, std::vector<MenuItem> items, MenuMode mode);
    std::optional<MenuView> menuView() const;
    ScrollState* menuScroll() { return menu_ ? &menu_->scroll : nullptr; }
    bool menuReveal() { return menu_ && std::exchange(menu_->reveal, false); }
    // at: where to open (window pixels); none for the keyboard position. textOffset: the pointer's caret offset.
    Result openMenu(std::string_view id, std::optional<MenuPosition> at, std::optional<OptionValue> item,
                     std::optional<unsigned> textOffset = std::nullopt);
    // Right-button release over a component: option is the pointer's row (lists).
    Result menuPointerOpen(std::string_view id, std::optional<std::size_t> option, std::optional<unsigned> textOffset, MenuPosition at);
    Result closeMenu();
    Result menuExecute(std::string_view itemId);
    Result menuHighlight(std::string_view itemId);
    Result menuPointerDown(std::string_view itemId);
    Result menuPointerUp(std::string_view itemId);
    Result menuKeyDown(KeyPress key);
    Result menuText(std::u16string_view text);
    void menuFrame(const Frame& frame);
    std::optional<Query<NodeSnapshot>> menuNode(const NodeRef& ref);
    std::optional<std::vector<NodeRef>> menuChildren(const NodeRef& parent) const;
private:
    friend class ::isoduo::ContextMenuHandle;
    friend class ::isoduo::TabsHandle;
    friend class ::isoduo::NavigationListHandle;
    friend class ::isoduo::TextFieldHandle;
    friend class ::isoduo::ButtonHandle;
    friend class ::isoduo::LabelHandle;
    friend class ::isoduo::CheckboxHandle;
    friend class ::isoduo::RadioGroupHandle;
    friend class ::isoduo::SliderHandle;
    friend class ::isoduo::ProgressHandle;
    friend class ::isoduo::DropdownHandle;
    friend class ::isoduo::ListHandle;
    friend class ::isoduo::DialogHandle;
    friend class ::isoduo::TooltipHandle;
    std::map<Id, std::function<void()>, std::less<>> blurCallbacks_;
    Result configurationAccess(std::string_view id, bool readOnly = false) const;
    // A text field's read source: plain, or rich for a field created with Component::rich (#95: one place for both).
    using TextFieldSource = std::variant<Source<std::u16string>, Source<RichText>>;
    Result connectSource(const Id& id, TextFieldSource source);
    void sourceChanged(const Id& id);
    std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
    PresentationSettings presentation_;
    std::function<void()> presentationChanged_;
    std::vector<PaletteOverride> paletteOverrides_;
    const Appearance* boundAppearance_ = nullptr;
    std::thread::id owner_ = std::this_thread::get_id();
    std::vector<Component> controls_;
    std::map<Id, std::uint64_t, std::less<>> semanticInstances_;
    std::uint64_t nextSemanticInstance_ = 0;
    struct Snapshot { std::u16string text; Selection selection; std::vector<StyleSpan> spans; };
    // The data connection of every field that writes back to the application (#94): text field, checkbox, radio group,
    // dropdown and slider. One copy of the state and of the rules on it; the public connect_*, confirm, useLatest and
    // draft keep their names and parameters. Change is the field's public change record (Change<TextFieldHandle>, Change<CheckboxHandle>, ...).
    template <class ChangeType> struct FieldConnection {
        using Change = ChangeType;
        bool connected = false;            // the source has delivered a valid value
        bool submitting = false;           // a write of this field is running
        bool confirmationMember = false;  // a confirmation group's button writes it
        std::function<Result(const Change&)> immediate, confirmed;
        std::function<void()> disconnect;
        // How the value gets written: an immediate or a confirmed write, or a confirmation group.
        bool hasWriteTiming() const { return confirmationMember || immediate || confirmed; }
        // What every change of the value needs (the "can be written" rule): a source and a write timing.
        bool writable() const { return connected && hasWriteTiming(); }
        // Whether a write of this timing may be connected: a source, no group, and not the other timing.
        bool acceptsWrite(bool immediateTiming) const {
            return connected && !confirmationMember && !(immediateTiming ? bool(confirmed) : bool(immediate));
        }
        // Whether the field may join a confirmation group: a source and no write timing yet.
        bool joinable() const { return connected && !hasWriteTiming(); }
    };
    struct Field : FieldConnection<Change<TextFieldHandle>> {
        EditSession session;
        std::vector<Snapshot> past, future;  // field history; never shared between fields
        float offset = 0;
        int offsetTextSize = 0;
        unsigned offsetAnchor = 0;
        struct MultiView {
            TextFieldView view;               // lines as last arranged (geometry rectangles are unused here)
            std::u16string text;          // the visible text those lines were laid out from
            std::vector<StyleSpan> styles; // and its styled runs (rich fields): a style change can re-wrap too
            int size = 0, width = 0;      // text size and wrap width they were laid out at
            unsigned revision = 0;        // layoutRevision_ they were laid out at: a property change makes them stale
            unsigned first = 0;           // first visible line
            unsigned anchor = 0;          // visible-text offset where that line starts: survives a re-wrap
            unsigned caret = 0;           // caret seen by the last arrange: a different one reveals itself
            bool upstream = false, placed = false;
            std::optional<unsigned> pendingFirst;  // set before the first arrange
            float wantX = 0;             // the column an Up/Down run keeps, in pixels from the line start
            unsigned wantFrom = ~0u;     // the caret that run last left it at: any other caret starts a new run
            ScrollState scroll;           // pixel offset = first * line height, for the field's scrollbar region
        } multi;
        WordSegmentation segmentation = WordSegmentation::dictionary;
        // A double or triple click arms a drag that grows by that unit while the selection stays as the click left it.
        struct UnitDrag { enum class Unit { character, word, paragraph } unit = Unit::character; TextSpan origin; Selection last; } drag;
        TextFieldSource source;                // empty until connected; a rich field's is a Source<RichText>
        std::u16string sourceValue, origin;
        bool rich = false;                     // Component::rich
        std::vector<StyleSpan> spans, sourceSpans, originSpans;  // committed styles, last source value, last written value
        std::optional<TextStyle> typing;       // the temporary typing style, valid while the selection has not moved
        std::uint64_t typingMoves = 0;
        std::vector<NamedColor> colors;
    };
    std::map<Id, Field, std::less<>> edits_;
    struct BoolField : FieldConnection<Change<CheckboxHandle>> {
        Source<bool> source;
        bool value = false, sourceValue = false, origin = false;
    };
    std::map<Id, BoolField, std::less<>> bools_;
    struct RadioField : FieldConnection<Change<RadioGroupHandle>> {
        Source<std::optional<OptionValue>> source;
        std::vector<OptionItem> options;
        std::optional<OptionValue> value, sourceValue, origin;
    };
    std::map<Id, RadioField, std::less<>> radios_;
    struct DropdownField : FieldConnection<Change<DropdownHandle>> {
        Source<std::optional<OptionValue>> source;
        std::vector<OptionItem> options;
        std::optional<OptionValue> value, sourceValue, origin, candidate;
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
    std::map<Id, std::pair<Id, std::size_t>, std::less<>> pageMembers_;
    struct ListField {
        std::vector<OptionItem> items;
        Source<std::optional<OptionValue>> source;
        std::optional<OptionValue> cursor;
        bool writing = false;
        std::function<Result(const Change<ListHandle>&)> immediate;
        std::function<void()> disconnect;
    };
    std::map<Id, ListField, std::less<>> lists_;
    struct TooltipState {
        std::u16string text;
        unsigned delayMs = 500;
        std::uint64_t deadline = 0;
        bool eligible = false, visible = false, dismissed = false;
    };
    struct MenuAttachment { MenuMode mode = MenuMode::append; std::vector<MenuItem> items; };
    std::map<Id, MenuAttachment, std::less<>> menuAttachments_;
    // The opening every ContextMenuHandle operation shares: the configuration check (readOnly for a query), then the
    // attachment on this target, notFound "Menu no longer exists" without one. On ok, `attachment` points at it.
    Result menuAttachment(std::string_view target, MenuAttachment*& attachment, bool readOnly = false);
    struct OpenMenu {
        Id target;
        std::optional<OptionValue> item;
        std::optional<MenuPosition> at;
        std::string highlighted, pressed;
        ScrollState scroll;
        bool reveal = false, clipboardText = false;
    };
    std::optional<OpenMenu> menu_;
    std::map<std::string, Rect, std::less<>> semanticMenuRows_;
    std::vector<MenuEntry> menuEntries(const Id& target) const;
    Result menuRunBuiltin(const Id& target, std::string_view command);
    Result validateMenuItems(const std::vector<MenuItem>& items) const;
    void menuItemsChanged(const Id& target);
    Result deleteSelection(std::string_view id);  // Delete and the Delete menu item share it
    std::map<Id, TooltipState, std::less<>> tooltips_;
    Id tooltipHover_;
    // Pointer hover. hover_ is the raw target; hoverTarget() filters it against the live state.
    std::optional<HoverTarget> hover_;
    bool hoverSimulated_ = false, hoverCapture_ = false;
    std::optional<std::pair<int, int>> hoverPosition_;  // last real pointer position
    bool hoverLocked() const;
    bool hoverAdmitted(const HoverTarget& target) const;
    void hoverSet(std::optional<HoverTarget> target);
    void hoverFromFrame(const Frame& frame, int x, int y);
    void hoverClear();
    std::uint64_t tooltipNow_ = 0;
    std::optional<std::uint64_t> tooltipPausedAt_;
    std::function<std::uint64_t()> tooltipClock_;
    bool windowActive_ = false, keyboardCues_ = false, systemCues_ = false;
    void focusCuesChanged();
    Id popupSource_;
    std::map<Id, bool, std::less<>> visibility_;
    bool hasFrameVisibility_ = false;
    std::map<Id, Rect, std::less<>> semanticBounds_;
    std::map<Id, std::vector<Rect>, std::less<>> semanticOptions_;
    std::vector<Id> semanticOrder_;
    std::map<Id, Rect, std::less<>> semanticPositions_;
    std::set<Id, std::less<>> diagnosedNames_;
    struct DialogState {
        std::vector<Id> members;
        Id initial, fallback, opener, nameLabel;
        std::function<void(bool)> result;
        bool configured = false, transitioning = false;
    };
    std::map<Id, DialogState, std::less<>> dialogs_;
    Id activeDialog_;
    bool dialogTransitioning_ = false;
    bool dialogMember(std::string_view id) const;
    Result selectDropdown(const Id& id, OptionValue value);
    void dropdownSourceChanged(const Id& id);
    struct NumericField : FieldConnection<Change<SliderHandle>> {
        SliderRange range;
        Source<int> source;
        bool sourceInvalid = false;
        std::u16string errorBeforeSource;
        int value, sourceValue, origin;
    };
    std::map<Id, NumericField, std::less<>> numerics_;
    // The field state behind a confirmable handle, by handle kind (the one place that pairs a kind with its map).
    template <class Handle> auto& fieldsOf() {
        if constexpr (std::is_same_v<Handle, TextFieldHandle>) return edits_;
        else if constexpr (std::is_same_v<Handle, CheckboxHandle>) return bools_;
        else if constexpr (std::is_same_v<Handle, RadioGroupHandle>) return radios_;
        else if constexpr (std::is_same_v<Handle, DropdownHandle>) return dropdowns_;
        else { static_assert(std::is_same_v<Handle, SliderHandle>); return numerics_; }
    }
    // Runs f on the connection of the field with this id, whichever confirmable kind (a ConfirmField alternative) it is.
    template <class F> void withConnection(std::string_view id, F&& f) {
        [&]<class... Handle>(std::variant<Handle...>*) {
            auto visit = [&](auto& fields) { if (auto it = fields.find(id); it != fields.end()) f(it->second); };
            (visit(fieldsOf<Handle>()), ...);
        }(static_cast<ConfirmField*>(nullptr));
    }
    // The write paths the checkbox, radio group, dropdown and slider share (a text field adds history and composition).
    template <class Connection> static Result connectFieldWrite(Connection& field, std::function<Result(const typename Connection::Change&)> write, bool immediate);
    template <class Handle> Result confirmField(const Handle& handle);
    template <class FieldType> Result writeImmediately(const Id& id, FieldType& field, const typename FieldType::Change& change);
    // The connection swap of every connectSource that subscribes to the application's source (ADR 0003). The new
    // source's subscribe runs first, before anything changes: if it throws, `installed` and `disconnect` keep the old
    // source and its live subscription and the call fails without stopping the Ui. Only then is the old subscription
    // cancelled and the new source installed. A notification the subscribe fires before returning sees the old source.
    template <class Installed, class Subscribe>
    Result replaceSource(Installed& installed, std::function<void()>& disconnect, Installed source, Subscribe&& subscribe);
    // The first read of a source replaceSource installed. The old connection is gone by then, so a throwing read stops the Ui.
    template <class Read> Result readInstalledSource(Read&& read);
    struct ProgressField { Source<int> source; std::function<void()> disconnect; };
    std::map<Id, ProgressField, std::less<>> progresses_;
    void numericSourceChanged(const Id& id);
    Result selectOption(const Id& id, OptionValue value);
    void optionSourceChanged(const Id& id);
    Result toggleCheckbox(const Id& id);
    void boolSourceChanged(const Id& id);
    std::map<Id, Action, std::less<>> actions_;
    std::set<Id, std::less<>> confirmationButtons_;
    std::map<Id, std::shared_ptr<void>, std::less<>> componentLifetimes_;
    std::shared_ptr<void> componentLifetime(const Id& id) {
        return componentLifetimes_.try_emplace(id, std::make_shared<int>(0)).first->second;
    }
    Result removeComponent(const Id& id);
    std::map<Id, std::vector<Id>, std::less<>> confirmationMembers_;
    std::vector<Id> pendingRemovals_;
    Id retiring_;
    bool removalPending(std::string_view id) const;
    unsigned operationDepth_ = 0;
    std::map<std::size_t, std::shared_ptr<std::function<void(const SemanticEvent&)>>> semanticSubscribers_;
    std::size_t nextSemanticSubscriber_ = 0;
    std::vector<SemanticEvent> semanticPending_;
    std::vector<NodeSnapshot> semanticBefore_;
    bool semanticDispatching_ = false;
    bool semanticInspecting_ = false;
    std::function<void()> semanticSchedule_;
    std::function<void(const Id&)> revealRequested_;
    Query<NodeSnapshot> semanticNode(const NodeRef&, const ComponentSnapshot&);
    std::vector<NodeSnapshot> semanticSnapshot();
    void semanticDiff();
    bool committingRemovals_ = false;
    void commitRemovals();
    std::map<Id, std::u16string, std::less<>> errors_;  // the one source for field errors and error labels
    struct ShortcutBinding { Shortcut chord; Id target, scope; };
    std::vector<ShortcutBinding> shortcuts_;
    Id focus_, keyArmed_, pointerArmed_, composing_, executing_;
    // origin/width: the track captured at press (the value basis). seen_*: the track the latest frame drew the thumb on.
    struct SliderDrag { Id id; double origin = 0, width = 1; int x = 0, pressX = 0; bool held = false; double seenOrigin = 0, seenWidth = 1; int threshold = 4; };
    std::optional<SliderDrag> sliderDrag_;
    AnimationClock animations_;
    bool systemAnimations_ = true, windowHidden_ = false;
    std::map<Id, int, std::less<>> sliderTargets_;  // value each running thumb transition is heading to
    static std::string sliderAnimationKey(std::string_view id) { return "slider:" + std::string(id); }
    bool sliderTransitionLive(std::string_view id) const;
    double sliderDisplayRatio(std::string_view id) const;
    void sliderStartedMoving(const Id& id, double from, int valueBefore);
    void endSliderDrag(bool slide);  // slide: the release of a drag eases to the tick; an interruption lands at once
    void sliderLand(std::string_view id);  // drop a running thumb transition: the thumb is at its value now
    std::optional<std::size_t> pointerOption_;
    std::optional<OptionValue> pointerOptionValue_;
    Clipboard* clipboard_ = nullptr;
    AsyncClipboard* asyncClipboard_ = nullptr;
    enum class ClipboardAction { copy, cut, paste };
    struct PendingClipboard {
        std::uint64_t request;
        ClipboardAction action;
        Id target;
        std::weak_ptr<void> lifetime;
        std::uint64_t revision, selectionChanges;
    };
    std::optional<PendingClipboard> pendingClipboard_;
    std::uint64_t nextClipboardRequest_ = 0;
    Result beginClipboard(std::string_view id, Field& field, ClipboardAction action, ClipboardData data = {});
    Result writeSelection(std::string_view id, Field& field, ClipboardAction action);
    Result applyClipboard(std::string_view id, std::u16string_view text, std::string_view payload);
    std::shared_ptr<const WordBreaker> wordBreaker_;
    const WordBreaker& dictionary() const { return wordBreaker_ ? *wordBreaker_ : systemIcuBreaker(); }
    Words wordsOf(const Field& field) const { return segmentWords(field.session.text, field.segmentation, dictionary()); }
    ClickSettings clickSettings_;
    std::function<std::uint64_t()> clickClock_;
    struct ClickRun { Id id; int x = 0, y = 0; std::uint64_t time = 0; unsigned count = 0; } clickRun_;
    unsigned countClick(const Id& id, int x, int y);
    Result selectUnit(std::string_view id, unsigned offset, Field::UnitDrag::Unit unit, bool arm);
    Result focusable(const Component& control) const;
    Result moveFocus(Id next);
    std::vector<std::pair<Id, Id>> pendingFocus_;  // focus notifications queued behind the current one
    bool notifyingFocus_ = false;
    Result writable(std::string_view id, Field*& field);
    Result richWritable(std::string_view id, Field*& field);
    Result replaceSelection(std::string_view id, Field& field, std::u16string_view insertion);
    // Replaces [first, last) of the text; insertedSpans (relative to the insertion) override the typing style.
    Result replaceRange(std::string_view id, Field& field, unsigned first, unsigned last, std::u16string_view insertion,
                         const std::vector<StyleSpan>* insertedSpans);
    Result commitStyle(std::string_view id, Field& field, std::vector<StyleSpan> spans);
    TextStyle insertionStyle(const Field& field, unsigned first, unsigned last) const;
    bool sourceMatches(const Field& field) const { return field.session.text == field.sourceValue && field.spans == field.sourceSpans; }
    Result finishEdit(std::string_view id, Field& field);
    Result submitField(std::string_view id, Field& field,
                        const std::function<Result(const Change<TextFieldHandle>&)>& write, bool command);
    Result travel(std::string_view id, bool forward);
    Result editKey(KeyPress key);  // the keys a text field takes
    // Multi-line cursor movement over the lines the last arrange settled. A host that can lay out
    // again on demand registers onRelayout so keys typed faster than frames never see stale lines.
    TextFieldView verticalView(const Id& id);
    // Bumped by every property that changes how text lines up (alignment, justification, spacing, the
    // justify-as-left setting): the lines cursor keys use are stale until an arrange sees the new value.
    unsigned layoutRevision_ = 0;
    Result moveLines(const Id& id, int direction, unsigned count, bool shift);
    Result moveLineEdge(const Id& id, bool end, bool shift);
    Result notify();
    std::function<void()> changed_, invalidate_, relayout_;
    struct TextScroll { ScrollState* state; int maximum, lineHeight; bool horizontal = false; };
    std::map<std::string, TextScroll, std::less<>> textScrolls_;
    std::map<Id, ScrollState, std::less<>> verticalScrolls_;
    std::function<void(const Id&, const Id&)> focusMoved_;
    std::function<void()> beforeFocus_;
    // A bound label's read, and the cancellation of its source subscription (empty for a read function alone).
    // A subscription ends on replacement, at the component's removal safe point, or with ~UiState.
    struct TextProvider { std::function<std::u16string()> read; std::function<void()> disconnect; };
    std::map<Id, TextProvider, std::less<>> providers_;
    bool modal_ = false, stopped_ = false, dispatching_ = false, invoking_ = false, rendererFailed_ = false;
};
const Placement* findPlacement(const Frame& frame, std::string_view id);
unsigned placementTextOffset(const Frame& frame, std::string_view id, int x);
// The caret position a pointer at (x, y) means; y only matters for multi-line fields.
TextHit placementTextHit(const Frame& frame, std::string_view id, int x, int y);
// The character under a pointer rather than the nearest caret stop: what a double or triple click means.
unsigned placementTextCell(const Frame& frame, std::string_view id, int x, int y);
UiState& access(Ui& ui);
const UiState& access(const Ui& ui);
}
}
