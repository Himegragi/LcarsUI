#pragma once
// Vertical writing for labels (display only): classification of characters, flow into columns.
// Pure logic over the measuring backend: no fonts, no drawing. Painting lives in text.cpp.
#include "isoduo/layout.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace isoduo::detail {
// UAX #50 Vertical_Orientation. Data: vertical_orientation_data.inc (see third-party notices).
enum class VerticalOrientation { U, R, Tu, Tr };
VerticalOrientation verticalOrientation(char32_t codePoint);

struct VerticalSegment { unsigned begin = 0, end = 0; VerticalForm form = VerticalForm::upright; };
// Splits text (no line breaks) into runs of one form. maxDigits is the longest half-width digit run
// that is combined into one cell (0 turns combining off); one digit stays upright, longer runs rotate.
std::vector<VerticalSegment> segmentVertical(std::u16string_view text, int maxDigits);

// The single mapping point for the paragraph properties of vertical text (#80): the label's
// textAlign becomes the alignment along each column (left/center/right -> top/center/bottom), its
// lineSpacing the multiple of the column pitch and its paragraphSpacing (DIP) the gap between the last
// column of one paragraph and the first of the next. Defaults (left, 1.0, 0) = top, 1.0, 0.
// `scale` turns DIP into physical pixels, as detail::paragraphGap does for horizontal text.
struct VerticalParams {
    enum class Align { top, center, bottom } align = Align::top;  // along the column
    double lineSpacing = 1.0;                                    // multiplies the column pitch
    int paragraphSpacing = 0;                                    // pixels between paragraphs
};
VerticalParams verticalParams(const ComponentConfig& control, float scale = 1.f);

// Flows text into columns of `height` pixels (0 = never wrap). Positions are relative to each column.
// `pitch` is the column width; columns get bounds {0, 0, pitch, length}. Paragraphs start at '\n'.
std::vector<VerticalColumn> flowVertical(TextMeasure& measure, std::u16string_view text, int em,
                                          int maxDigits, int height);
}
