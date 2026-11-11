#pragma once
// Justified and distributed lines (#84). One display line is cut into pieces at the places where extra
// width may go; every piece is drawn on its own at its own x, so ligatures and kerning never span a
// cut. The caret stops are rebuilt from the pieces, so drawing, caret, hit testing, selection and the
// input method all read the same stretched positions.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace lcars::detail {
struct Justified {
    std::vector<TextSegment> segments;  // pieces in order, together the whole line
    TextRun run;                        // caret stops of the stretched line; width is the stretched content width
};
// Stretches one display line to `width` physical pixels. `natural` is the run measure() gave the whole line.
// Returns nothing when the line cannot or need not stretch: no place to add width (the line stays
// left-aligned), the content already fills the width, or the line is only spaces. Trailing spaces never
// count towards the width and never receive any extra.
// `styles` (rich fields, #81): the line's runs of one style, relative to the line start. The line is also cut
// at every run boundary (a cut that gets no extra width), so each piece lies inside exactly one run and can be
// drawn in that run's face; `natural` must already continue across the runs, each measured in its own face.
std::optional<Justified> justify_line(TextMeasure& measure, std::u16string_view text, int pixels,
                                      const TextRun& natural, float width, JustifyMethod method,
                                      const std::vector<FieldLine::Piece>* styles = nullptr);
// The character classes the automatic method uses. The code point is the first of a cluster.
bool is_wide_character(char32_t code_point);  // CJK ideographs, kana, hangul, bopomofo, full-width forms and CJK punctuation
}
