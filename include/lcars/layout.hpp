#pragma once
// B01-B03 minimal layout: column/row, grid and one scroll region, measured through a host
// text backend. Arrangement is pure: the same inputs give the same frame, which the host both
// draws and hit-tests, so measurement, painting and pointer targets cannot disagree.
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
};
class TextMeasure {
public:
    virtual ~TextMeasure() = default;
    virtual TextRun measure(std::u16string_view text, int pixels) = 0;
    virtual int line_height(int pixels) = 0;
};
// Breaks at '\n' and before the first character that would overflow width (CJK-friendly).
std::vector<std::u16string> wrap(TextMeasure& measure, std::u16string_view text, int pixels, int width);

namespace layout {
struct Node {
    enum class Type { control, column, row, grid, scroll } type = Type::column;
    Id id;               // control
    int text_size = 0;   // control: label size override in DIP, 0 = appearance default
    int gap = 0;         // DIP between children
    int padding = 0;     // DIP around children
    int min_column = 0;  // grid: as many columns as fit at this width, at least one
    std::vector<Node> children;
};
Node control(Id id, int text_size = 0);
Node column(std::vector<Node> children, int gap = 12, int padding = 0);
Node row(std::vector<Node> children, int gap = 12);  // buttons keep natural width, others share the rest
Node grid(std::vector<Node> children, int min_column, int gap = 16);
Node scroll(Node child);  // S1 supports one scroll region per frame
}

struct Line { Rect bounds; std::u16string text; };
struct Placement {
    Id id;
    Kind kind = Kind::label;
    Rect bounds, clip;            // physical pixels after scrolling; hits need bounds and clip
    int text_size = 0;            // physical pixels
    std::vector<Line> lines;      // labels and button titles
    FieldLayout field;            // text fields, translated to frame coordinates
    std::vector<Line> label_lines, description_lines, error_lines;
    TextRun run;                  // text fields: visible text (composition included)
    float offset = 0;             // text fields: horizontal display offset in pixels
};
struct ScrollState { int offset = 0; };  // physical pixels, clamped by every arrange
struct Frame {
    std::vector<Placement> items;
    Rect viewport, track, thumb;  // scroll region and its bar; thumb empty when nothing scrolls
    int extent = 0, maximum = 0, offset = 0;
    const Placement* find(std::string_view id) const;
    const Placement* hit(int x, int y) const;        // topmost focusable-or-label item under the point
    unsigned text_offset(std::string_view id, int x) const;  // field caret offset for a pointer x
    int offset_for_thumb(int thumb_top) const;       // scroll offset when the thumb top is dragged here
};
// reveal: when not empty, scroll so that control is fully visible (top-aligned if taller).
// Updates each focused field's display offset in ui so its caret stays visible.
Frame arrange(const layout::Node& root, Ui& ui, const Appearance& appearance, TextMeasure& measure,
              int width, int height, float scale, ScrollState& scroll, std::string_view reveal = {});
}
