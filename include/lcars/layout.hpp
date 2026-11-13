#pragma once
// B01-B03 minimal layout: column/row, grid and nested scroll regions, measured through a host
// text backend. The same inputs give the same frame, which the host both draws and hit-tests,
// so measurement, painting and pointer targets cannot disagree. arrange settles view state as it
// goes: it clamps the scroll offset and each field's display offset before placing anything.
#include "lcars/appearance.hpp"
#include "lcars/core.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcars {
// Caret stops for one shaped line, in physical pixels. Drawing and hit-testing share it.
struct TextRun {
    std::vector<std::pair<unsigned, float>> stops;  // UTF-16 offset -> x, ascending, starts {0, 0}
    float width = 0;
    float x(unsigned offset) const;    // x of the last stop at or before offset
    unsigned hit(float x) const;       // nearest stop
    unsigned cell(float x) const;      // the last stop at or before x: the character whose cell holds x
};
// The weight and slant of a run of text; they change advances (a bold face is wider). Color and underline never do.
struct FontStyle {
    bool bold = false, italic = false;
    bool operator==(const FontStyle&) const = default;
};
class TextMeasure {
public:
    virtual ~TextMeasure() = default;
    virtual TextRun measure(std::u16string_view text, int pixels) = 0;
    // The same for bold and/or italic text. A backend that cannot tell the styles apart keeps the default.
    virtual TextRun measure(std::u16string_view text, int pixels, FontStyle style) { (void)style; return measure(text, pixels); }
    // The same line shaped for letters that will be drawn apart (per-character justification): optional ligatures
    // (liga, clig) are off so every character is its own cluster, kerning and the font's spacing stay on. A backend
    // without ligatures keeps the default.
    virtual TextRun measure_apart(std::u16string_view text, int pixels, FontStyle style) { return measure(text, pixels, style); }
    virtual int line_height(int pixels) = 0;
};
// Breaks at '\n' and before the first character that would overflow width (CJK-friendly).
std::vector<std::u16string> wrap(TextMeasure& measure, std::u16string_view text, int pixels, int width);

// A wrapped piece of text; soft means the next piece continues the same paragraph (no LF between them).
struct WrappedLine { std::u16string text; bool soft = false; };
std::vector<WrappedLine> wrap_marked(TextMeasure& measure, std::u16string_view text, int pixels, int width);

// A justified or distributed line is drawn in pieces, each at its own x: offsets are UTF-16 offsets
// relative to the start of the line, x is physical pixels from the line's start (the start the line's
// alignment gives it: the left edge of the box or text area). A line drawn as one piece has none (#84).
struct TextSegment { unsigned begin = 0, end = 0; float x = 0, width = 0; };  // width: the piece's own advance (no added gap)
// One display line of a multi-line field: a hard line (it ends at an LF) or one wrapped piece of it.
// Offsets are UTF-16 offsets into the visible text (an IME composition included).
struct FieldLine {
    unsigned begin = 0, end = 0;  // end excludes the LF that ends a hard line
    bool soft = false;            // wrapped: the next line continues it, and next.begin == end
    TextRun run;                  // caret stops for this line alone; x is 0 at the text's own left edge. A justified line's stops include the added width
    std::vector<TextSegment> segments;  // #84: a justified line's pieces (x from the shift); empty = draw the whole line in one go
    float shift = 0;             // where the text starts, from the text area's left edge: its alignment (#80)
    int top = 0;                  // top of the line's row from the first line's, physical pixels (#80)
    // Rich fields: the line cut into runs of one style, in order and covering [0, end - begin). Offsets are
    // relative to begin; x is each run's left edge from the TEXT AREA's left edge, the line's alignment shift
    // included (where the run is drawn); the run's stops continue across runs.
    struct Piece { unsigned begin = 0, end = 0; TextStyle style; float x = 0, width = 0; };
    std::vector<Piece> pieces;
};
// The settled multi-line geometry of one field. Core keeps a copy for the cursor keys, so drawing,
// hit testing, navigation and the IME all read the same lines.
struct FieldView {
    std::vector<FieldLine> lines;  // every display line, never empty once laid out
    unsigned first = 0;            // index of the first visible line: the scroll position
    unsigned visible = 0;          // configured height in lines
    int line_height = 0;           // physical pixels: the font's line height, what a glyph run occupies
    int row_height = 0;            // physical pixels: one line's row, line_height times the line spacing (#80)
    bool upstream = false;         // a caret on a wrap point is drawn at the end of the line above
    Rect text, track, thumb;       // frame coordinates; thumb is empty while everything fits
    // The line holding a caret offset; a wrap point belongs to the next line unless upstream.
    unsigned line_of(unsigned offset) const;
    // Nearest caret stop in a line for an x relative to the line start. The flag reports an offset
    // that is the end of a wrapped line, which must be drawn upstream to stay on that line.
    std::pair<unsigned, bool> hit_in_line(unsigned line, float x) const;
    // x relative to the text area's left edge, alignment shift included (hit_in_line takes the same).
    float x_in_line(unsigned line, unsigned offset) const;
    // The character whose cell holds an x relative to the text area's left edge (alignment shift included, like
    // hit_in_line): what a double or triple click selects. Right of a wrapped line it is that line's last character.
    unsigned cell_in_line(unsigned line, float x) const;
    // Frame y of the top of a line's row. A line outside the visible window is placed as if the lines
    // were laid out continuously (it is not drawn there).
    int row_y(unsigned line) const { return row_y(line, first); }
    int row_y(unsigned line, unsigned first_line) const;  // as if first_line were the first visible one
    // The visible line a frame y falls in: the last one whose row starts at or above y (the space after a
    // paragraph belongs to the row above); above the window it is the first, below it the last visible one.
    unsigned line_at(int y) const { return line_at(y, first); }
    unsigned line_at(int y, unsigned first_line) const;
};
// Wraps like wrap() and measures each resulting line, keeping UTF-16 offsets.
// With spans (a rich field's styled runs, offsets into text) every run is measured in its own style, so
// wrapping and caret stops follow the real widths of bold and italic text.
std::vector<FieldLine> wrap_lines(TextMeasure& measure, std::u16string_view text, int pixels, int width,
                                  const std::vector<StyleSpan>* spans = nullptr);

struct ScrollState {
    int offset = 0; // physical pixels, clamped by every arrange
    int text_size = 0;
    std::string anchor_id;
    std::string anchor_item;
};

namespace layout {
struct Node {
    enum class Type { control, column, row, grid, scroll, stack, pinned, modal } type = Type::column;
    ComponentHandle component;   // control: non-owning typed reference
    DialogHandle dialog;
    std::vector<DialogHandle> dialogs;
    int text_size = 0;   // control: label size override in DIP, 0 = appearance default
    int gap = 0;         // DIP between children
    int padding = 0;     // DIP around children
    int min_column = 0;  // grid: as many columns as fit at this width, at least one
    ScrollState* scroll_state = nullptr; // bounded nested viewport: caller keeps this state alive
    int scroll_height = 0; // viewport height in DIP; 0 fills the available space
    bool scrollable_text = false; // a single label may receive focus when it overflows
    int vertical_height = 0;      // control: height in DIP of a vertical-writing label; 0 = columns never wrap
    std::vector<Node> children;
};
Node control(ComponentHandle component, int text_size = 0);
Node column(std::vector<Node> children, int gap = 12, int padding = 0);
Node row(std::vector<Node> children, int gap = 12);  // buttons keep natural width, others share the rest
Node grid(std::vector<Node> children, int min_column, int gap = 16);
Node scroll(Node child);  // fills available space, using arrange's scroll state
Node scroll(Node child, ScrollState& state, int height); // bounded nested viewport in DIP
Node scroll_text(LabelHandle label, ScrollState& state, int height);
// A label whose height the application gives (DIP). While the label is in vertical writing mode its text
// fills columns of that height from right to left, and its width follows from the number of columns; when
// the columns are wider than the space offered, the label scrolls sideways by itself (initially showing
// the right end, where the text starts). In horizontal writing mode it lays out as an ordinary label.
Node vertical_text(LabelHandle label, int height);
Node pinned(Node header, Node content); // fixed header above a scroll region
Node stack(std::vector<Node> children); // later children are painted and hit above earlier children
struct DialogLayout { DialogHandle dialog; Node content; };
Node modal(Node background, std::vector<DialogLayout> dialogs);
Node modal(Node background, Node content, DialogHandle dialog);
}
using DialogLayout = layout::DialogLayout;

// bounds.x is where the (aligned) text starts. A justified or distributed label line spans the whole width and
// lists its pieces in segments (#84); every other line leaves segments empty.
struct Line { Rect bounds; std::u16string text; std::vector<TextSegment> segments = {}; };
// One row of the open context menu, positioned after scrolling; rows outside Placement::menu_inner are clipped.
struct MenuRow { std::string id; Rect bounds; std::u16string label, hint; bool separator = false, enabled = true, highlighted = false; };
// Vertical (top-to-bottom, columns right-to-left) text, shared by layout and painting. A piece is one
// unit placed on a column: a single character cell, a combined digit group, or a sideways word.
enum class VerticalForm {
    upright,      // CJK, full-width and emoji: one em cell, glyph stays upright
    sideways,     // Latin words, long digit runs: rotated 90 degrees clockwise, advance is the text width
    combined,     // 2-3 half-width digits side by side in one cell, squeezed to the cell width
    point,        // full-width comma, period, enumeration comma, semicolon, colon, ! and ?: centered, never the font's vertical alternate
    bracket,      // quotes and brackets: the font's vertical alternate, else rotated 90 degrees
    line,         // dashes and ellipses: rotated, centered in the cell on both axes
    middle_dot,   // middle dots: upright, centered in the cell
};
struct VerticalPiece { unsigned begin = 0, end = 0; VerticalForm form = VerticalForm::upright; int y = 0, advance = 0; };
struct VerticalColumn {
    Rect bounds;                      // the whole column, width = pitch
    unsigned begin = 0, end = 0;      // UTF-16 range of the display text on this column
    bool last_in_paragraph = false;   // the paragraph (text up to a line break) ends on this column
    std::vector<VerticalPiece> pieces;  // y is relative to bounds.y
};
struct VerticalLayout {
    std::vector<VerticalColumn> columns;  // reading order: the first column is the rightmost
    int em = 0, pitch = 0;                // text pixels and column width, including line spacing
    int extent = 0, maximum = 0, offset = 0;  // content width, scrollable distance, distance scrolled from the start (right)
    Rect track, thumb;                    // horizontal scrollbar when the content is wider than the label
};
struct Placement {
    std::string id;
    ComponentKind kind = ComponentKind::label;
    Rect bounds, clip;            // physical pixels after scrolling; hits need bounds and clip
    bool clipped = false;         // clip has been assigned, including an empty intersection
    bool popup = false;           // an overlay belongs to its source, outside normal layout flow
    bool dropdown_panel = false; // actual dropdown panel, distinct from modal content
    bool tooltip = false;         // explanatory overlay: never receives pointer input
    bool dialog = false;          // content of the open dialog: it sits on the dialog's panel, not on the page, so what a focus ring's inside is filled with (ComponentPaint::backdrop) is the panel color
    int text_size = 0;            // physical pixels
    int caption_size = 0;         // text fields: label, description and error text size
    std::vector<Line> lines;      // labels and command titles; vertical labels: one line per column
    bool vertical = false;        // labels in vertical writing mode: see `column_layout`
    VerticalLayout column_layout;
    std::vector<Rect> options;   // hit and paint bounds of each placed option of a radio group, a list or an open dropdown's panel (tabs and navigation lists: of each page)
    std::size_t option_start = 0; // logical index of the first placed option
    Rect slider_track;           // slider track in frame coordinates
    Rect progress_track, progress_fill; // progress display in frame coordinates
    FieldLayout field;            // text fields, translated to frame coordinates
    bool multiline = false;       // text fields: lines, caret and hits come from view, not run
    FieldView view;               // multi-line text fields
    std::vector<Line> label_lines, description_lines, error_lines;
    TextRun run;                  // text fields: visible text (composition included)
    float offset = 0;             // text fields: horizontal display offset in pixels
    float shift = 0;              // single-line fields: where text shorter than the room starts, by its alignment (#80)
    int text_top = 0;             // single-line fields: top of the glyphs inside the editor row (line spacing)
    int caret_x(unsigned offset) const;  // text fields: frame x of a caret, as painted and hit
    // The open context menu, an overlay painted and hit above every other; id is menu_node_id(target).
    bool menu = false;
    Rect menu_inner;              // rows' viewport inside the border
    std::vector<MenuRow> menu_rows;
    int menu_maximum = 0;         // scroll range in pixels; 0 when every row fits
    Rect menu_track, menu_thumb;  // scrollbar, empty when nothing scrolls
    int menu_pad = 0;             // horizontal text inset inside a row
    int caret_y(unsigned offset) const;  // multi-line fields: frame y of the top of the caret's line
    // Frame rectangle of the caret: one line tall, one pixel wide. In a multi-line field a caret scrolled
    // out of view is held on the nearest visible row, which is where an input method anchors its window.
    Rect caret_rect(unsigned offset) const;
};
struct ScrollRegion {
    ScrollState* state = nullptr;
    Rect viewport, clip, track, thumb;
    int extent = 0, maximum = 0, offset = 0;
    std::string field;           // a multi-line text field's own scrollbar; it moves by whole lines
    int step = 0;                // physical pixels per line when field is set
    bool modal_content = false;  // inside the open dialog, so it still takes wheel and drag input
    // A horizontal region is a vertical-writing label: its offset counts pixels scrolled from the start of
    // the text (the right edge), the bar lies along the bottom, and the thumb starts at the right end.
    bool horizontal = false;
    // The offset for a dragged thumb: its top (vertical regions) or its left edge (horizontal regions).
    int offset_for_thumb(int thumb_position) const;
};
struct Frame {
    std::vector<Placement> items;
    std::vector<ScrollRegion> regions; // outermost first, innermost last
    std::vector<Placement> overlays; // painted and hit above normal content and scrollbars
    bool modal = false;
    Rect modal_panel;
    Rect viewport, track, thumb;  // scroll region and its bar; thumb empty when nothing scrolls
    int extent = 0, maximum = 0, offset = 0;
    const Placement* find(const TextHandle& handle) const;
    const Placement* find(const ButtonHandle& handle) const;
    const Placement* find(const LabelHandle& handle) const;
    const Placement* find(const CheckboxHandle& handle) const;
    const Placement* find(const RadioGroupHandle& handle) const;
    const Placement* find(const SliderHandle& handle) const;
    const Placement* find(const ProgressHandle& handle) const;
    const Placement* find(const DropdownHandle& handle) const;
    const Placement* find(const TabsHandle& handle) const;
    const Placement* find(const NavigationListHandle& handle) const;
    const Placement* find(const ListHandle& handle) const;
    std::optional<std::size_t> option_at(const Placement& item, int x, int y) const;
    const Placement* hit(int x, int y) const;        // topmost focusable-or-label item under the point; never the context menu
    const Placement* menu_panel() const;             // the open context menu's overlay
    bool menu_contains(int x, int y) const;          // inside the open menu's panel
    std::optional<std::string> menu_item_at(int x, int y) const;  // id of the row under the point (separators included)
    // Apply an input-driven scroll to a region in this frame. Returns true only when it moved.
    bool scroll_to(Ui& ui, ScrollState& state, int offset) const;
    // Route a wheel step at window coordinates; delta is in physical pixels, positive upwards.
    // With sideways_only (Shift held), only sideways-scrolling labels take the step; a vertical wheel step down
    // on a vertical-writing label moves toward the end of the text.
    bool wheel(Ui& ui, int x, int y, int delta, bool sideways_only = false) const;
    // Route a horizontal wheel step (delta in physical pixels, positive to the right) to a sideways-scrolling label.
    bool hwheel(Ui& ui, int x, int y, int delta) const;
    unsigned text_offset(const TextHandle& handle, int x) const;  // field caret offset for a pointer x
    // Multi-line fields: caret offset for a pointer position. Above or below the lines it clamps to
    // the first or last visible line.
    unsigned text_offset(const TextHandle& handle, int x, int y) const;
    // Extend the field's selection to the pointer, as a drag does. Outside the top or bottom edge the
    // field also scrolls one line toward the pointer; hosts repeat this on a timer. True if anything changed.
    bool select_to(Ui& ui, const TextHandle& handle, int x, int y) const;
    const detail::UiState* owner_ = nullptr; // identity only; never dereferenced
    int offset_for_thumb(int thumb_top) const;       // scroll offset when the thumb top is dragged here
};
// reveal: when not empty, scroll so that control is fully visible (top-aligned if taller).
// Updates each focused field's display offset in ui so its caret stays visible.
Frame arrange(const layout::Node& root, Ui& ui, const Appearance& appearance, TextMeasure& measure,
              int width, int height, float scale, ScrollState& scroll, std::string_view reveal = {});
}
