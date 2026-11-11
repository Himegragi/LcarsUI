#include "rich_text.hpp"
#include <algorithm>

namespace lcars {
namespace {
bool low_surrogate(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
bool high_surrogate(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
StyleSpan span_of(unsigned start, unsigned end, const TextStyle& style) {
    return {start, end - start, style.bold, style.italic, style.underline, style.color};
}
}
Query<RichText> normalize_rich_text(RichText value, std::size_t max_length) {
    if (!valid_multi_line(value.text, max_length)) return {Status::invalid_value, u"The text is not valid multi-line text within the limit", std::nullopt};
    auto spans = std::move(value.spans);
    std::stable_sort(spans.begin(), spans.end(), [](const StyleSpan& a, const StyleSpan& b) { return a.start < b.start; });
    const auto size = static_cast<unsigned long long>(value.text.size());
    unsigned long long previous_end = 0;
    for (const auto& span : spans) {
        const unsigned long long end = static_cast<unsigned long long>(span.start) + span.length;
        if (span.start > size || end > size) return {Status::invalid_value, u"A style span runs past the text", std::nullopt};
        if (span.start < previous_end) return {Status::invalid_value, u"Style spans overlap", std::nullopt};
        previous_end = end;
        for (unsigned long long edge : {static_cast<unsigned long long>(span.start), end})
            if (edge > 0 && edge < size && low_surrogate(value.text[edge]) && high_surrogate(value.text[edge - 1]))
                return {Status::invalid_value, u"A style span splits a surrogate pair", std::nullopt};
    }
    RichText out{std::move(value.text), {}};
    out.spans = detail::canonical_spans(std::move(spans));
    return {Status::ok, {}, std::move(out)};
}
namespace detail {
std::vector<StyleSpan> canonical_spans(std::vector<StyleSpan> spans) {
    std::vector<StyleSpan> out;
    for (auto& span : spans) {
        if (span.length == 0 || span.style().plain()) continue;
        if (!out.empty() && out.back().start + out.back().length == span.start && out.back().style() == span.style()) out.back().length += span.length;
        else out.push_back(std::move(span));
    }
    return out;
}
TextStyle style_at(const std::vector<StyleSpan>& spans, unsigned pos) {
    for (const auto& span : spans)
        if (pos >= span.start && pos - span.start < span.length) return span.style();
    return {};
}
TextStyle changed(TextStyle style, const StyleChange& change) {
    if (change.bold) style.bold = *change.bold;
    if (change.italic) style.italic = *change.italic;
    if (change.underline) style.underline = *change.underline;
    if (change.color) style.color = *change.color;
    return style;
}
std::vector<StyleSpan> apply_style_change(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, const StyleChange& change) {
    if (begin >= end) return spans;
    // A cover of [0, end_of_everything) in runs of one style, split at begin and end, then restyled between them.
    std::vector<StyleSpan> out;
    auto emit = [&](unsigned from, unsigned to, const TextStyle& style) {
        if (to <= from) return;
        const unsigned a = std::max(from, begin), b = std::min(to, end);
        if (from < a) out.push_back(span_of(from, std::min(a, to), style));
        if (a < b) out.push_back(span_of(a, b, changed(style, change)));
        if (to > end) out.push_back(span_of(std::max(end, from), to, style));
    };
    unsigned cursor = 0;
    for (const auto& span : spans) {
        emit(cursor, span.start, {});
        emit(span.start, span.start + span.length, span.style());
        cursor = span.start + span.length;
    }
    emit(cursor, std::max(cursor, end), {});
    return canonical_spans(std::move(out));
}
std::vector<StyleSpan> uniform_spans(unsigned length, const TextStyle& style) {
    if (length == 0 || style.plain()) return {};
    return {span_of(0, length, style)};
}
std::vector<StyleSpan> splice_spans(const std::vector<StyleSpan>& spans, unsigned pos, unsigned removed, unsigned inserted,
                                    const std::vector<StyleSpan>& inserted_spans) {
    std::vector<StyleSpan> out;
    const unsigned tail = pos + removed;
    auto push = [&](unsigned from, unsigned to, const TextStyle& style, long long shift) {
        if (to > from) out.push_back(span_of(static_cast<unsigned>(from + shift), static_cast<unsigned>(to + shift), style));
    };
    for (const auto& span : spans) {  // before the replaced range
        const unsigned end = span.start + span.length;
        if (span.start < pos) push(span.start, std::min(end, pos), span.style(), 0);
    }
    for (const auto& span : inserted_spans)
        if (span.start < inserted) push(span.start, std::min(span.start + span.length, inserted), span.style(), pos);
    const long long shift = static_cast<long long>(inserted) - static_cast<long long>(removed);
    for (const auto& span : spans) {  // after it
        const unsigned end = span.start + span.length;
        if (end > tail) push(std::max(span.start, tail), end, span.style(), shift);
    }
    return canonical_spans(std::move(out));
}
std::vector<StyleSpan> slice_spans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end) {
    std::vector<StyleSpan> out;
    for (const auto& span : spans) {
        const unsigned from = std::max(span.start, begin), to = std::min(span.start + span.length, end);
        if (from < to) out.push_back(span_of(from - begin, to - begin, span.style()));
    }
    return canonical_spans(std::move(out));
}
bool all_have(const std::vector<StyleSpan>& spans, std::u16string_view text, unsigned begin, unsigned end, StyleAttribute attribute) {
    end = std::min<unsigned>(end, static_cast<unsigned>(text.size()));
    auto has = [&](const StyleSpan& s) { return attribute == StyleAttribute::bold ? s.bold : attribute == StyleAttribute::italic ? s.italic : s.underline; };
    unsigned counted = 0;
    for (unsigned i = begin; i < end; ++i) {
        if (text[i] == u'\n') continue;
        ++counted;
        auto it = std::find_if(spans.begin(), spans.end(), [&](const StyleSpan& s) { return i >= s.start && i - s.start < s.length; });
        if (it == spans.end() || !has(*it)) return false;
    }
    return counted > 0;
}
std::vector<StyleSpan> composition_spans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, unsigned preedit_size, const TextStyle& style) {
    return splice_spans(spans, begin, end - begin, preedit_size, uniform_spans(preedit_size, style));
}
}
}
