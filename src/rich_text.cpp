#include "rich_text.hpp"
#include "utf.hpp"
#include <algorithm>

namespace isoduo {
namespace {
bool lowSurrogate(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
bool highSurrogate(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
StyleSpan spanOf(unsigned start, unsigned end, const TextStyle& style) {
    return {start, end - start, style.bold, style.italic, style.underline, style.color};
}
}
Query<RichText> normalizeRichText(RichText value, std::size_t maxLength) {
    if (!validMultiLine(value.text, maxLength)) return {Status::invalidValue, u"The text is not valid multi-line text within the limit", std::nullopt};
    auto spans = std::move(value.spans);
    std::stable_sort(spans.begin(), spans.end(), [](const StyleSpan& a, const StyleSpan& b) { return a.start < b.start; });
    const auto size = static_cast<unsigned long long>(value.text.size());
    unsigned long long previousEnd = 0;
    for (const auto& span : spans) {
        const unsigned long long end = static_cast<unsigned long long>(span.start) + span.length;
        if (span.start > size || end > size) return {Status::invalidValue, u"A style span runs past the text", std::nullopt};
        if (span.start < previousEnd) return {Status::invalidValue, u"Style spans overlap", std::nullopt};
        previousEnd = end;
        for (unsigned long long edge : {static_cast<unsigned long long>(span.start), end})
            if (edge > 0 && edge < size && lowSurrogate(value.text[edge]) && highSurrogate(value.text[edge - 1]))
                return {Status::invalidValue, u"A style span splits a surrogate pair", std::nullopt};
    }
    RichText out{std::move(value.text), {}};
    out.spans = detail::canonicalSpans(std::move(spans));
    return {Status::ok, {}, std::move(out)};
}
namespace detail {
std::vector<StyleSpan> canonicalSpans(std::vector<StyleSpan> spans) {
    std::vector<StyleSpan> out;
    for (auto& span : spans) {
        if (span.length == 0 || span.style().plain()) continue;
        if (!out.empty() && out.back().start + out.back().length == span.start && out.back().style() == span.style()) out.back().length += span.length;
        else out.push_back(std::move(span));
    }
    return out;
}
TextStyle styleAt(const std::vector<StyleSpan>& spans, unsigned pos) {
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
std::vector<StyleSpan> applyStyleChange(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, const StyleChange& change) {
    if (begin >= end) return spans;
    // A cover of [0, end_of_everything) in runs of one style, split at begin and end, then restyled between them.
    std::vector<StyleSpan> out;
    auto emit = [&](unsigned from, unsigned to, const TextStyle& style) {
        if (to <= from) return;
        const unsigned a = std::max(from, begin), b = std::min(to, end);
        if (from < a) out.push_back(spanOf(from, std::min(a, to), style));
        if (a < b) out.push_back(spanOf(a, b, changed(style, change)));
        if (to > end) out.push_back(spanOf(std::max(end, from), to, style));
    };
    unsigned cursor = 0;
    for (const auto& span : spans) {
        emit(cursor, span.start, {});
        emit(span.start, span.start + span.length, span.style());
        cursor = span.start + span.length;
    }
    emit(cursor, std::max(cursor, end), {});
    return canonicalSpans(std::move(out));
}
std::vector<StyleSpan> uniformSpans(unsigned length, const TextStyle& style) {
    if (length == 0 || style.plain()) return {};
    return {spanOf(0, length, style)};
}
std::vector<StyleSpan> spliceSpans(const std::vector<StyleSpan>& spans, unsigned pos, unsigned removed, unsigned inserted,
                                    const std::vector<StyleSpan>& insertedSpans) {
    std::vector<StyleSpan> out;
    const unsigned tail = pos + removed;
    auto push = [&](unsigned from, unsigned to, const TextStyle& style, long long shift) {
        if (to > from) out.push_back(spanOf(static_cast<unsigned>(from + shift), static_cast<unsigned>(to + shift), style));
    };
    for (const auto& span : spans) {  // before the replaced range
        const unsigned end = span.start + span.length;
        if (span.start < pos) push(span.start, std::min(end, pos), span.style(), 0);
    }
    for (const auto& span : insertedSpans)
        if (span.start < inserted) push(span.start, std::min(span.start + span.length, inserted), span.style(), pos);
    const long long shift = static_cast<long long>(inserted) - static_cast<long long>(removed);
    for (const auto& span : spans) {  // after it
        const unsigned end = span.start + span.length;
        if (end > tail) push(std::max(span.start, tail), end, span.style(), shift);
    }
    return canonicalSpans(std::move(out));
}
std::vector<StyleSpan> sliceSpans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end) {
    std::vector<StyleSpan> out;
    for (const auto& span : spans) {
        const unsigned from = std::max(span.start, begin), to = std::min(span.start + span.length, end);
        if (from < to) out.push_back(spanOf(from - begin, to - begin, span.style()));
    }
    return canonicalSpans(std::move(out));
}
bool allHave(const std::vector<StyleSpan>& spans, std::u16string_view text, unsigned begin, unsigned end, StyleAttribute attribute) {
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
std::vector<StyleSpan> compositionSpans(const std::vector<StyleSpan>& spans, unsigned begin, unsigned end, unsigned preeditSize, const TextStyle& style) {
    return spliceSpans(spans, begin, end - begin, preeditSize, uniformSpans(preeditSize, style));
}
std::string serializeRichPayload(const RichText& value) {
    const auto text = utf8FromUtf16(value.text);
    std::string out = "Isoduo.RichText.1\n" + std::to_string(text.size()) + "\n" + text + "\n" + std::to_string(value.spans.size()) + "\n";
    for (const auto& span : value.spans) {
        const unsigned flags = (span.bold ? 1u : 0u) | (span.italic ? 2u : 0u) | (span.underline ? 4u : 0u);
        out += std::to_string(span.start) + " " + std::to_string(span.length) + " " + std::to_string(flags) + " " + std::to_string(span.color.size()) + ":" + span.color + "\n";
    }
    return out;
}
namespace {
// A reader over the payload that fails closed: any malformed number or short read ends the parse.
struct Reader {
    std::string_view rest;
    bool failed = false;
    bool expect(std::string_view literal) { if (rest.substr(0, literal.size()) != literal) return failed = true, false; rest.remove_prefix(literal.size()); return true; }
    unsigned long number(char stop) {
        unsigned long long value = 0; std::size_t used = 0;
        while (used < rest.size() && rest[used] >= '0' && rest[used] <= '9' && used < 10) value = value * 10 + static_cast<unsigned>(rest[used++] - '0');
        if (used == 0 || used >= rest.size() || rest[used] != stop || value > 0xffffffffull) { failed = true; return 0; }
        rest.remove_prefix(used + 1);
        return static_cast<unsigned long>(value);
    }
    std::string_view bytes(std::size_t count) {
        if (count > rest.size()) { failed = true; return {}; }
        auto out = rest.substr(0, count); rest.remove_prefix(count); return out;
    }
};
}
std::optional<RichText> parseRichPayload(std::string_view payload) {
    try {
        Reader in{payload};
        // Accept the former header only on input; all copies use the canonical header.
        if (payload.starts_with("LcarsUI.RichText.1\n")) in.rest.remove_prefix(std::string_view("LcarsUI.RichText.1\n").size());
        else if (!in.expect("Isoduo.RichText.1\n")) return std::nullopt;
        const auto textSize = in.number('\n');
        if (in.failed) return std::nullopt;
        RichText value;
        value.text = utf16FromUtf8(in.bytes(textSize));
        if (in.failed || !in.expect("\n")) return std::nullopt;
        const auto count = in.number('\n');
        if (in.failed || count > payload.size()) return std::nullopt;
        for (unsigned long i = 0; i < count; ++i) {
            StyleSpan span;
            span.start = static_cast<unsigned>(in.number(' '));
            span.length = static_cast<unsigned>(in.number(' '));
            const auto flags = in.number(' ');
            const auto colorSize = in.number(':');
            if (in.failed || flags > 7) return std::nullopt;
            span.bold = flags & 1; span.italic = flags & 2; span.underline = flags & 4;
            span.color = std::string(in.bytes(colorSize));
            if (in.failed || !in.expect("\n")) return std::nullopt;
            value.spans.push_back(std::move(span));
        }
        if (!in.rest.empty()) return std::nullopt;
        auto normal = normalizeRichText(std::move(value), static_cast<std::size_t>(-1));
        return normal ? std::optional<RichText>(std::move(*normal)) : std::nullopt;
    } catch (const std::exception&) { return std::nullopt; }
}
}
}
