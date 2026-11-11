#include "vertical.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace lcars::detail {
namespace {
struct OrientationRange { char32_t first, last; VerticalOrientation value; };
constexpr OrientationRange orientation_table[] = {
#include "vertical_orientation_data.inc"
};

// Taiwan Ministry of Education rules (see the research note): point punctuation is centered.
bool is_point(char32_t c) { return c == 0xff0c || c == 0x3002 || c == 0x3001 || c == 0xff1b || c == 0xff1a || c == 0xff01 || c == 0xff1f; }
bool is_line(char32_t c) { return c == 0x2014 || c == 0x2015 || c == 0x2e3a || c == 0x2e3b || c == 0x2500 || c == 0x2026 || c == 0x22ef; }
bool is_middle_dot(char32_t c) { return c == 0x00b7 || c == 0x2027 || c == 0x30fb; }
bool is_ascii_digit(char32_t c) { return c >= u'0' && c <= u'9'; }
bool is_ascii_letter(char32_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }
bool extends(char32_t c) {
    return (c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff) || (c >= 0x1dc0 && c <= 0x1dff) || (c >= 0x20d0 && c <= 0x20ff) ||
           (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xfe20 && c <= 0xfe2f) || (c >= 0xe0100 && c <= 0xe01ef) || (c >= 0x1f3fb && c <= 0x1f3ff) ||
           (c >= 0xe0020 && c <= 0xe007f) || c == 0x200c || c == 0x200d;
}
bool regional_indicator(char32_t c) { return c >= 0x1f1e6 && c <= 0x1f1ff; }

struct Cluster { unsigned begin, end; char32_t first; bool upright_sequence; };
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
            if (join || extends(next) || (regional_indicator(cluster.first) && regional_indicator(next) && i == cluster.begin + 2 && !cluster.upright_sequence)) {
                if (next == 0xfe0f || next == 0x20e3 || next == 0x200d || regional_indicator(next)) cluster.upright_sequence = true;
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

VerticalOrientation vertical_orientation(char32_t code_point) {
    auto it = std::upper_bound(std::begin(orientation_table), std::end(orientation_table), code_point,
                               [](char32_t value, const OrientationRange& range) { return value < range.first; });
    if (it == std::begin(orientation_table)) return VerticalOrientation::R;
    --it;
    return code_point <= it->last ? it->value : VerticalOrientation::R;
}

std::vector<VerticalSegment> segment_vertical(std::u16string_view text, int max_digits) {
    auto parts = clusters(text);
    std::vector<VerticalSegment> result;
    auto add = [&](unsigned begin, unsigned end, VerticalForm form) {
        if (form == VerticalForm::sideways && !result.empty() && result.back().form == form && result.back().end == begin) result.back().end = end;
        else result.push_back({begin, end, form});
    };
    for (std::size_t i = 0; i < parts.size();) {
        const auto& part = parts[i];
        if (is_ascii_digit(part.first)) {
            std::size_t last = i;
            while (last + 1 < parts.size() && is_ascii_digit(parts[last + 1].first)) ++last;
            int count = static_cast<int>(last - i + 1);
            bool touches_letter = (i > 0 && is_ascii_letter(parts[i - 1].first)) || (last + 1 < parts.size() && is_ascii_letter(parts[last + 1].first));
            unsigned begin = part.begin, end = parts[last].end;
            if (touches_letter) add(begin, end, VerticalForm::sideways);
            else if (count == 1) add(begin, end, VerticalForm::upright);
            else if (count <= max_digits) add(begin, end, VerticalForm::combined);
            else add(begin, end, VerticalForm::sideways);
            i = last + 1;
            continue;
        }
        VerticalForm form;
        auto orientation = vertical_orientation(part.first);
        if (part.upright_sequence) form = VerticalForm::upright;
        else if (is_point(part.first)) form = VerticalForm::point;
        else if (is_line(part.first)) form = VerticalForm::line;
        else if (is_middle_dot(part.first)) form = VerticalForm::middle_dot;
        else if (orientation == VerticalOrientation::R) form = VerticalForm::sideways;
        else if (orientation == VerticalOrientation::Tr) form = VerticalForm::bracket;
        else form = VerticalForm::upright;
        add(part.begin, part.end, form);
        ++i;
    }
    return result;
}
VerticalParams vertical_params(const Control&) { return {}; }

std::vector<VerticalColumn> flow_vertical(TextMeasure& measure, std::u16string_view text, int em, int max_digits, int height) {
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
        auto finish = [&](bool paragraph_end = false) {
            column.last_in_paragraph = paragraph_end;
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
        for (const auto& segment : segment_vertical(part, max_digits)) {
            const unsigned first = base + segment.begin, last = base + segment.end;
            if (segment.form == VerticalForm::combined) { place(first, last, segment.form, em); continue; }
            if (segment.form != VerticalForm::sideways) {
                // One cell per character; clusters keep their marks.
                for (const auto& cluster : clusters(part.substr(segment.begin, segment.end - segment.begin)))
                    place(first + cluster.begin, first + cluster.end, segment.form, em);
                continue;
            }
            // A sideways run breaks after a space; a word longer than the column is cut where it overflows.
            auto run_text = part.substr(segment.begin, segment.end - segment.begin);
            auto run = measure.measure(run_text, em);
            unsigned token = 0;
            while (token < run_text.size()) {
                unsigned token_end = token;
                while (token_end < run_text.size() && run_text[token_end++] != u' ') {}
                auto width_of = [&](unsigned from, unsigned to) { return static_cast<int>(std::ceil(measure.measure(run_text.substr(from, to - from), em).width)); };
                int advance = width_of(token, token_end);
                if (height > 0 && advance > height) {
                    // Hard cut: fill the rest of this column, then whole columns, at caret stops.
                    unsigned from = token;
                    while (from < token_end) {
                        unsigned to = from;
                        int used = 0;
                        for (const auto& [stop, x] : run.stops) {
                            if (stop <= from || stop > token_end) continue;
                            int w = static_cast<int>(std::ceil(run.x(stop) - run.x(from)));  // caret stops: no reshaping per candidate
                            if (y + w > height && to > from) break;
                            to = stop; used = w;
                            if (y + w >= height) break;
                        }
                        if (to == from) to = token_end;
                        place(first + from, first + to, VerticalForm::sideways, used ? used : width_of(from, to));
                        from = to;
                        if (from < token_end) { column.end = first + from; finish(); column.begin = first + from; }
                    }
                } else place(first + token, first + token_end, VerticalForm::sideways, advance);
                token = token_end;
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
