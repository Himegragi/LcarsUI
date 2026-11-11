#include "justify.hpp"
#include <algorithm>
#include <string>

namespace lcars::detail {
namespace {
char32_t code_point_at(std::u16string_view text, std::size_t i) {
    const char16_t first = text[i];
    if (first >= 0xd800 && first <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff)
        return 0x10000 + ((static_cast<char32_t>(first) - 0xd800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xdc00);
    return first;
}
char32_t code_point_before(std::u16string_view text, std::size_t i) {
    if (i >= 2 && text[i - 1] >= 0xdc00 && text[i - 1] <= 0xdfff && text[i - 2] >= 0xd800 && text[i - 2] <= 0xdbff) return code_point_at(text, i - 2);
    return text[i - 1];
}
// Characters that continue the cluster before them: combining marks, variation selectors, a zero-width
// joiner, emoji skin tones and tag characters. HarfBuzz keeps them in one cluster already; a measuring
// backend that stops anywhere else must still never get a cut here.
bool continues_cluster(char32_t c) {
    return (c >= 0x0300 && c <= 0x036f) || (c >= 0x1ab0 && c <= 0x1aff) || (c >= 0x1dc0 && c <= 0x1dff) || (c >= 0x20d0 && c <= 0x20ff) ||
           (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xfe20 && c <= 0xfe2f) || c == 0x200c || c == 0x200d || c == 0x3099 || c == 0x309a ||
           (c >= 0x1f3fb && c <= 0x1f3ff) || (c >= 0xe0020 && c <= 0xe007f) || (c >= 0xe0100 && c <= 0xe01ef);
}
bool regional_indicator(char32_t c) { return c >= 0x1f1e6 && c <= 0x1f1ff; }
// May a line be cut at this UTF-16 offset without splitting a character?
bool safe_cut(std::u16string_view text, std::size_t offset) {
    if (offset == 0 || offset >= text.size()) return true;
    const char32_t before = code_point_before(text, offset), after = code_point_at(text, offset);
    if (text[offset] >= 0xdc00 && text[offset] <= 0xdfff && text[offset - 1] >= 0xd800 && text[offset - 1] <= 0xdbff) return false;  // inside a surrogate pair
    if (continues_cluster(after) || before == 0x200d) return false;
    if (regional_indicator(before) && regional_indicator(after)) {  // flags are pairs: cut only between two of them
        std::size_t run = 0;
        for (std::size_t i = offset; i > 0;) {
            const bool pair = i >= 2 && text[i - 1] >= 0xdc00 && text[i - 1] <= 0xdfff;
            const char32_t c = code_point_before(text, i);
            if (!regional_indicator(c)) break;
            ++run;
            i -= pair ? 2 : 1;
        }
        return run % 2 == 0;
    }
    return true;
}
bool is_space(char32_t c) { return c == u' '; }
// The line shaped once for letters that will be drawn apart: optional ligatures off, kerning on (TextMeasure::measure_apart).
// Rich lines are measured run by run in each run's own face and joined, as the layout does for the natural run.
TextRun measure_line_apart(TextMeasure& measure, std::u16string_view text, int pixels, const std::vector<FieldLine::Piece>* styles) {
    if (!styles || styles->empty()) return measure.measure_apart(text, pixels, FontStyle{});
    TextRun joined;
    joined.stops.push_back({0, 0.f});
    float x = 0;
    for (const auto& run : *styles) {
        if (run.end <= run.begin || run.end > text.size()) continue;
        const auto part = measure.measure_apart(text.substr(run.begin, run.end - run.begin), pixels, FontStyle{run.style.bold, run.style.italic});
        for (const auto& [offset, position] : part.stops)
            if (offset > 0) joined.stops.push_back({run.begin + offset, x + position});
        x += part.width;
    }
    joined.width = x;
    return joined;
}
// A blank in the sense of the automatic method: an ASCII space. It belongs to the character before it, and the added width
// goes after it, so a blank is never drawn as a piece of its own. The ideographic space (U+3000) is NOT one: it is a CJK
// character with a gap on each side, like any ideograph (clreq; the X04 readers accept its ink-less draw).
bool is_blank(char32_t c) { return c == u' '; }
}
bool is_wide_character(char32_t c) {
    return (c >= 0x1100 && c <= 0x11ff) || (c >= 0x2e80 && c <= 0x303f) || (c >= 0x3040 && c <= 0x33ff) || (c >= 0x3400 && c <= 0x4dbf) ||
           (c >= 0x4e00 && c <= 0x9fff) || (c >= 0xa960 && c <= 0xa97f) || (c >= 0xac00 && c <= 0xd7ff) || (c >= 0xf900 && c <= 0xfaff) ||
           (c >= 0xfe10 && c <= 0xfe1f) || (c >= 0xfe30 && c <= 0xfe6f) || (c >= 0xff00 && c <= 0xffef) || (c >= 0x1b000 && c <= 0x1b16f) ||
           (c >= 0x20000 && c <= 0x3ffff);
}
std::optional<Justified> justify_line(TextMeasure& measure, std::u16string_view text, int pixels, const TextRun& natural, float width, JustifyMethod method,
                                      const std::vector<FieldLine::Piece>* styles) {
    std::size_t content_end = text.size();
    while (content_end > 0 && is_blank(text[content_end - 1])) --content_end;  // trailing spaces never stretch
    if (content_end == 0) return std::nullopt;
    // Whole characters: the measuring backend's own stops (its shaping clusters), minus any that would split a
    // character. A per-character line takes them from a run shaped with ligatures off (an ffi ligature is one cluster
    // of the natural run, but its letters must be separate pieces); that run keeps the font's kerning, so the added
    // space lands on top of it.
    const TextRun apart = method == JustifyMethod::per_character ? measure_line_apart(measure, text, pixels, styles) : TextRun{};
    const TextRun& shaped = method == JustifyMethod::per_character ? apart : natural;
    std::vector<unsigned> cuts;
    for (const auto& [offset, x] : shaped.stops) {
        (void)x;
        if (offset < text.size() && safe_cut(text, offset) && (cuts.empty() || offset > cuts.back())) cuts.push_back(offset);
    }
    if (cuts.empty() || cuts.front() != 0) cuts.insert(cuts.begin(), 0);
    cuts.push_back(static_cast<unsigned>(text.size()));
    // Where extra width may go: between two characters, both inside the content.
    std::vector<unsigned> gaps;
    for (std::size_t j = 1; j + 1 < cuts.size(); ++j) {
        if (cuts[j] >= content_end) break;
        const char32_t a = code_point_at(text, cuts[j - 1]), b = code_point_at(text, cuts[j]);
        bool gap = false;
        switch (method) {
        case JustifyMethod::automatic: gap = is_blank(a) || (!is_blank(b) && (is_wide_character(a) || is_wide_character(b))); break;
        case JustifyMethod::spaces: gap = is_space(a); break;
        case JustifyMethod::per_character: gap = true; break;
        }
        if (gap) gaps.push_back(cuts[j]);
    }
    if (gaps.empty()) return std::nullopt;
    // The pieces: cut at every gap, once more before trailing spaces so they do not count, and (rich text) at every
    // style boundary; only the gaps receive extra width.
    std::vector<unsigned> pieces = gaps;
    if (content_end < text.size()) pieces.push_back(static_cast<unsigned>(content_end));
    if (styles)
        for (const auto& run : *styles)
            if (run.begin > 0 && run.begin < text.size()) pieces.push_back(run.begin);
    pieces.push_back(0);
    pieces.push_back(static_cast<unsigned>(text.size()));
    std::sort(pieces.begin(), pieces.end());
    pieces.erase(std::unique(pieces.begin(), pieces.end()), pieces.end());
    auto is_gap = [&](unsigned offset) { return std::binary_search(gaps.begin(), gaps.end(), offset); };
    // Piece widths come from the one measured run (kerning included): the natural run, or the ligature-free one for per_character.
    float content = 0;
    std::vector<float> widths;
    for (std::size_t k = 0; k + 1 < pieces.size(); ++k) {
        widths.push_back(shaped.x(pieces[k + 1]) - shaped.x(pieces[k]));
        if (pieces[k + 1] <= content_end) content += widths.back();
    }
    // Stop a hair short of the width: a piece drawn at a whole-pixel x (rounded down) with a rounded-up advance must still lie inside the box.
    const float extra = (width - 0.02f - content) / static_cast<float>(gaps.size());
    if (!(extra > 0.01f)) return std::nullopt;
    Justified out;
    float x = 0;
    for (std::size_t k = 0; k + 1 < pieces.size(); ++k) {
        out.segments.push_back({pieces[k], pieces[k + 1], x, widths[k]});
        x += widths[k];
        if (is_gap(pieces[k + 1])) x += extra;  // the other cuts (trailing blanks, style boundaries) add nothing
    }
    out.run.width = x;
    // Stops: inside a piece they follow the measured run; a cut belongs to the piece that starts there, so a
    // caret at a gap is drawn at the start of the next character, after the added width.
    for (std::size_t k = 0; k < out.segments.size(); ++k) {
        const auto& segment = out.segments[k];
        if (!out.run.stops.empty() && out.run.stops.back().first == segment.begin) out.run.stops.pop_back();
        out.run.stops.push_back({segment.begin, segment.x});
        const float origin = shaped.x(segment.begin);
        for (const auto& [offset, position] : shaped.stops)
            if (offset > segment.begin && offset <= segment.end) out.run.stops.push_back({offset, segment.x + position - origin});
    }
    return out;
}
}
