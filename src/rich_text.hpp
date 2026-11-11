#pragma once
// Internal helpers for rich text: the span arithmetic the field editing, layout and semantic layer share.
// Span lists passed in are canonical (sorted, non-overlapping, no plain or empty spans); results are too.
#include "lcars/core.hpp"

namespace lcars::detail {
// Sorted, non-overlapping input: drops empty and plain spans and merges adjacent spans of equal style.
std::vector<StyleSpan> canonical_spans(std::vector<StyleSpan> spans);
// The style of the UTF-16 unit at pos (plain past the end).
TextStyle style_at(const std::vector<StyleSpan>& spans, unsigned pos);
// Applies a style change to [begin, end).
std::vector<StyleSpan> apply_style_change(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, const StyleChange& change);
// Text [pos, pos + removed) is replaced by `inserted` units described by inserted_spans (offsets relative to
// the insertion); the spans after the removed range move with their text.
std::vector<StyleSpan> splice_spans(const std::vector<StyleSpan>& spans, unsigned pos, unsigned removed, unsigned inserted,
                                    const std::vector<StyleSpan>& inserted_spans);
// A span list covering `length` units from `at` with one style (empty for the plain style).
std::vector<StyleSpan> uniform_spans(unsigned length, const TextStyle& style);
// The spans of [begin, end) with offsets relative to begin.
std::vector<StyleSpan> slice_spans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end);
// True when every unit of [begin, end) that is not a line feed has the attribute (and the range has such a unit).
bool all_have(const std::vector<StyleSpan>& spans, std::u16string_view text, unsigned begin, unsigned end, StyleAttribute attribute);
// Applies a StyleChange to one style.
TextStyle changed(TextStyle style, const StyleChange& change);
// The spans of the text as displayed while an IME composition replaces [begin, end) with preedit_size units of `style`.
std::vector<StyleSpan> composition_spans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, unsigned preedit_size, const TextStyle& style);
}
