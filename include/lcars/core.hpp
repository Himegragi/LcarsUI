#pragma once
#include "lcars/appearance.hpp"
#include "lcars/presentation.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace lcars {
// Labels may span lines and describe another component; an overflowing scroll_text label is focusable.
enum class ComponentKind { text, button, label, checkbox, radio_group, slider, dropdown, list, tabs, progress, navigation_list };
// What a label says about its target. Association is by logical ID, never by position or appearance.
enum class LabelKind { plain, name, description, error };
enum class Status { ok, not_found, disabled, read_only, blocked_by_modal, busy, invalid_value, wrong_thread, failed, not_supported, empty_history, hidden, renderer_failed };
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
// Horizontal placement of every display line inside the component's own width (#80). justify stretches
// every display line but the last one of a paragraph to the full width (#84); distribute stretches the
// last line, and a one-line text, too, and is for labels only (a text field refuses it with invalid_value).
enum class TextAlign { left, center, right, justify, distribute };
// Where justify and distribute may add the extra width (#84). automatic: around every CJK character and
// full-width punctuation mark and at every ASCII space, never inside a Latin word; spaces: only at ASCII
// spaces, so a line without any stays left-aligned; per_character: between any two characters, Latin
// letters included (such a line is drawn without ligatures). Only used while the alignment is justify or distribute.
enum class JustifyMethod { automatic, spaces, per_character };
// Text flow limits. Line spacing multiplies the font's own line height; paragraph spacing is extra
// space in DIP after each LF-ended paragraph (never between the pieces of one wrapped paragraph).
inline constexpr double min_line_spacing = 1.0, max_line_spacing = 3.0;
inline constexpr int max_paragraph_spacing = 48;
inline bool valid_line_spacing(double value) { return value >= min_line_spacing && value <= max_line_spacing; }  // false for NaN
inline bool valid_paragraph_spacing(int value) { return value >= 0 && value <= max_paragraph_spacing; }
// Writing direction of a label (display only). Vertical runs top to bottom with columns from right to
// left, laid out by the Taiwan Ministry of Education rules. Only labels support it.
enum class WritingMode { horizontal, vertical };
// Longest run of half-width digits that vertical text sets side by side in one cell (0 turns it off).
inline constexpr int max_vertical_digits = 3;
struct ComponentConfig {
    std::string id;
    std::u16string label = {};  // fallback accessible name when no name label targets this component
    std::string page = {};
    std::u16string value = {};  // text field content, or label display text
    bool enabled = true;
    bool read_only = false;
    bool checked = false;
    std::string target = {};         // labels: the component this text names, describes or reports errors for
    LabelKind label_kind = LabelKind::plain;
    std::size_t max_length = default_max_text_length;  // text fields only
    // Text fields only; fixed when the field is created. A multi-line field holds line breaks as a
    // single LF, wraps at its edge and scrolls vertically inside a box visible_lines tall.
    bool multiline = false;
    unsigned visible_lines = 3;  // multi-line height in lines, at least 1; later changed through the handle
    // Labels and text fields only (any other kind needs the defaults, else invalid_value). They apply
    // to the one component, can be changed at run time through its handle or the semantic layer, and
    // out-of-range values are invalid_value. The defaults draw exactly as before.
    TextAlign text_align = TextAlign::left;
    JustifyMethod justify_method = JustifyMethod::automatic;  // #84: only read while text_align is justify or distribute
    double line_spacing = 1.0;       // times the font's line height, 1.0-3.0; extra space splits above and below
    int paragraph_spacing = 0;       // DIP after each paragraph ended by an LF, 0-48
    WritingMode writing_mode = WritingMode::horizontal;  // labels only; other components reject vertical
    int vertical_digits = max_vertical_digits;           // labels only; 0-3, anything else is invalid_value
    // Multi-line text fields only (invalid_value on any other component); fixed when the field is created.
    // A rich field holds plain text plus style spans (RichText) and takes Ctrl+B / Ctrl+I / Ctrl+U itself.
    bool rich = false;
};
// What one run of rich text looks like. An empty color means the field's normal text color; a color
// name is looked up in the field's color list on every paint, so theme changes recolor it.
struct TextStyle {
    bool bold = false, italic = false, underline = false;
    std::string color;
    bool plain() const { return !bold && !italic && !underline && color.empty(); }
    bool operator==(const TextStyle&) const = default;
};
// One styled run of a rich value: UTF-16 offsets into its plain text.
struct StyleSpan {
    unsigned start = 0, length = 0;
    bool bold = false, italic = false, underline = false;
    std::string color;
    TextStyle style() const { return {bold, italic, underline, color}; }
    bool operator==(const StyleSpan&) const = default;
};
// Plain text (the multi-line rule: LF only) plus the styled runs of it. Only styled text is listed.
// The framework never parses a markup language. See normalize_rich_text for what a valid value is.
struct RichText {
    std::u16string text;
    std::vector<StyleSpan> spans;
    bool operator==(const RichText&) const = default;
};
// A rich value in its one canonical form: spans sorted by start, zero-length spans and spans of plain
// style removed, adjacent spans of equal style merged, so equal content always reads back as an equal
// value. Spans that overlap, run past the text or start/end inside a surrogate pair, and text that is not
// valid multi-line text within max_length, give invalid_value.
Query<RichText> normalize_rich_text(RichText value, std::size_t max_length = default_max_text_length);
// An edit of a style: unset members are left as they are, a set member replaces it.
// color == "" returns the text to the normal color.
struct StyleChange {
    std::optional<bool> bold, italic, underline;
    std::optional<std::string> color;
    bool operator==(const StyleChange&) const = default;
};
enum class StyleAttribute { bold, italic, underline };
// A color a rich field can use by name, one value per theme: dark for ThemeVariant::dark(), light
// for every other variant.
struct NamedColor {
    std::string name;
    Color light, dark;
    bool operator==(const NamedColor&) const = default;
};
// Public operations a component currently admits; restricted ones are omitted, not just flagged.
enum class Operation { focus, activate, select, copy, edit, cut, paste, undo, redo, scroll, style };
struct OptionValue { std::string id; bool operator==(const OptionValue&) const = default; };
// One option of a radio group, a dropdown or a list: its stable value, the text the user sees, and whether it can be chosen.
struct OptionItem { OptionValue value; std::u16string label; bool enabled = true; };
struct SliderRange { int minimum, maximum, step; };
struct OptionSnapshot { OptionValue value; std::u16string label; bool enabled = true, selected = false, invalid = false; };
// Semantic snapshot from a handle or a host-side adapter.
struct ComponentSnapshot {
    std::string id;
    ComponentKind kind = ComponentKind::label;
    std::u16string name, value, description, error;
    bool enabled = true, read_only = false, focusable = false, focused = false, invalid = false, executing = false, source_conflict = false;
    bool visible = false; // intersects the clip in the latest arranged frame
    bool multiline = false;     // text fields: created multi-line
    bool rich = false;          // text fields: created with ComponentConfig::rich
    unsigned visible_lines = 0; // multi-line text fields: configured height in lines
    unsigned scroll_line = 0;   // multi-line text fields: index of the first visible display line
    unsigned line_count = 0;    // multi-line text fields: display lines in the latest arranged frame, 0 before one
    TextAlign text_align = TextAlign::left;  // labels and text fields; the defaults elsewhere. The property as set, even while the user's "justified text as left" setting shows it left-aligned
    JustifyMethod justify_method = JustifyMethod::automatic;
    double line_spacing = 1.0;
    int paragraph_spacing = 0;
    std::vector<OptionSnapshot> items;  // every option of a radio group, dropdown or list, every page of tabs or a navigation list, in order
    std::optional<SliderRange> slider_range;
    std::optional<int> numeric_value;
    std::optional<bool> checked;  // present for a checkbox
    bool expanded = false;
    std::optional<OptionValue> candidate;
    std::string target;
    LabelKind label_kind = LabelKind::plain;
    std::vector<std::string> labels;  // labels whose target is this component, in declaration order
    std::vector<Operation> operations;
    WritingMode writing_mode = WritingMode::horizontal;  // labels only
    int vertical_digits = max_vertical_digits;           // labels only
};
// Platform-neutral keys. Hosts translate native events; Ctrl shortcuts carry their letter.
// menu is the application/menu key (VK_APPS); Shift+F10 arrives as f10 with shift; alt is a lone Alt press.
enum class Key { other, tab, enter, space, escape, left, right, up, down, home, end, backspace, del, page_up, page_down, menu, f10, alt };
struct KeyPress {
    Key key = Key::other;
    bool shift = false, ctrl = false, repeat = false;
    char16_t letter = 0;  // uppercase ASCII letter for Ctrl shortcuts, e.g. u'Z'
};
struct Shortcut { Key key = Key::other; char16_t letter = 0; bool ctrl = false, shift = false; };
struct Selection { unsigned anchor = 0, caret = 0; };
// A half-open range of UTF-16 offsets into a field's text.
struct TextSpan { unsigned begin = 0, end = 0; bool operator==(const TextSpan&) const = default; };
// How a text field decides where words are, for double click, Ctrl+arrow and UIA word units. Every mode
// gives each punctuation mark its own word and keeps a run of spaces together. Set per field at any time.
enum class WordSegmentation {
    dictionary,     // the system ICU dictionary (zh_TW); falls back to per_character when it is unavailable
    per_character,  // every Chinese or Japanese character is a word; a run of Latin letters and digits is one word
    script_run      // a run of Chinese or Japanese characters is one word, a run of Latin letters and digits another
};
// Host-provided system clipboard. Core never touches OS services directly.
class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual bool write(std::u16string_view text) = 0;
    // not_supported means no text; failed means the clipboard could not be read.
    virtual Query<std::u16string> read() = 0;
    // Rich text. A copy from a rich field hands the plain text (CRLF) together with an opaque payload that
    // carries the styles; the host stores the payload under one private format beside the plain text and
    // returns it unchanged. A host that does not know the format keeps these defaults, so only plain text
    // moves. A plain write replaces every format, as on Windows, so a stale payload never outlives its text.
    virtual bool write_rich(std::u16string_view text, std::string_view payload) { (void)payload; return write(text); }
    // not_supported means no payload is on the clipboard.
    virtual Query<std::string> read_rich() { return {Status::not_supported, u"No rich text", std::nullopt}; }
};
// Pasted CRLF counts as one line break, lone CR and LF as one each; every break becomes a space.
std::u16string paste_as_single_line(std::u16string_view text);
// Multi-line fields keep breaks: CRLF, a lone CR and a lone LF each become one LF.
std::u16string paste_as_multi_line(std::u16string_view text);
// One session owns text and transient composition. Offsets are UTF-16.
// The text backend supplies navigation stops; this is not a Unicode segmenter.
class EditSession {
public:
    std::u16string text;
    std::uint64_t revision = 0; // committed content changes, independent of layout and composition
    Selection selection;
    bool composing = false;
    std::uint64_t selection_changes = 0;  // counts every select(); a temporary typing style ends when it moves
    std::u16string preedit;
    unsigned preedit_caret = 0;
    // Multi-line fields: the caret sits at the end of a wrapped line, not the start of the next one.
    // Only the End key and a click on a line's end set it; every other selection change clears it.
    bool caret_upstream = false;
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
// As valid_single_line, but LF is allowed (one UTF-16 unit each) and CR still is not.
bool valid_multi_line(std::u16string_view text, std::size_t max_length = default_max_text_length);
// A read source: the half of a data connection through which a component reads the application's current value and
// hears that it changed. The application owns the model. Notifications run on the UI thread; the source outlives the
// connection. Its subscription returns a disconnection function. One template told apart by value type (#95), so a
// source of one value type never converts to another: Source<std::u16string> (text fields), Source<RichText> (text
// fields created with ComponentConfig::rich), Source<bool> (checkboxes), Source<int> (sliders, progress displays) and
// Source<std::optional<OptionValue>> (radio groups, dropdowns, lists). Write one as `{read, subscribe}`.
// Integer sources (Source<int>) also feed read-only progress displays, which accept values outside 0-100.
// Integer slider values are the minimum plus whole steps, with the maximum also valid.
// In-range edits snap to the closest valid value; ties choose the higher value.
// Programmatic values outside the range are rejected; pointer and keyboard input clamp.
// Arrow keys move to the adjacent valid value, including the shorter final interval; Home and End
// select the exact endpoints.
// The maximum is reachable even when the range is not divisible by the step. Source values
// must already be valid; an invalid initial value rejects connection and later invalid notifications
// leave the last valid value in place and expose an error.
template <class T> struct Source {
    std::function<T()> read;
    std::function<std::function<void()>(std::function<void()>)> subscribe;
};
class Ui;
class SemanticTree;
struct Frame;
namespace detail { class UiState; UiState& access(Ui&); const UiState& access(const Ui&); }
// What every typed handle holds (#94): the Ui it belongs to, the lifetime that says whether what it names still exists
// (the whole Ui's, a button's own, or a tooltip's source component's), and the id of what it names (the component; for
// a context menu the component it is attached to, for a tooltip its source, for a dialog the dialog). Each handle kind
// derives from its own HandleBase privately, so only the kind itself and the framework see inside: no user of a handle,
// and no class derived from one, reaches the Ui, the lifetime or the id, and two different kinds never compare.
// Equality: two handles are equal when they control the same thing now (same Ui, same lifetime, same id); two default
// handles are equal to each other and to no real one. Every kind declares `bool operator==(const Kind&) const = default;`.
// Every kind takes HandleBase's constructor (`using HandleBase::HandleBase;`): it stays as private as the base's, so only
// the framework makes real handles. Each kind still befriends UiState (and Frame) itself: reaching the base of a
// privately derived handle needs friendship of the kind, not of the base.
namespace detail {
template <class Handle> class HandleBase {
    friend Handle;
    friend class UiState;
    friend struct ::lcars::Frame;
    HandleBase() = default;
    HandleBase(UiState* ui, std::weak_ptr<void> lifetime, std::string id) : ui_(ui), lifetime_(std::move(lifetime)), id_(std::move(id)) {}
    bool operator==(const HandleBase& other) const {
        return ui_ == other.ui_ && id_ == other.id_ && !lifetime_.owner_before(other.lifetime_) && !other.lifetime_.owner_before(lifetime_);
    }
    UiState* ui_ = nullptr;
    std::weak_ptr<void> lifetime_;
    std::string id_;
};
}
class TextHandle;
class CheckboxHandle;
class RadioGroupHandle;
class SliderHandle;
class DropdownHandle;
class ListHandle;
namespace detail {
// The value type a field kind's change record carries when Change names only the field kind (void: the kind has none).
template <class Field> struct ChangeValue { using type = void; };
template <> struct ChangeValue<TextHandle> { using type = std::u16string; };
template <> struct ChangeValue<CheckboxHandle> { using type = bool; };
template <> struct ChangeValue<RadioGroupHandle> { using type = std::optional<OptionValue>; };
template <> struct ChangeValue<SliderHandle> { using type = int; };
template <> struct ChangeValue<DropdownHandle> { using type = std::optional<OptionValue>; };
template <> struct ChangeValue<ListHandle> { using type = std::optional<OptionValue>; };
// The combinations the framework sends: each field kind with its own value type, and a rich text field's RichText.
template <class Field, class Value>
inline constexpr bool sends_change = (!std::is_void_v<Value> && std::is_same_v<Value, typename ChangeValue<Field>::type>) ||
                                     (std::is_same_v<Field, TextHandle> && std::is_same_v<Value, RichText>);
}
// A change record: what a write hands the application's change entry. `field` is the field it came from, `value` the new
// value, `origin` the value the draft started from (the source value when the draft began). One template told apart by
// field kind and value type (#95): a radio group's and a dropdown's records share a value type, and a confirmation group
// must still say which kind of field each came from. The value type defaults to the field kind's own:
// Change<TextHandle> (std::u16string), Change<CheckboxHandle> (bool), Change<RadioGroupHandle>, Change<DropdownHandle>
// and Change<ListHandle> (std::optional<OptionValue>), Change<SliderHandle> (int); a rich text field's is spelled
// Change<TextHandle, RichText>. A list keeps no draft (#99): its origin is the selection its source held just before the
// write, and it writes immediately only, so Change<ListHandle> is in neither FieldChange nor a confirmation group.
// Only these seven exist; any other combination (Change<ButtonHandle>, Change<SliderHandle, bool>) does not compile.
template <class Field, class Value = typename detail::ChangeValue<Field>::type>
    requires detail::sends_change<Field, Value>
struct Change {
    Field field;
    Value value, origin;
};
// A handle identifies a component without owning it. Each operation validates its lifetime.
class TextHandle : private detail::HandleBase<TextHandle> {
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
    // Multi-line fields only; any other field answers not_supported. The scroll position is the index of
    // the first visible display line (a wrapped piece counts as a line), so it keeps its meaning when the
    // text size or density changes. A position past the last page is invalid_value, as for the semantic scroll.
    Result set_visible_lines(unsigned lines) const;  // owner configuration: at least 1; reflows at once
    Query<unsigned> visible_lines() const;
    Result set_scroll_line(unsigned first_visible_line) const;
    Query<unsigned> scroll_line() const;
    // Owner configuration (#80), live: the next frame reflows. Out-of-range values are invalid_value and
    // leave the field unchanged. Horizontal alignment, line spacing and paragraph spacing; the field's
    // caption, description and error text always stay left-aligned.
    // A field refuses TextAlign::distribute with invalid_value (it is for labels). justify is accepted: the
    // last line of each paragraph stays left-aligned, so a single-line field looks left-aligned.
    Result set_text_align(TextAlign align) const;
    Query<TextAlign> text_align() const;
    Result set_justify_method(JustifyMethod method) const;  // #84; live, like the alignment
    Query<JustifyMethod> justify_method() const;
    Result set_line_spacing(double multiple) const;
    Query<double> line_spacing() const;
    Result set_paragraph_spacing(int dip) const;
    Query<int> paragraph_spacing() const;
    // Words and paragraphs (#83). A word is what the field's segmentation says it is; a paragraph is the text
    // between line feeds (the whole text of a single-line field). Offsets are UTF-16 and name the character that
    // starts there; one at or past the end of a paragraph means "after its last character". These work like
    // select(): they need an available field, are refused while an input method composes, and ignore read-only.
    Result set_word_segmentation(WordSegmentation mode) const;  // owner configuration; takes effect at once
    Query<WordSegmentation> word_segmentation() const;          // the mode as set
    Query<WordSegmentation> word_segmentation_in_use() const;   // the mode that segments the text now (dictionary may fall back)
    Query<TextSpan> word_at(unsigned offset) const;             // what a double click there selects
    Query<TextSpan> paragraph_at(unsigned offset) const;        // what a triple click there selects
    Query<unsigned> next_word_stop(unsigned offset) const;      // where Ctrl+Right goes from offset
    Query<unsigned> previous_word_stop(unsigned offset) const;  // where Ctrl+Left goes from offset
    Result select_word(unsigned offset) const;
    Result select_paragraph(unsigned offset) const;
    // Like a double or triple click with the pointer there: the same selection, and a drag that follows (see
    // Frame::select_to) grows it by words or paragraphs until the next selection change.
    Result double_click(unsigned offset) const;
    Result triple_click(unsigned offset) const;
    Result connect_source(Source<std::u16string> source) const;
    Result connect_immediate(std::function<Result(const Change<TextHandle>&)> write) const;
    Result connect_confirmed(std::function<Result(const Change<TextHandle>&)> write) const;
    // Rich fields (ComponentConfig::rich) connect with the rich value type; the plain connect_* calls above answer
    // not_supported on a rich field, and these answer not_supported on any other field. Confirmation
    // groups take rich fields through Ui::connect_confirmation (a Change<TextHandle, RichText> per rich field).
    Result connect_rich_source(Source<RichText> source) const;
    Result connect_rich_immediate(std::function<Result(const Change<TextHandle, RichText>&)> write) const;
    Result connect_rich_confirmed(std::function<Result(const Change<TextHandle, RichText>&)> write) const;
    // Rich fields only; any other field answers not_supported. The same admission as every edit (a
    // read-only, disabled, composing or unconnected field refuses with its usual status). set_rich_value
    // normalizes (normalize_rich_text) and is one undo entry; set_value on a rich field replaces the text
    // and leaves it unstyled. draft() is the plain text.
    Result set_rich_value(RichText value) const;
    Query<RichText> rich_value() const;
    // Range of the committed text, begin <= end <= size, else invalid_value. Set members of the change
    // replace the style of every character of the range (bold=false removes bold, color="" the color);
    // one call is one undo entry.
    Result set_style(unsigned begin, unsigned end, StyleChange change) const;
    // What Ctrl+B / Ctrl+I / Ctrl+U do: with a selection, apply the attribute to all of it unless all of
    // it already has it, then remove it; with a caret only, flip it in the typing style.
    Result toggle_style(StyleAttribute attribute) const;
    // set_style on the selection; with a caret only it changes the typing style (the style of the next typed text).
    Result apply_style(StyleChange change) const;
    // The style the next typed or pasted-as-plain text takes: the temporary setting if there is one (it ends
    // when the caret moves or text is inserted), else the previous character's (the next character's at the
    // start of a paragraph).
    Query<TextStyle> typing_style() const;
    Result set_typing_style(TextStyle style) const;
    // The named colors this field draws with; replaced as a whole and repainted at once. Names must be
    // non-empty and distinct. Text whose color name is not in the list is drawn in the normal text color.
    Result set_colors(std::vector<NamedColor> colors) const;
    Query<std::vector<NamedColor>> colors() const;
    Result confirm() const;
    Query<bool> source_conflict() const;
    Result use_latest() const;
    Query<ComponentSnapshot> describe() const;
    Query<std::u16string> draft() const;
    bool operator==(const TextHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class ButtonHandle : private detail::HandleBase<ButtonHandle> {
public:
    ButtonHandle() = default;
    Result invoke() const;
    Result focus() const;
    Result bind(std::function<Result()> action) const;
    Result enable(bool enabled) const;
    // Owner configuration: permanently remove this button; existing handles become invalid.
    Result remove() const;
    Query<bool> executing() const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const ButtonHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class LabelHandle : private detail::HandleBase<LabelHandle> {
public:
    LabelHandle() = default;
    // The label's text, read again every time the label is drawn (it keeps no copy), so keep the read cheap. Two forms:
    // a full read source, whose notifications ask for a repaint (text changed by background work: a clock, a download),
    // or a read function alone, for text that only changes through what happens on screen (another field's draft).
    // Connecting again, in either form, replaces the connection and cancels the previous subscription. A source without
    // read or subscribe, or an empty function, is refused with invalid_value and keeps the current connection.
    // A bare lambda, {read, subscribe}, {{}, {}} and a named Source each pick one form. Braces around a single lambda,
    // connect_source({[] { ... }}), match both and do not compile (#99): drop the braces. (Braces around a variable that
    // already is a std::function<std::u16string()> pick the read-function form, as the variable matches it exactly.)
    Result connect_source(Source<std::u16string> source) const;
    Result connect_source(std::function<std::u16string()> read) const;
    // Owner configuration (#80); same rules as the text field's.
    Result set_text_align(TextAlign align) const;
    Query<TextAlign> text_align() const;
    Result set_justify_method(JustifyMethod method) const;
    Query<JustifyMethod> justify_method() const;
    Result set_line_spacing(double multiple) const;
    Query<double> line_spacing() const;
    Result set_paragraph_spacing(int dip) const;
    Query<int> paragraph_spacing() const;
    // Vertical writing: the text is re-arranged at once. The digit limit is 0-3 (invalid_value otherwise).
    Result set_writing_mode(WritingMode mode) const;
    Query<WritingMode> writing_mode() const;
    Result set_vertical_digits(int digits) const;
    Query<int> vertical_digits() const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const LabelHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class CheckboxHandle : private detail::HandleBase<CheckboxHandle> {
public:
    CheckboxHandle() = default;
    Result toggle() const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(Source<bool> source) const;
    Result connect_immediate(std::function<Result(const Change<CheckboxHandle>&)> write) const;
    Result connect_confirmed(std::function<Result(const Change<CheckboxHandle>&)> write) const;
    Result confirm() const;
    Query<bool> draft() const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const CheckboxHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class RadioGroupHandle : private detail::HandleBase<RadioGroupHandle> {
public:
    RadioGroupHandle() = default;
    Result select(OptionValue value) const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result enable_option(OptionValue value, bool enabled) const;
    Result connect_source(Source<std::optional<OptionValue>> source) const;
    Result connect_immediate(std::function<Result(const Change<RadioGroupHandle>&)> write) const;
    Result connect_confirmed(std::function<Result(const Change<RadioGroupHandle>&)> write) const;
    Result confirm() const;
    Query<std::optional<OptionValue>> draft() const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const RadioGroupHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class SliderHandle : private detail::HandleBase<SliderHandle> {
public:
    SliderHandle() = default;
    Result on_blur(std::function<void()> callback) const;
    Result set_value(int value) const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(Source<int> source) const;
    Result connect_immediate(std::function<Result(const Change<SliderHandle>&)> write) const;
    Result connect_confirmed(std::function<Result(const Change<SliderHandle>&)> write) const;
    Result confirm() const;
    Query<int> draft() const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const SliderHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
// A display-only percentage. Source values are never written; display is clamped to 0-100.
class ProgressHandle : private detail::HandleBase<ProgressHandle> {
public:
    ProgressHandle() = default;
    Result connect_source(Source<int> source) const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const ProgressHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class DropdownHandle : private detail::HandleBase<DropdownHandle> {
public:
    DropdownHandle() = default;
    Result open() const;
    Result cancel() const;
    Result select(OptionValue value) const;
    Result set_items(std::vector<OptionItem> items) const;
    Result use_latest() const;
    Result focus() const;
    Result enable(bool enabled) const;
    Result connect_source(Source<std::optional<OptionValue>> source) const;
    Result connect_immediate(std::function<Result(const Change<DropdownHandle>&)> write) const;
    Result connect_confirmed(std::function<Result(const Change<DropdownHandle>&)> write) const;
    Result confirm() const;
    Query<std::optional<OptionValue>> draft() const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const DropdownHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class ListHandle;
class TabsHandle;
class NavigationListHandle;
using ComponentHandle = std::variant<std::monostate, TextHandle, ButtonHandle, LabelHandle, CheckboxHandle, RadioGroupHandle, SliderHandle, DropdownHandle, ListHandle, TabsHandle, ProgressHandle, NavigationListHandle>;
class TabsHandle : private detail::HandleBase<TabsHandle> {
public:
    TabsHandle() = default;
    Result activate(OptionValue page) const;
    Result enable_page(OptionValue page, bool enabled) const;
    // Owner configuration: attach an existing component to a page.
    Result add_member(OptionValue page, ComponentHandle member) const;
    Result focus() const;
    Query<OptionValue> active() const;
    Query<ComponentSnapshot> describe() const;
    Result on_page_change(std::function<void(OptionValue, OptionValue)> callback) const;
    bool operator==(const TabsHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class NavigationListHandle : private detail::HandleBase<NavigationListHandle> {
public:
    NavigationListHandle() = default;
    Result select(OptionValue page) const;
    Result enable_page(OptionValue page, bool enabled) const;
    // Owner configuration: attach an existing component to a page.
    Result add_member(OptionValue page, ComponentHandle member) const;
    Result focus() const;
    Query<OptionValue> selected() const;
    Query<ComponentSnapshot> describe() const;
    Result on_page_change(std::function<void(OptionValue, OptionValue)> callback) const;
    bool operator==(const NavigationListHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
class ListHandle : private detail::HandleBase<ListHandle> {
public:
    ListHandle() = default;
    Result select(OptionValue value) const;
    Result set_items(std::vector<OptionItem> items) const;
    Result focus() const;
    Result focus(OptionValue value) const;
    Result enable(bool enabled) const;
    Result connect_source(Source<std::optional<OptionValue>> source) const;
    // The write runs when the user or the code selects another item: `value` is that item, `origin` the item the source
    // held just before. When set_items drops the selected item (leaves it out, or keeps it disabled) it runs with no value
    // and that item as origin.
    // Selecting the selected item writes nothing; a rejected write leaves the selection and returns the result as is.
    // Connect the source first. A list has no confirmed write: it keeps no draft (#99).
    Result connect_immediate(std::function<Result(const Change<ListHandle>&)> write) const;
    Query<ComponentSnapshot> describe() const;
    bool operator==(const ListHandle& other) const = default;
private:
    friend class detail::UiState;
    friend struct Frame;
    using HandleBase::HandleBase;
};
// A single window-modal dialog, assembled from ordinary typed components. The opener and
// fallback are non-owning handles; the result callback runs once for each successful opening.
class DialogHandle : private detail::HandleBase<DialogHandle> {
public:
    DialogHandle() = default;
    Result open(ComponentHandle opener) const;
    Result open() const;
    Result settle(bool confirmed) const;
    Query<bool> is_open() const;
    bool operator==(const DialogHandle& other) const = default;
private:
    friend class detail::UiState;
    using HandleBase::HandleBase;
};
// Non-owning reference to explanatory content attached to a source component.
class TooltipHandle : private detail::HandleBase<TooltipHandle> {
public:
    TooltipHandle() = default;
    Query<bool> visible() const;
    bool operator==(const TooltipHandle& other) const = default;
private:
    friend class detail::UiState;
    using HandleBase::HandleBase;
};
// Page membership is explicit, using component handles. Page identities remain stable on reorder.
struct TabPage { OptionValue value; std::u16string label; bool enabled = true; std::vector<ComponentHandle> members; };
using NavigationPage = TabPage;
using ConfirmField = std::variant<TextHandle, CheckboxHandle, RadioGroupHandle, SliderHandle, DropdownHandle>;
// One change record per field of a confirmation group; a rich text field's is Change<TextHandle, RichText>.
using FieldChange = std::variant<Change<TextHandle>, Change<CheckboxHandle>, Change<RadioGroupHandle>, Change<SliderHandle>,
                                 Change<DropdownHandle>, Change<TextHandle, RichText>>;
struct FieldError { ConfirmField field; std::u16string message; };
struct ConfirmationResult { Result result; std::vector<FieldError> errors; };
// ---- Context menu. A short list of commands opened beside a component.
struct MenuContext;
// A separator line, or a command: id, text, availability, an optional quick-key letter, an optional
// shortcut hint (display only; it registers no shortcut) and the code to run when chosen.
// Ids beginning with "lcars." are reserved for the framework's built-in items.
struct MenuItem {
    std::string id;
    std::u16string label;
    bool enabled = true;
    char16_t access_key = 0;
    std::u16string shortcut_hint;
    std::function<Result(const MenuContext&)> action;
    bool separator = false;
    static MenuItem divider() { MenuItem item; item.separator = true; return item; }
};
// What the application's code learns about the opening: the component, and for a list the row.
struct MenuContext { ComponentHandle target; std::optional<OptionValue> item; };
// How an attached menu relates to a component's built-in items (only text fields have any).
enum class MenuMode { append, replace, off };
struct MenuPosition { int x = 0, y = 0; };  // window client pixels, as in Frame
struct MenuItemSnapshot {
    std::string id;
    std::u16string label;
    bool enabled = true, separator = false, highlighted = false;
    char16_t access_key = 0;
    std::u16string shortcut_hint;
};
struct MenuSnapshot { ComponentHandle target; std::optional<OptionValue> item; std::vector<MenuItemSnapshot> items; };
// Built-in text field items, by id. A rich text field (ComponentConfig::rich) adds bold, italic and underline, which run
// the same function as Ctrl+B / Ctrl+I / Ctrl+U.
inline constexpr const char* menu_undo = "lcars.undo";
inline constexpr const char* menu_redo = "lcars.redo";
inline constexpr const char* menu_cut = "lcars.cut";
inline constexpr const char* menu_copy = "lcars.copy";
inline constexpr const char* menu_paste = "lcars.paste";
inline constexpr const char* menu_delete = "lcars.delete";
inline constexpr const char* menu_select_all = "lcars.select_all";
inline constexpr const char* menu_bold = "lcars.bold";
inline constexpr const char* menu_italic = "lcars.italic";
inline constexpr const char* menu_underline = "lcars.underline";
// The application's items attached to one component. Built-in items are not part of it.
class ContextMenuHandle : private detail::HandleBase<ContextMenuHandle> {
public:
    ContextMenuHandle() = default;
    Result set_mode(MenuMode mode) const;
    Result set_items(std::vector<MenuItem> items) const;
    Result add_item(MenuItem item, std::string before = {}) const;  // before: id of the item to precede; empty appends
    Result remove_item(std::string id) const;
    Result update_item(MenuItem item) const;  // replaces the item with the same id
    Result enable_item(std::string id, bool enabled) const;
    Query<std::vector<MenuItemSnapshot>> items() const;
    bool operator==(const ContextMenuHandle& other) const = default;
private:
    friend class detail::UiState;
    using HandleBase::HandleBase;
};
// What the pointer hovers: a component, plus the item when the pointer is over one item of it.
struct HoverTarget { std::string id; std::optional<OptionValue> item; bool operator==(const HoverTarget&) const = default; };
// Owns the component tree. Components are operated through the handles returned at creation.
class Ui {
public:
    Ui();
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    Query<TextHandle> add_text(ComponentConfig config);
    Query<ButtonHandle> add_button(ComponentConfig config);
    Query<LabelHandle> add_label(ComponentConfig config);
    Query<CheckboxHandle> add_checkbox(ComponentConfig config);
    Query<RadioGroupHandle> add_radio_group(ComponentConfig config, std::vector<OptionItem> items);
    // Invalid ranges and steps return invalid_value without creating a component.
    Query<SliderHandle> add_slider(ComponentConfig config, SliderRange range);
    Query<ProgressHandle> add_progress(ComponentConfig config);
    Query<DropdownHandle> add_dropdown(ComponentConfig config, std::vector<OptionItem> items);
    Query<TabsHandle> add_tabs(ComponentConfig config, std::vector<TabPage> pages);
    Query<NavigationListHandle> add_navigation_list(ComponentConfig config, std::vector<NavigationPage> pages, OptionValue initial);
    Query<ListHandle> add_list(ComponentConfig config, std::vector<OptionItem> items);
    // Delay is in milliseconds; the source remains the focus and lifetime owner.
    Query<TooltipHandle> add_tooltip(std::string id, ComponentHandle source, std::u16string text, unsigned delay_ms = 500);
    Query<DialogHandle> add_dialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial_focus,
                            ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name = {});
    Result add_shortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope = {});
    Result connect_confirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<ConfirmationResult(const std::vector<FieldChange>&)> submit);
    // Attach the application's menu to any component. Text fields have built-in items; with
    // MenuMode::append (the default) the application's items follow them after a separator.
    // Other components have no menu until one is attached. A component carries one attachment.
    Query<ContextMenuHandle> add_context_menu(ComponentHandle target, std::vector<MenuItem> items = {}, MenuMode mode = MenuMode::append);
    // Open the one window-wide menu for a component: at `at`, or at the keyboard position (a text
    // field's caret, a list's current row, otherwise the component). For a list `item` names the
    // row: it is selected first and handed to the application's code.
    Result open_context_menu(ComponentHandle target, std::optional<MenuPosition> at = {}, std::optional<OptionValue> item = {});
    Result close_context_menu();
    Query<bool> context_menu_open() const;
    Query<MenuSnapshot> context_menu() const;  // hidden while no menu is open
    Result invoke_menu_item(std::string id);
    void on_change(std::function<void()> callback);
    Result set_presentation(const PresentationSettings& settings);
    const PresentationSettings& presentation() const;
    // True while any on-screen transition (for example a slider thumb sliding to its new value) is
    // still running. Values and semantics never wait for it, so this only says the picture has not
    // settled; wait until it is false before comparing screenshots.
    Query<bool> animating() const;
    // Overrides individual palette colors of the built-in appearances for the whole Ui. The list
    // replaces the previous one and applies on the next frame; an empty list restores built-in
    // values. Unknown appearance, unsupported variant, unknown role, a value of the wrong kind,
    // scrim_alpha outside 0-255, a repeated (appearance, variant, role) or any contrast failure
    // (see check_palette_contrast, applied to every affected appearance and variant) returns
    // invalid_value and leaves the active list unchanged. Custom appearances are never affected.
    // The built-in appearance modules register their names when linked into the program.
    Result set_palette_overrides(std::vector<PaletteOverride> overrides);
    const std::vector<PaletteOverride>& palette_overrides() const;
    // The palette that paints for this appearance and variant: built-in values with overrides applied.
    Query<Palette> effective_palette(const Appearance& appearance, const ThemeVariant& variant) const;
    // Tells the Ui which appearance it is shown with so SemanticTree::effective_palette can report
    // it. Desktop hosts bind and unbind their appearance themselves; core-only hosts call this.
    // The Appearance is borrowed and must outlive the binding (nullptr unbinds).
    void bind_appearance(const Appearance* appearance);
    // The component (and item, for lists, tabs, radio groups, navigation lists and dropdowns) the
    // pointer hovers, from the real mouse or from simulate_hover. The value is empty when nothing is
    // hovered, or the target was disabled or is blocked by a modal dialog. It answers the same with
    // the presentation hover switch off: the switch only hides the overlay.
    Query<std::optional<HoverTarget>> hover() const;
    // Acts as if the real mouse rested on the target: same overlay, same tooltip delay, same exclusions
    // (disabled, blocked by a modal dialog, an unknown item). The next real mouse move takes over.
    // busy while a mouse drag holds the hover.
    Result simulate_hover(const ComponentHandle& target, std::optional<OptionValue> item = {});
    Result clear_simulated_hover();

    // Whether focus indicators are being drawn now: the latest real input was a key, the system asks for
    // keyboard cues, or the presentation forces them. Editable text fields are drawn focused regardless.
    bool focus_indicator_visible() const;
    const std::vector<std::string>& diagnostics() const;
    SemanticTree& semantics();
private:
    friend detail::UiState& detail::access(Ui&);
    friend const detail::UiState& detail::access(const Ui&);
    std::unique_ptr<detail::UiState> state_;
    std::unique_ptr<SemanticTree> semantics_;
};
}
