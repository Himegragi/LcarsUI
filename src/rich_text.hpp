#pragma once
// Internal helpers for rich text: the span arithmetic the field editing, layout and semantic layer share.
// Span lists passed in are canonical (sorted, non-overlapping, no plain or empty spans); results are too.
#include "isoduo/core.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace isoduo::detail {
// Sorted, non-overlapping input: drops empty and plain spans and merges adjacent spans of equal style.
std::vector<StyleSpan> canonicalSpans(std::vector<StyleSpan> spans);
// The style of the UTF-16 unit at pos (plain past the end).
TextStyle styleAt(const std::vector<StyleSpan>& spans, unsigned pos);
// Applies a style change to [begin, end).
std::vector<StyleSpan> applyStyleChange(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, const StyleChange& change);
// Text [pos, pos + removed) is replaced by `inserted` units described by insertedSpans (offsets relative to
// the insertion); the spans after the removed range move with their text.
std::vector<StyleSpan> spliceSpans(const std::vector<StyleSpan>& spans, unsigned pos, unsigned removed, unsigned inserted,
                                    const std::vector<StyleSpan>& insertedSpans);
// A span list covering `length` units from `at` with one style (empty for the plain style).
std::vector<StyleSpan> uniformSpans(unsigned length, const TextStyle& style);
// The spans of [begin, end) with offsets relative to begin.
std::vector<StyleSpan> sliceSpans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end);
// True when every unit of [begin, end) that is not a line feed has the attribute (and the range has such a unit).
bool allHave(const std::vector<StyleSpan>& spans, std::u16string_view text, unsigned begin, unsigned end, StyleAttribute attribute);
// Applies a StyleChange to one style.
TextStyle changed(TextStyle style, const StyleChange& change);
// The spans of the text as displayed while an IME composition replaces [begin, end) with preeditSize units of `style`.
std::vector<StyleSpan> compositionSpans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, unsigned preeditSize, const TextStyle& style);
}

namespace isoduo::detail {
// The framework-private clipboard payload of a copied range: the text, its spans and a version tag, as bytes.
// A host stores the bytes under one private format next to the plain text and returns them unchanged.
std::string serializeRichPayload(const RichText& value);
// nullopt for anything that is not a payload of this version or that does not describe a valid value.
std::optional<RichText> parseRichPayload(std::string_view payload);
}
