#pragma once
// Vertical writing for labels (display only): classification of characters, flow into columns.
// Pure logic over the measuring backend: no fonts, no drawing. Painting lives in text.cpp.
#include "lcars/layout.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace lcars::detail {
// UAX #50 Vertical_Orientation. Data: vertical_orientation_data.inc (see third-party notices).
enum class VerticalOrientation { U, R, Tu, Tr };
VerticalOrientation vertical_orientation(char32_t code_point);

struct VerticalSegment { unsigned begin = 0, end = 0; VerticalForm form = VerticalForm::upright; };
// Splits text (no line breaks) into runs of one form. max_digits is the longest half-width digit run
// that is combined into one cell (0 turns combining off); one digit stays upright, longer runs rotate.
std::vector<VerticalSegment> segment_vertical(std::u16string_view text, int max_digits);

// The single mapping point for the paragraph properties of vertical text (#80): the label's
// text_align becomes the alignment along each column (left/center/right -> top/center/bottom), its
// line_spacing the multiple of the column pitch and its paragraph_spacing (DIP) the gap between the last
// column of one paragraph and the first of the next. Defaults (left, 1.0, 0) = top, 1.0, 0.
// `scale` turns DIP into physical pixels, as detail::paragraph_gap does for horizontal text.
struct VerticalParams {
    enum class Align { top, center, bottom } align = Align::top;  // along the column
    double line_spacing = 1.0;                                    // multiplies the column pitch
    int paragraph_spacing = 0;                                    // pixels between paragraphs
};
VerticalParams vertical_params(const ComponentConfig& control, float scale = 1.f);

// Flows text into columns of `height` pixels (0 = never wrap). Positions are relative to each column.
// `pitch` is the column width; columns get bounds {0, 0, pitch, length}. Paragraphs start at '\n'.
std::vector<VerticalColumn> flow_vertical(TextMeasure& measure, std::u16string_view text, int em,
                                          int max_digits, int height);
}
