#include "vertical.hpp"
#include "component_type.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace isoduo::detail {
namespace {
struct OrientationRange { char32_t first, last; VerticalOrientation value; };
constexpr OrientationRange orientationTable[] = {
#include "vertical_orientation_data.inc"
};

// Taiwan Ministry of Education rules (see the research note): point punctuation is centered.
bool isPoint(char32_t c) { return c == 0xff0c || c == 0x3002 || c == 0x3001 || c == 0xff1b || c == 0xff1a || c == 0xff01 || c == 0xff1f; }
bool isLine(char32_t c) { return c == 0x2014 || c == 0x2015 || c == 0x2e3a || c == 0x2e3b || c == 0x2500 || c == 0x2026 || c == 0x22ef; }
bool isMiddleDot(char32_t c) { return c == 0x00b7 || c == 0x2027 || c == 0x30fb; }
bool isAsciiDigit(char32_t c) { return c >= u'0' && c <= u'9'; }
bool isAsciiLetter(char32_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }
bool extends(char32_t c) {
    return (c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff) || (c >= 0x1dc0 && c <= 0x1dff) || (c >= 0x20d0 && c <= 0x20ff) ||
           (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xfe20 && c <= 0xfe2f) || (c >= 0xe0100 && c <= 0xe01ef) || (c >= 0x1f3fb && c <= 0x1f3ff) ||
           (c >= 0xe0020 && c <= 0xe007f) || c == 0x200c || c == 0x200d;
}
bool regionalIndicator(char32_t c) { return c >= 0x1f1e6 && c <= 0x1f1ff; }

struct Cluster { unsigned begin, end; char32_t first; bool uprightSequence; };
char32_t decode(std::u16string_view text, unsigned& i) {
    char32_t c = text[i++];
    if (c >= 0xd800 && c <= 0xdbff && i < text.size() && text[i] >= 0xdc00 && text[i] <= 0xdfff)
        c = 0x10000 + ((c - 0xd800) << 10) + (text[i++] - 0xdc00);
    return c;
}
// A cluster is what the reader sees as one character: a base with its marks, a ZWJ emoji sequence, a flag.
std::vector<Cluster> clusters(std::u16string_view text) {
    std::vector<Cluster> result;
    for (unsigned i = 0; i < text.size();) {
        Cluster cluster{i, 0, 0, false};
        cluster.first = decode(text, i);
        bool join = false;
        for (unsigned peek = i; peek < text.size();) {
            char32_t next = decode(text, peek);
            if (join || extends(next) || (regionalIndicator(cluster.first) && regionalIndicator(next) && i == cluster.begin + 2 && !cluster.uprightSequence)) {
                if (next == 0xfe0f || next == 0x20e3 || next == 0x200d || regionalIndicator(next)) cluster.uprightSequence = true;
                join = next == 0x200d;
                i = peek;
            } else break;
        }
        cluster.end = i;
        result.push_back(cluster);
    }
    return result;
}
}

VerticalOrientation verticalOrientation(char32_t codePoint) {
    auto it = std::upper_bound(std::begin(orientationTable), std::end(orientationTable), codePoint,
                               [](char32_t value, const OrientationRange& range) { return value < range.first; });
    if (it == std::begin(orientationTable)) return VerticalOrientation::R;
    --it;
    return codePoint <= it->last ? it->value : VerticalOrientation::R;
}

std::vector<VerticalSegment> segmentVertical(std::u16string_view text, int maxDigits) {
    auto parts = clusters(text);
    std::vector<VerticalSegment> result;
    auto add = [&](unsigned begin, unsigned end, VerticalForm form) {
        if (form == VerticalForm::sideways && !result.empty() && result.back().form == form && result.back().end == begin) result.back().end = end;
        else result.push_back({begin, end, form});
    };
    for (std::size_t i = 0; i < parts.size();) {
        const auto& part = parts[i];
        if (isAsciiDigit(part.first)) {
            std::size_t last = i;
            while (last + 1 < parts.size() && isAsciiDigit(parts[last + 1].first)) ++last;
            int count = static_cast<int>(last - i + 1);
            bool touchesLetter = (i > 0 && isAsciiLetter(parts[i - 1].first)) || (last + 1 < parts.size() && isAsciiLetter(parts[last + 1].first));
            unsigned begin = part.begin, end = parts[last].end;
            if (touchesLetter) add(begin, end, VerticalForm::sideways);
            else if (count == 1) add(begin, end, VerticalForm::upright);
            else if (count <= maxDigits) add(begin, end, VerticalForm::combined);
            else add(begin, end, VerticalForm::sideways);
            i = last + 1;
            continue;
        }
        VerticalForm form;
        auto orientation = verticalOrientation(part.first);
        if (part.uprightSequence) form = VerticalForm::upright;
        else if (isPoint(part.first)) form = VerticalForm::point;
        else if (isLine(part.first)) form = VerticalForm::line;
        else if (isMiddleDot(part.first)) form = VerticalForm::middleDot;
        else if (orientation == VerticalOrientation::R) form = VerticalForm::sideways;
        else if (orientation == VerticalOrientation::Tr) form = VerticalForm::bracket;
        else form = VerticalForm::upright;
        add(part.begin, part.end, form);
        ++i;
    }
    return result;
}
VerticalParams verticalParams(const ComponentConfig& control, float scale) {
    VerticalParams params;
    // Anything else (left; #84's justify/distribute are meaningless for vertical text) reads from the top.
    if (control.textAlign == TextAlign::center) params.align = VerticalParams::Align::center;
    else if (control.textAlign == TextAlign::right) params.align = VerticalParams::Align::bottom;
    params.lineSpacing = control.lineSpacing;
    params.paragraphSpacing = paragraphGap(control, scale);
    return params;
}

std::vector<VerticalColumn> flowVertical(TextMeasure& measure, std::u16string_view text, int em, int maxDigits, int height) {
    std::vector<VerticalColumn> columns;
    if (text.empty()) return columns;
    for (std::size_t paragraph = 0; paragraph <= text.size();) {
        auto end = text.find(u'\n', paragraph);
        if (end == std::u16string_view::npos) end = text.size();
        auto part = text.substr(paragraph, end - paragraph);
        if (!part.empty() && part.back() == u'\r') part.remove_suffix(1);
        const auto base = static_cast<unsigned>(paragraph);
        VerticalColumn column;
        column.begin = column.end = base;
        int y = 0;
        auto finish = [&](bool paragraphEnd = false) {
            column.lastInParagraph = paragraphEnd;
            column.bounds = {0, 0, 0, y};
            columns.push_back(std::move(column));
            column = {};
            y = 0;
        };
        auto place = [&](unsigned begin, unsigned stop, VerticalForm form, int advance) {
            if (height > 0 && y > 0 && y + advance > height) { column.end = begin; finish(); column.begin = begin; }
            if (column.pieces.empty()) column.begin = begin;
            column.pieces.push_back({begin, stop, form, y, advance});
            y += advance;
            column.end = stop;
        };
        for (const auto& segment : segmentVertical(part, maxDigits)) {
            const unsigned first = base + segment.begin, last = base + segment.end;
            if (segment.form == VerticalForm::combined) { place(first, last, segment.form, em); continue; }
            if (segment.form != VerticalForm::sideways) {
                // One cell per character; clusters keep their marks.
                for (const auto& cluster : clusters(part.substr(segment.begin, segment.end - segment.begin)))
                    place(first + cluster.begin, first + cluster.end, segment.form, em);
                continue;
            }
            // A sideways run breaks after a space; a word longer than the column is cut where it overflows.
            auto runText = part.substr(segment.begin, segment.end - segment.begin);
            auto run = measure.measure(runText, em);
            unsigned token = 0;
            while (token < runText.size()) {
                unsigned tokenEnd = token;
                while (tokenEnd < runText.size() && runText[tokenEnd++] != u' ') {}
                auto widthOf = [&](unsigned from, unsigned to) { return static_cast<int>(std::ceil(measure.measure(runText.substr(from, to - from), em).width)); };
                int advance = widthOf(token, tokenEnd);
                if (height > 0 && advance > height) {
                    // Hard cut: fill the rest of this column, then whole columns, at caret stops.
                    unsigned from = token;
                    while (from < tokenEnd) {
                        unsigned to = from;
                        int used = 0;
                        for (const auto& [stop, x] : run.stops) {
                            if (stop <= from || stop > tokenEnd) continue;
                            int w = static_cast<int>(std::ceil(run.x(stop) - run.x(from)));  // caret stops: no reshaping per candidate
                            if (y + w > height && to > from) break;
                            to = stop; used = w;
                            if (y + w >= height) break;
                        }
                        if (to == from) to = tokenEnd;
                        place(first + from, first + to, VerticalForm::sideways, used ? used : widthOf(from, to));
                        from = to;
                        if (from < tokenEnd) { column.end = first + from; finish(); column.begin = first + from; }
                    }
                } else place(first + token, first + tokenEnd, VerticalForm::sideways, advance);
                token = tokenEnd;
            }
        }
        column.end = std::max(column.end, base + static_cast<unsigned>(part.size()));
        if (column.pieces.empty()) column.begin = column.end = base + static_cast<unsigned>(part.size());
        finish(true);
        paragraph = end + 1;
        if (end == text.size()) break;
    }
    return columns;
}
}
