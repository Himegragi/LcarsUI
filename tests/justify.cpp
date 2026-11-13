#include "core_internal.hpp"
#include "component_type.hpp"
#include "justify.hpp"
// #84 justified and distributed alignment through the public core, semantic and layout seams, with a
// deterministic fake text backend (FakeMeasure at 16 px: an ASCII character is 8 px, any other one 16 px).
// Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include <algorithm>
#include <cmath>
using namespace lcars;
using test::check;
using test::FakeMeasure;

namespace {
struct Laid {
    FakeMeasure measure; ScrollState scroll; Frame frame;
    template<class Handle> Laid(Ui& ui, const Handle& handle, int width = 100, float scale = 1.f, const Appearance& appearance = web_appearance()) {
        frame = arrange(layout::component(handle), ui, appearance, measure, width, 400, scale, scroll);
    }
};
ComponentConfig label(std::string id, std::u16string value, TextAlign align, JustifyMethod method = JustifyMethod::automatic) {
    return {.id = std::move(id), .value = std::move(value), .text_align = align, .justify_method = method};
}
// What a drawn line looks like: where its pieces start and end. A line that is not justified is one piece.
struct Edges { float left = 0, right = 0; std::size_t pieces = 0; };
Edges edges_of(const Line& line, int pixels = 16) {
    FakeMeasure measure;
    if (line.segments.empty()) return {static_cast<float>(line.bounds.x), line.bounds.x + measure.measure(line.text, pixels).width, 1};
    const auto& last = line.segments.back();
    const auto tail = std::u16string_view(line.text).substr(last.begin, last.end - last.begin);
    return {line.segments.front().x, last.x + measure.measure(tail, pixels).width, line.segments.size()};
}
bool near(float a, float b, float tolerance = 1.f) { return std::abs(a - b) <= tolerance; }

// 14 wide characters in a 100 px label: 6 + 6 + 2 per line (a character is 16 px).
void a_justified_label_spreads_chinese_evenly_between_the_characters() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"一二三四五六七八九十甲乙丙丁", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 3, "Three lines");
    // 96 px of text in 100 px: 4 px over five gaps, 0.8 px each.
    const auto& first = p->lines[0];
    check(first.segments.size() == 6, "Every character is its own piece");
    for (std::size_t i = 0; i < first.segments.size(); ++i)
        check(near(first.segments[i].x, 16.8f * static_cast<float>(i), 0.05f), "Pieces are 16.8 px apart: the character plus an equal share");
    const auto edges = edges_of(first);
    check(near(edges.left, 0) && near(edges.right, 100), "Both ends are flush with the label");
    check(near(edges_of(p->lines[1]).left, 0) && near(edges_of(p->lines[1]).right, 100), "So is the second line");
    check(p->lines[2].segments.empty() && p->lines[2].bounds.x == 0, "The last line of the paragraph stays left-aligned");
}

struct Piece { unsigned begin, end; float x; };
std::vector<Piece> pieces_of(const Line& line) {
    std::vector<Piece> out;
    for (const auto& s : line.segments) out.push_back({s.begin, s.end, s.x});
    return out;
}
bool same_pieces(const std::vector<Piece>& got, const std::vector<Piece>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i].begin != want[i].begin || got[i].end != want[i].end || !near(got[i].x, want[i].x, 0.05f)) return false;
    return true;
}

// "我愛 cat 中" is 88 px in a 100 px label; the 12 px spare goes to three places, 4 px each: after 我, after
// the first space, after the second space. "cat" itself and the space after 愛 are not touched.
void the_automatic_method_keeps_english_words_whole_and_widens_spaces() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"我愛 cat 中文字", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 2 && p->lines[0].text == u"我愛 cat 中", "The paragraph wraps after 中");
    check(same_pieces(pieces_of(p->lines[0]), {{0, 1, 0}, {1, 3, 20}, {3, 7, 48}, {7, 8, 84}}), "Pieces: 我 | 愛_ | cat_ | 中, 4 px added after each of the first three");
    check(near(edges_of(p->lines[0]).right, 100), "Flush right");
    check(p->lines[1].segments.empty(), "The last line is left-aligned");
}

void full_width_punctuation_keeps_its_width_and_takes_a_share_like_a_character() {
    Ui ui;
    // 一，二，三，四，五，六，七 : 13 characters of 16 px; a 100 px label holds six per line.
    auto handle = *ui.add_label(label("l", u"一，二，三，四，五，六，七", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 3 && p->lines[0].text == u"一，二，三，", "Six characters per line");
    // 96 px in 100: 4 px over five gaps. Each mark keeps one 16 px cell and is spaced exactly like a character.
    check(same_pieces(pieces_of(p->lines[0]), {{0, 1, 0}, {1, 2, 16.8f}, {2, 3, 33.6f}, {3, 4, 50.4f}, {4, 5, 67.2f}, {5, 6, 84}}),
          "Characters and marks sit 16.8 px apart");
}

void the_spaces_method_only_widens_spaces_and_a_chinese_line_stays_left() {
    {
        Ui ui;
        auto handle = *ui.add_label(label("l", u"一二三四五六七八九十甲乙丙丁", TextAlign::justify, JustifyMethod::spaces));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p && p->lines.size() == 3 && p->lines[0].segments.empty() && p->lines[0].bounds.x == 0, "A line without spaces is not stretched");
    }
    Ui ui;
    // A 100 px label holds 12 characters of 8 px: "aa bb cc dd " breaks after the space. Its content "aa bb cc dd"
    // is 88 px, so 12 px go to the three spaces inside it, 4 px each; the trailing space gets nothing.
    auto handle = *ui.add_label(label("l", u"aa bb cc dd ee ff", TextAlign::justify, JustifyMethod::spaces));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 2 && p->lines[0].text == u"aa bb cc dd ", "One wrapped line, ending in its space");
    check(same_pieces(pieces_of(p->lines[0]), {{0, 3, 0}, {3, 6, 28}, {6, 9, 56}, {9, 11, 84}, {11, 12, 100}}), "Only the spaces inside the content are widened");
}

void a_line_with_nowhere_to_stretch_stays_left_and_trailing_spaces_get_nothing() {
    Ui ui;
    // One long word: no gap in either automatic or spaces mode. "abcdefghijkl" is 96 px, then more text wraps it.
    for (auto method : {JustifyMethod::automatic, JustifyMethod::spaces}) {
        auto handle = *ui.add_label(label(method == JustifyMethod::automatic ? "a" : "s", u"abcdefghijklmnopqrstuvwxyz", TextAlign::justify, method));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p && p->lines.size() == 3 && p->lines[0].segments.empty() && p->lines[0].bounds.x == 0, "A word alone on a line stays left-aligned");
    }
    // The line "一二三四五" + two spaces: the trailing spaces are neither part of the width nor given a share, so the
    // five characters (80 px) spread over the 100 px with 5 px per gap, and the spaces follow at the edge.
    auto handle = *ui.add_label(label("t", u"一二三四五    六", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines[0].text == u"一二三四五  ", "The line keeps two trailing spaces");
    check(same_pieces(pieces_of(p->lines[0]), {{0, 1, 0}, {1, 2, 21}, {2, 3, 42}, {3, 4, 63}, {4, 5, 84}, {5, 7, 100}}),
          "The visible text reaches the right edge; the spaces start there");
}

void the_per_character_method_spreads_latin_letters_and_never_draws_a_ligature() {
    // "difficult " is 80 px with its space; the nine letters are 72 px. Spread to 100: 28 px over eight gaps, 3.5 each.
    Ui ui;
    auto per_character = *ui.add_label(label("p", u"difficult fiasco", TextAlign::justify, JustifyMethod::per_character));
    auto automatic = *ui.add_label(label("a", u"difficult fiasco", TextAlign::justify, JustifyMethod::automatic));
    {
        Laid laid(ui, per_character);
        const auto* p = laid.frame.find(per_character);
        check(p && p->lines.size() == 2 && p->lines[0].text == u"difficult ", "The first line ends after the space");
        check(p->lines[0].segments.size() == 10, "Nine single letters and the trailing space");
        for (const auto& s : p->lines[0].segments) {
            const auto piece = p->lines[0].text.substr(s.begin, s.end - s.begin);
            check(piece.find(u"fi") == std::u16string::npos && piece.find(u"ff") == std::u16string::npos, "No piece holds a ligature pair");
        }
        check(near(p->lines[0].segments[8].x, 92) && near(p->lines[0].segments[1].x, 11.5f), "Letters are 8 + 3.5 px apart; the last one ends at 100");
    }
    Laid laid(ui, automatic);
    check(laid.frame.find(automatic)->lines[0].segments.empty(), "The automatic method leaves a line without spaces or Chinese alone");
}

// A shaper that makes "ffi" and "fi" one cluster, like a font with those ligatures: no caret stop inside them.
struct LigatureMeasure final : TextMeasure {
    FakeMeasure base;
    TextRun measure(std::u16string_view text, int pixels) override {
        auto run = base.measure(text, pixels);
        std::vector<std::pair<unsigned, float>> kept;
        for (const auto& stop : run.stops) {
            const bool inside = (stop.first >= 1 && stop.first < text.size() && text[stop.first - 1] == u'f' && (text[stop.first] == u'f' || text[stop.first] == u'i')) ||
                                (stop.first >= 2 && stop.first < text.size() && text[stop.first - 2] == u'f' && text[stop.first - 1] == u'f' && text[stop.first] == u'i');
            if (!inside) kept.push_back(stop);
        }
        run.stops = std::move(kept);
        return run;
    }
    // Ligatures off: every character is a cluster again (the fake has no kerning, so the rest is the same run).
    TextRun measure_apart(std::u16string_view text, int pixels, FontStyle) override { return base.measure(text, pixels); }
    int line_height(int pixels) override { return base.line_height(pixels); }
};

// A shaper with pair kerning ("AV" is 4 px tighter) and a character that is two code points in one cluster
// (the Hangul jamo U+1100 U+1161), measured at 16 px: ASCII 8 px, anything else 16 px.
struct KernMeasure final : TextMeasure {
    TextRun measure(std::u16string_view text, int pixels) override {
        TextRun run;
        run.stops.push_back({0, 0.f});
        float x = 0;
        for (unsigned i = 0; i < text.size();) {
            unsigned next = i + 1;
            if (text[i] == 0x1100 && i + 1 < text.size() && text[i + 1] == 0x1161) next = i + 2;
            float advance = text[i] < 0x80 ? pixels * 0.5f : static_cast<float>(pixels);
            if (text[i] == u'A' && i + 1 < text.size() && text[i + 1] == u'V') advance -= 4;
            x += advance; i = next;
            run.stops.push_back({i, x});
        }
        run.width = x;
        return run;
    }
    int line_height(int pixels) override { return pixels * 3 / 2; }
};
// Code review 2026-11-12 item 10: the per-character method measures the line once (ligatures off, kerning on) and
// takes the piece positions from that run, cutting at its clusters; it does not measure each letter alone.
void the_per_character_method_keeps_kerning_and_cuts_at_clusters() {
    KernMeasure measure;
    const std::u16string kerned = u"AVA";
    const auto natural = measure.measure(kerned, 16);  // A 4 (kerned) + V 8 + A 8 = 20
    auto stretched = detail::justify_line(measure, kerned, 16, natural, 60.f, JustifyMethod::per_character);
    check(stretched && stretched->segments.size() == 3, "Three pieces");
    if (stretched && stretched->segments.size() == 3) {
        const auto& s = stretched->segments;
        // Two gaps share 60 - 0.02 - 20 = 39.98: 19.99 each.
        check(near(s[0].width, 4, 0.01f) && near(s[1].width, 8, 0.01f), "The A before a V keeps its kerned advance");
        check(near(s[1].x, 4 + 19.99f, 0.02f) && near(s[2].x, 4 + 19.99f + 8 + 19.99f, 0.02f), "Kerning sits under the added space");
        check(near(stretched->run.width, 59.98f, 0.02f), "The line still ends at the target width");
    }
    const std::u16string clustered = u"\x1100\x1161字";
    const auto run = measure.measure(clustered, 16);
    auto split = detail::justify_line(measure, clustered, 16, run, 100.f, JustifyMethod::per_character);
    check(split && split->segments.size() == 2, "Two clusters, two pieces");
    if (split) for (const auto& s : split->segments) check(s.begin != 1 && s.end != 1, "No cut inside the two-code-point cluster");
}

void the_per_character_method_cuts_inside_a_ligature_cluster_and_the_others_do_not() {
    Ui ui;
    auto per_character = *ui.add_label(label("p", u"office is open now", TextAlign::justify, JustifyMethod::per_character));
    auto automatic = *ui.add_label(label("a", u"office is open now", TextAlign::justify, JustifyMethod::automatic));
    LigatureMeasure measure; ScrollState scroll;
    auto first = arrange(layout::component(per_character), ui, web_appearance(), measure, 100, 400, 1.f, scroll);
    const auto& line = first.find(per_character)->lines[0];
    check(line.text == u"office is " && std::count_if(line.segments.begin(), line.segments.end(), [&](const TextSegment& s) { return s.end - s.begin == 1; }) >= 8,
          "Every letter is a piece of its own, though the shaper clusters ffi");
    for (const auto& s : line.segments) check(line.text.substr(s.begin, s.end - s.begin).find(u"fi") == std::u16string::npos, "No piece can form the ligature");
    auto second = arrange(layout::component(automatic), ui, web_appearance(), measure, 100, 400, 1.f, scroll);
    const auto& plain = second.find(automatic)->lines[0];
    check(!plain.segments.empty() && plain.segments[0].end == 7, "The automatic method keeps the word, ligature and all, in one piece");
}

void the_distribute_alignment_stretches_the_last_line_and_a_single_line() {
    Ui ui;
    auto name = *ui.add_label(label("n", u"姓名", TextAlign::distribute));
    auto spaced = *ui.add_label(label("s", u"姓　名", TextAlign::distribute));
    auto plain = *ui.add_label(label("j", u"姓名", TextAlign::justify));
    auto one = *ui.add_label(label("o", u"姓", TextAlign::distribute));
    {
        Laid laid(ui, name);
        // 32 px of text in 100: the 68 px spare goes to the single gap.
        check(same_pieces(pieces_of(laid.frame.find(name)->lines[0]), {{0, 1, 0}, {1, 2, 84}}), "Two characters sit at the two ends");
    }
    {
        Laid laid(ui, spaced);
        // Code review item 11: U+3000 is a CJK character like any other (clreq: gaps on both sides of every ideograph and
        // full-width punctuation). 姓, U+3000 and 名 are 48 px; the 52 px spare goes to the two gaps, 25.99 px each.
        check(same_pieces(pieces_of(laid.frame.find(spaced)->lines[0]), {{0, 1, 0}, {1, 2, 41.99f}, {2, 3, 83.98f}}), "A full-width space is a piece with a gap on each side");
    }
    {
        Laid laid(ui, plain);
        check(laid.frame.find(plain)->lines[0].segments.empty(), "Justify leaves a one-line text left-aligned");
    }
    Laid laid(ui, one);
    check(laid.frame.find(one)->lines[0].segments.empty(), "A single character has no gap, so it stays left");
}

void a_character_made_of_several_code_points_is_never_cut() {
    Ui ui;
    // 一 + a variation selector, then five more characters: the selector is a unit of its own for the fake
    // backend, but a cut between it and its base is never made.
    auto selector = *ui.add_label(label("v", u"一︀二三四五六七八九", TextAlign::justify, JustifyMethod::per_character));
    {
        Laid laid(ui, selector);
        const auto* p = laid.frame.find(selector);
        check(p && p->lines[0].segments.size() > 1, "The line is stretched");
        for (const auto& s : p->lines[0].segments) check(s.begin != 1 && s.end != 1, "No cut between a character and its variation selector");
        check(same_pieces({pieces_of(p->lines[0])[0]}, {{0, 2, 0}}), "The first piece is the character with its selector");
    }
    // A ZWJ emoji sequence: woman (a surrogate pair), joiner, laptop (a pair), offsets 2..6.
    auto emoji = *ui.add_label(label("e", u"一二\U0001F469‍\U0001F4BB三四五六七八九", TextAlign::justify, JustifyMethod::per_character));
    Laid laid(ui, emoji);
    const auto* p = laid.frame.find(emoji);
    check(p && p->lines[0].segments.size() > 1, "The emoji line is stretched");
    for (const auto& s : p->lines[0].segments)
        for (unsigned inside : {3u, 4u, 5u, 6u}) check(s.begin != inside && s.end != inside, "The emoji sequence stays in one piece");
}
}

std::size_t laid_segments(Ui& ui, const LabelHandle& handle) {
    Laid laid(ui, handle);
    return laid.frame.find(handle)->lines[0].segments.size();
}

// ---- Text fields ----------------------------------------------------------------------------------
TextHandle field_of(Ui& ui, ComponentConfig control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto field = *ui.add_text(std::move(control));
    check(bool(field.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connect_confirmed([](const Change<TextHandle>&) { return Result{}; })), "Connect a write");
    return field;
}
ComponentConfig multi(std::string id, std::u16string value, TextAlign align, JustifyMethod method = JustifyMethod::automatic, unsigned lines = 3) {
    return {.id = std::move(id), .value = std::move(value), .multiline = true, .visible_lines = lines, .text_align = align, .justify_method = method};
}
ComponentConfig single(std::string id, std::u16string value, TextAlign align) {
    return {.id = std::move(id), .value = std::move(value), .text_align = align};
}
// What a component painted: every text call and every selection fill.
struct TextProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    struct Text { std::u16string text; int x, y; };
    std::vector<Text> texts;
    std::vector<Rect> selections;
    detail::PaintRole current = detail::PaintRole::content;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect r, Color) override { if (current == detail::PaintRole::selection) selections.push_back(r); }
    void rounded(Rect, int, Color) override {}
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int x, int y, Color) override { texts.push_back({std::u16string(value), x, y}); }
};
template <class Handle>
TextProbe painted(Ui& ui, const Frame& frame, const Handle& handle, const char* id) {
    const auto* item = frame.find(handle);
    auto semantics = *detail::access(ui).describe(id);
    TextProbe probe(detail::access(ui), *item, semantics, web_appearance(), 1.f, true);
    detail::component_type(item->kind).paint(probe);
    return probe;
}
// 20 wide characters in a field 190 px wide: the text area is 152 px, lines wrap at 150, so nine fit (144 px).
const std::u16string twenty = u"一二三四五六七八九十甲乙丙丁戊己庚辛壬癸";
const int inset = web_appearance().component_presentation(Density::standard).inset;

void a_justified_multi_line_field_stretches_each_wrapped_line_to_the_wrap_edge() {
    Ui ui;
    auto handle = field_of(ui, multi("t", twenty, TextAlign::justify));
    Laid laid(ui, handle, 190);
    const auto* p = laid.frame.find(handle);
    check(p && p->multiline && p->view.lines.size() == 3, "9 + 9 + 2 characters");
    const float wrap = static_cast<float>(p->view.text.w - inset);
    // 6 px spare over eight gaps: 0.75 px per gap, so the caret stop before character k is 16.75 * k.
    for (unsigned k : {0u, 1u, 3u, 8u}) check(near(p->view.x_in_line(0, k), 16.75f * static_cast<float>(k), 0.05f), "Stops follow the stretched characters");
    check(near(p->view.x_in_line(0, 9), wrap, 0.05f) && near(p->view.x_in_line(1, 18), wrap, 0.05f), "The first two lines end at the wrap edge");
    check(near(p->view.x_in_line(1, 12), 3 * 16.75f, 0.05f), "The second line is stretched the same way");
    check(p->view.lines[2].segments.empty() && near(p->view.x_in_line(2, 20), 32, 0.05f), "The paragraph's last line is as wide as its text, at the left");
    // The caret is drawn where the stops put it, and a wrap point belongs to the next line.
    check(p->caret_x(3) == p->view.text.x + static_cast<int>(std::lround(50.25f)) && p->caret_x(9) == p->view.text.x, "caret_x follows the stretched stops");
}

void clicks_and_drags_land_on_the_drawn_characters_of_a_justified_line() {
    Ui ui;
    auto handle = field_of(ui, multi("t", twenty, TextAlign::justify));
    Laid laid(ui, handle, 190);
    const auto* p = laid.frame.find(handle);
    const int x = p->view.text.x, y0 = p->view.text.y + 5, y1 = y0 + 24;
    // Stops of line 0 at 0, 16.75, 33.5, 50.25, 67, ...: the nearest wins.
    check(laid.frame.text_offset(handle, x + 8, y0) == 0 && laid.frame.text_offset(handle, x + 9, y0) == 1, "Halfway between the first two stops");
    check(laid.frame.text_offset(handle, x + 50, y0) == 3 && laid.frame.text_offset(handle, x + 60, y0) == 4, "A click near a stop goes to it");
    check(laid.frame.text_offset(handle, x + 140, y0) == 8 && laid.frame.text_offset(handle, x + 149, y0) == 9, "Near the right edge: the end of the line");
    check(laid.frame.text_offset(handle, x + 50, y1) == 12, "Line 1 maps the same way");
    auto& host = detail::access(ui);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 0, 0);
    check(laid.frame.select_to(ui, handle, x + 52, y1) && host.edit("t")->selection.anchor == 0 && host.edit("t")->selection.caret == 12, "A drag into the second line selects up to the character under the pointer");
    // The selection highlight is drawn from stop to stop: line 0 whole, line 1 up to 50.25 px.
    Laid after(ui, handle, 190);
    auto probe = painted(ui, after.frame, handle, "t");
    check(probe.selections.size() == 2 && probe.selections[0].x == x && near(static_cast<float>(probe.selections[0].w), 150, 1) && near(static_cast<float>(probe.selections[1].w), 50.25f, 1), "The highlight follows the stretched stops");
}

void vertical_keys_and_the_input_method_follow_the_stretched_positions() {
    Ui ui;
    auto handle = field_of(ui, multi("t", twenty, TextAlign::justify));
    auto& host = detail::access(ui);
    Laid first(ui, handle, 190);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 20, 20);  // the end of the short last line, 32 px from the left
    Laid start(ui, handle, 190);
    check(bool(host.key_down({.key = Key::up})), "Up");
    // The stretched line's stops: 2 * 16.75 = 33.5 is the nearest to 32, so the caret lands before the third character.
    Laid above(ui, handle, 190);
    check(host.edit("t")->selection.caret == 11, "Up keeps the drawn x: the nearest stop of the stretched line");
    check(bool(host.key_down({.key = Key::down})), "Down");
    Laid below(ui, handle, 190);
    // From stop 33.5 down to the last line, whose stops are 0, 16, 32: the nearest is 32, the end.
    check(host.edit("t")->selection.caret == 20, "And back to the end of the last line");
    // The input method anchors at the caret as drawn.
    host.select("t", 3, 3);
    Laid laid(ui, handle, 190);
    const auto* p = laid.frame.find(handle);
    check(p->caret_rect(3).x == p->caret_x(3) && p->caret_rect(3).x == p->view.text.x + 50, "The input method's anchor is the stretched caret");
}

void a_justified_single_line_field_looks_left_aligned() {
    Ui plain_ui, justified_ui;
    auto plain = field_of(plain_ui, single("t", u"一二三四五", TextAlign::left));
    auto justified = field_of(justified_ui, single("t", u"一二三四五", TextAlign::justify));
    Laid a(plain_ui, plain, 200), b(justified_ui, justified, 200);
    const auto *pa = a.frame.find(plain), *pb = b.frame.find(justified);
    check(pb->shift == 0 && pa->shift == 0, "No shift");
    for (unsigned k = 0; k <= 5; ++k) check(pa->caret_x(k) == pb->caret_x(k), "The same caret stops as a left-aligned field");
    check(painted(justified_ui, b.frame, justified, "t").texts.size() == painted(plain_ui, a.frame, plain, "t").texts.size(), "Drawn in one piece, as before");
    check(*justified.text_align() == TextAlign::justify, "The property still reads justify");
}

void a_field_refuses_distribute_and_a_label_accepts_it() {
    Ui ui;
    check(ui.add_text(single("bad", u"x", TextAlign::distribute)).status == Status::invalid_value, "A single-line field is not created distributed");
    check(ui.add_text(multi("bad", u"x", TextAlign::distribute)).status == Status::invalid_value, "Nor a multi-line one");
    check(ui.add_button({.id = "b", .text_align = TextAlign::justify}).status == Status::invalid_value, "A button has no alignment at all");
    check(ui.add_button({.id = "b2", .justify_method = JustifyMethod::spaces}).status == Status::invalid_value, "Nor a justification method");
    check(ui.add_label(label("bad", u"x", TextAlign::left, static_cast<JustifyMethod>(9))).status == Status::invalid_value, "An unknown method is invalid at creation");
    auto field = field_of(ui, multi("t", u"abc", TextAlign::center, JustifyMethod::spaces));
    auto name = *ui.add_label(label("l", u"abc", TextAlign::distribute));
    check(*name.text_align() == TextAlign::distribute, "A label can be distributed");
    check(field.set_text_align(TextAlign::distribute).status == Status::invalid_value && *field.text_align() == TextAlign::center, "The handle refuses and leaves the field unchanged");
    auto& tree = ui.semantics();
    check(tree.perform({"t"}, Verb::set_text_align, TextAlign::distribute).status == Status::invalid_value && *field.text_align() == TextAlign::center, "So does the semantic layer, with the same status");
    check(bool(field.set_text_align(TextAlign::justify)) && *field.text_align() == TextAlign::justify, "justify is fine");
    check(field.set_text_align(static_cast<TextAlign>(9)).status == Status::invalid_value && name.set_text_align(static_cast<TextAlign>(9)).status == Status::invalid_value, "An unknown alignment is invalid");
}

void the_justification_method_is_readable_and_settable_everywhere() {
    Ui ui;
    auto name = *ui.add_label(label("l", u"一二三", TextAlign::justify));
    auto field = field_of(ui, multi("t", u"abc", TextAlign::justify, JustifyMethod::spaces));
    auto button = *ui.add_button({.id = "b"});
    check(*name.justify_method() == JustifyMethod::automatic && *field.justify_method() == JustifyMethod::spaces, "Defaults and creation values");
    for (auto method : {JustifyMethod::per_character, JustifyMethod::spaces, JustifyMethod::automatic}) {
        check(bool(name.set_justify_method(method)) && *name.justify_method() == method, "A label takes every method");
        check(bool(field.set_justify_method(method)) && *field.justify_method() == method, "So does a field");
    }
    check(name.set_justify_method(static_cast<JustifyMethod>(7)).status == Status::invalid_value && *name.justify_method() == JustifyMethod::automatic, "An unknown method is invalid and changes nothing");
    check(field.describe()->justify_method == JustifyMethod::automatic, "describe reports it");
    auto& tree = ui.semantics();
    auto node = tree.get({"l"});
    check(node && node->justify_method == JustifyMethod::automatic && node->text_align == TextAlign::justify, "The semantic node reports alignment and method");
    check(std::find(node->verbs.begin(), node->verbs.end(), Verb::set_justify_method) != node->verbs.end(), "The verb is offered");
    const auto button_node = *tree.get({"b"});
    check(std::find(button_node.verbs.begin(), button_node.verbs.end(), Verb::set_justify_method) == button_node.verbs.end(), "A button is not offered it");
    check(tree.perform({"b"}, Verb::set_justify_method, JustifyMethod::spaces).status == Status::not_supported, "A button refuses it");
    std::vector<std::string> changed;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::property_changed) changed.push_back(e.property); });
    check(bool(tree.perform({"l"}, Verb::set_justify_method, JustifyMethod::per_character)) && *name.justify_method() == JustifyMethod::per_character, "The semantic write reaches the typed read");
    check(bool(field.set_justify_method(JustifyMethod::spaces)) && tree.get({"t"})->justify_method == JustifyMethod::spaces, "And the typed write reaches the semantic read");
    tree.dispatch_events();
    check(std::count(changed.begin(), changed.end(), "justify_method") >= 1, "A change raises property_changed");
    check(tree.perform({"l"}, Verb::set_justify_method, static_cast<JustifyMethod>(7)).status == name.set_justify_method(static_cast<JustifyMethod>(7)).status, "The same status for an unknown method");
    check(tree.perform({"l"}, Verb::set_justify_method, 1).status == Status::invalid_value, "The wrong argument type is invalid");
    check(tree.perform({"l"}, Verb::set_text_align, TextAlign::distribute).status == Status::ok && tree.get({"l"})->text_align == TextAlign::distribute, "The semantic layer reads and writes both new alignments");
    check(tree.perform({"missing"}, Verb::set_justify_method, JustifyMethod::spaces).status == Status::not_found, "An unknown component is not found");
}

void a_run_time_change_restretches_the_next_frame() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"一二三四五六七八九十", TextAlign::left));
    check(laid_segments(ui, handle) == 0, "Left at first");
    check(bool(handle.set_text_align(TextAlign::justify)) && laid_segments(ui, handle) == 6, "The next arrange is stretched: six pieces on the first line");
    check(bool(handle.set_justify_method(JustifyMethod::spaces)) && laid_segments(ui, handle) == 0, "A method with no spaces to use leaves it left");
    check(bool(handle.set_justify_method(JustifyMethod::per_character)) && laid_segments(ui, handle) == 6, "And back");
}

void the_user_setting_draws_stretched_text_left_but_keeps_the_properties() {
    Ui ui;
    auto name = *ui.add_label(label("l", twenty, TextAlign::justify));
    auto spread = *ui.add_label(label("d", u"姓名", TextAlign::distribute));
    auto field = field_of(ui, multi("t", twenty, TextAlign::justify));
    check(laid_segments(ui, name) == 6, "Stretched by default");
    auto settings = ui.presentation();
    settings.justify_as_left = true;
    check(bool(ui.set_presentation(settings)), "Turn the setting on");
    check(laid_segments(ui, name) == 0 && laid_segments(ui, spread) == 0, "Labels draw left-aligned");
    {
        Laid laid(ui, field, 190);
        const auto* p = laid.frame.find(field);
        check(p->view.lines[0].segments.empty() && near(p->view.x_in_line(0, 9), 144, 0.05f) && p->caret_x(3) == p->view.text.x + 48, "A field draws left-aligned: stops are the natural ones");
        auto probe = painted(ui, laid.frame, field, "t");
        check(probe.texts.size() == 3 && probe.texts[0].text.size() == 9, "Each line is drawn in one piece");
    }
    check(*name.text_align() == TextAlign::justify && *spread.text_align() == TextAlign::distribute && *field.text_align() == TextAlign::justify, "The property values are unchanged");
    check(ui.semantics().get({"l"})->text_align == TextAlign::justify && ui.semantics().get({"d"})->text_align == TextAlign::distribute, "So are the semantic reads");
    settings.justify_as_left = false;
    check(bool(ui.set_presentation(settings)) && laid_segments(ui, name) == 6, "Turning it off stretches again");
    check(laid_segments(ui, spread) == 2, "A distributed label too");
}

// ---- #81 x #84: justified rich text -----------------------------------------------------------------
// FakeMeasure at 16 px: an ASCII character is 8 px, a bold one 10 px. The words are two letters, so with the
// spans below "bb cc" is bold and underlined (10 + 10 + 10 + 10 + 10 = 50 px) and the space after it is plain.
const std::u16string words = u"aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp";
const std::vector<StyleSpan> rich_spans = {{.start = 3, .length = 5, .bold = true, .underline = true, .color = {}}, {.start = 27, .length = 5, .italic = true, .color = {}}};
TextHandle rich_field_of(Ui& ui, ComponentConfig control, std::vector<StyleSpan> spans = rich_spans) {
    control.rich = true;
    auto model = std::make_shared<RichText>(RichText{control.value, std::move(spans)});
    auto field = *ui.add_text(std::move(control));
    check(bool(field.connect_rich_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a rich source");
    check(bool(field.connect_rich_confirmed([](const Change<TextHandle, RichText>&) { return Result{}; })), "Connect a rich write");
    return field;
}
// What a rich field painted: every styled draw call and every fill, with the role it was painted in.
struct StyledProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    struct Draw { std::u16string text; FontStyle style; int x, y; };
    struct Fill { Rect rect; detail::PaintRole role; };
    std::vector<Draw> draws;
    std::vector<Fill> fills;
    detail::PaintRole current = detail::PaintRole::content;
    void role(detail::PaintRole r) override { current = r; }
    void fill(Rect r, Color) override { fills.push_back({r, current}); }
    void rounded(Rect, int, Color) override {}
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int x, int y, Color) override { draws.push_back({std::u16string(value), {}, x, y}); }
    void styled_text(std::u16string_view value, int, FontStyle style, int x, int y, Color) override { draws.push_back({std::u16string(value), style, x, y}); }
};

void a_justified_rich_line_keeps_caret_selection_hits_glyphs_and_underline_together() {
    for (auto method : {JustifyMethod::automatic, JustifyMethod::spaces, JustifyMethod::per_character}) {
        Ui ui;
        auto handle = rich_field_of(ui, multi("t", words, TextAlign::justify, method, 4));
        check(bool(handle.focus()) && bool(handle.select(1, 4)), "Focus and select across the bold run's start");
        Laid laid(ui, handle, 190);
        const auto* p = laid.frame.find(handle);
        const auto& view = p->view;
        check(p->multiline && view.lines.size() >= 3 && view.lines[0].soft, "The text wraps into several lines");
        const float wrap = static_cast<float>(view.text.w - inset);
        const auto& line = view.lines[0];
        check(!line.segments.empty() && line.pieces.size() == 3 && line.pieces[1].style.bold && line.pieces[1].style.underline && !line.pieces[0].style.bold && !line.pieces[2].style.bold,
              "A rich line keeps its runs (plain | bold underline | plain) and is cut into segments");
        // Every segment lies inside exactly one run, so it is drawn in that run's face.
        bool contained = true;
        for (const auto& segment : line.segments)
            contained &= std::count_if(line.pieces.begin(), line.pieces.end(), [&](const auto& piece) { return segment.begin >= piece.begin && segment.end <= piece.end; }) == 1;
        check(contained, "Each segment is inside exactly one style run");
        // The run's x and width are those of its segments; the stops are the segments' stops.
        for (const auto& piece : line.pieces) {
            const TextSegment* first = nullptr; const TextSegment* last = nullptr;
            for (const auto& segment : line.segments)
                if (segment.begin >= piece.begin && segment.end <= piece.end) { if (!first) first = &segment; last = &segment; }
            check(first && near(piece.x, first->x, 0.01f) && near(piece.x + piece.width, last->x + last->width, 0.01f), "A run starts at its first segment and ends at the end of its last");
        }
        for (const auto& segment : line.segments)
            check(near(view.x_in_line(0, segment.begin), segment.x, 0.01f), "The caret stop before a segment is where the segment is drawn");
        // The edge: the content of every stretched wrapped line ends at the wrap edge, in every method.
        for (unsigned i = 0; i + 1 < view.lines.size(); ++i) {
            const auto& item = view.lines[i];
            const auto visible = std::u16string_view(words).substr(item.begin, item.end - item.begin);
            const unsigned content_end = item.begin + static_cast<unsigned>(visible.find_last_not_of(u' ') + 1);
            if (!item.segments.empty()) check(near(view.x_in_line(i, content_end), wrap, 1.f), "A stretched rich line ends flush at the wrap edge");
        }
        // The bold characters are measured bold: 'b' is 10 px wide.
        if (method == JustifyMethod::per_character) check(view.x_in_line(0, 4) - view.x_in_line(0, 3) > 10.f, "Per character: the stop after a bold letter includes the gap added after it");
        else check(near(view.x_in_line(0, 4) - view.x_in_line(0, 3), 10.f, 0.01f), "A bold character advances by its bold width");
        // Drawn glyphs: every call starts at the caret stop of its first character, in the face of its run.
        auto semantics = *detail::access(ui).describe("t");
        StyledProbe probe(detail::access(ui), *p, semantics, web_appearance(), 1.f, true);
        detail::component_type(ComponentKind::text).paint(probe);
        unsigned offset = 0;
        bool drawn_where_stops_are = !probe.draws.empty(), faces_right = true;
        for (const auto& draw : probe.draws) {
            const unsigned at = view.line_of(offset);
            drawn_where_stops_are &= draw.x == view.text.x + static_cast<int>(std::lround(view.x_in_line(at, offset)));
            faces_right &= draw.style.bold == (offset >= 3 && offset < 8) && draw.style.italic == (offset >= 27 && offset < 32);
            offset += static_cast<unsigned>(draw.text.size());
        }
        check(drawn_where_stops_are && faces_right, "Every drawn piece sits at its caret stop and keeps its run's face");
        const unsigned last_visible = std::min<unsigned>(view.first + view.visible, static_cast<unsigned>(view.lines.size())) - 1;
        check(offset == view.lines[last_visible].end, "The visible lines are drawn whole: no character dropped, none drawn twice");
        // The underline spans the whole bold run, the added gap inside it included.
        const int line_y = view.row_y(0) + (view.row_height - view.line_height) / 2 + view.line_height * 5 / 6;
        const auto& bold = line.pieces[1];
        const int left = view.text.x + static_cast<int>(std::lround(bold.x)), right = view.text.x + static_cast<int>(std::lround(bold.x + bold.width));
        const auto rules = std::count_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::content && f.rect.h == 1 && f.rect.y == line_y && f.rect.x == left && f.rect.w == right - left; });
        const auto thin = std::count_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::content && f.rect.h == 1 && f.rect.y >= view.text.y; });
        check(rules == 1 && thin == 1, "One underline, from the first to the last segment of the bold run");
        if (method == JustifyMethod::automatic) check(right - left > 51, "It is wider than the 50 px of the bold text: it spans the gap that was added inside it");
        // The selection highlight is drawn from stop to stop.
        const int from = view.text.x + static_cast<int>(std::lround(view.x_in_line(0, 1))), to = view.text.x + static_cast<int>(std::lround(view.x_in_line(0, 4)));
        const auto selected = std::count_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::selection; });
        const auto selection = std::find_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::selection; });
        check(selected == 1 && selection->rect.x == from && selection->rect.w == to - from && to - from > 0, "The selection highlight runs from the stop of the second character to the stop inside the bold run");
        // A click on every drawn stop of the line puts the caret there; the caret is drawn at the same stop.
        for (const auto& [at, x] : line.run.stops) {
            const int px = view.text.x + static_cast<int>(std::lround(line.shift + x));
            check(laid.frame.text_offset(handle, px, view.text.y + 2) == line.begin + at, "A click on a drawn stop lands on its offset");
            check(line.begin + at == line.end || p->caret_x(line.begin + at) == px, "caret_x follows the same stop");
        }
    }
}

void a_double_click_on_a_justified_line_selects_the_word_under_the_pointer() {
    // "ee " is drawn several pixels right of where unstretched text would put it: the click must follow the drawn text.
    for (bool rich : {false, true}) {
        Ui ui;
        auto control = multi("t", words, TextAlign::justify, JustifyMethod::automatic, 4);
        auto handle = rich ? rich_field_of(ui, std::move(control), {{.start = 3, .length = 5, .bold = true, .color = {}}}) : field_of(ui, std::move(control));
        check(bool(handle.set_word_segmentation(WordSegmentation::script_run)), "Words are runs of one script: no system dictionary involved");
        auto& host = detail::access(ui);
        std::uint64_t now = 0;
        host.set_click_clock([&] { return now; });
        host.set_click_settings({500, 4, 4});
        Laid laid(ui, handle, 190);
        const auto* p = laid.frame.find(handle);
        const auto& view = p->view;
        check(view.lines.size() >= 3 && !view.lines[0].segments.empty() && view.lines[0].end >= 15, "The first line holds at least six words and is stretched");
        check(view.x_in_line(0, 12) > 12 * 8.f + 4, "The word really moved right because of the stretching");
        auto double_click_on = [&](unsigned offset, unsigned line_index = 0) {
            const int x = view.text.x + static_cast<int>(std::lround(view.x_in_line(line_index, offset))) + 3, y = view.row_y(line_index) + view.row_height / 2;
            now += 1000;
            host.text_pointer_down("t", detail::placement_text_hit(laid.frame, "t", x, y), detail::placement_text_cell(laid.frame, "t", x, y), x, y, false);
            now += 100;
            host.text_pointer_down("t", detail::placement_text_hit(laid.frame, "t", x, y), detail::placement_text_cell(laid.frame, "t", x, y), x, y, false);
            const auto s = host.edit("t")->selection;
            return TextSpan{std::min(s.anchor, s.caret), std::max(s.anchor, s.caret)};
        };
        check(double_click_on(12) == TextSpan{12, 15}, "A double click on the drawn 'ee' selects ee and the space after it");
        check(double_click_on(9) == TextSpan{9, 12}, "dd, a word to its left");
        check(double_click_on(0) == TextSpan{0, 3}, "aa, at the start of the line");
        const unsigned second = view.lines[1].begin;
        check(double_click_on(second + 6, 1) == TextSpan{second + 6, second + 9}, "A word on the second stretched line");
    }
}

void a_vertical_label_with_justify_or_distribute_reads_from_the_top() {
    const std::u16string text = u"縱書標籤的第一段落，文字夠長而折成數欄。\n第二段";
    for (auto align : {TextAlign::justify, TextAlign::distribute}) {
        Ui ui, plain_ui;
        auto spread = *ui.add_label({.id = "v", .value = text, .text_align = align, .justify_method = JustifyMethod::per_character, .writing_mode = WritingMode::vertical});
        auto left = *plain_ui.add_label({.id = "v", .value = text, .text_align = TextAlign::left, .writing_mode = WritingMode::vertical});
        Laid a(ui, spread, 200), b(plain_ui, left, 200);
        const auto* got = a.frame.find(spread);
        const auto* want = b.frame.find(left);
        check(got && want && got->vertical && got->column_layout.columns.size() >= 2, "A vertical label with justify flows into columns");
        check(got->column_layout.columns.size() == want->column_layout.columns.size() && got->column_layout.pitch == want->column_layout.pitch && got->column_layout.extent == want->column_layout.extent,
              "Same columns as the left-aligned label");
        bool same = true;
        for (std::size_t i = 0; i < got->column_layout.columns.size() && i < want->column_layout.columns.size(); ++i) {
            const auto& g = got->column_layout.columns[i];
            const auto& w = want->column_layout.columns[i];
            same &= g.bounds.x == w.bounds.x && g.bounds.y == w.bounds.y && g.bounds.w == w.bounds.w && g.bounds.h == w.bounds.h && g.begin == w.begin && g.end == w.end && g.pieces.size() == w.pieces.size();
        }
        check(same, "Each column is placed exactly as top alignment places it: justify and distribute read as top");
        check(std::all_of(got->lines.begin(), got->lines.end(), [](const Line& l) { return l.segments.empty(); }), "No horizontal pieces are made");
        // The property still reads as set; run-time changes and the user's setting never break the layout.
        check(*spread.text_align() == align && *spread.justify_method() == JustifyMethod::per_character, "The property reads as set");
        check(bool(spread.set_text_align(TextAlign::left)) && bool(spread.set_text_align(align)), "Switching to and from it works at run time");
        auto settings = ui.presentation();
        settings.justify_as_left = true;
        check(bool(ui.set_presentation(settings)), "The user's setting is on");
        Laid again(ui, spread, 200);
        check(again.frame.find(spread)->column_layout.columns.size() == want->column_layout.columns.size(), "Still the same columns with the setting on");
        auto probe = painted(ui, again.frame, spread, "v");
        check(probe.texts.empty() || !probe.texts.front().text.empty(), "The label paints without error");
    }
}

// ---- Code review 2026-11-12 item 15: what is really drawn ---------------------------------------------------
// A line of n Chinese characters of one em each, stretched to `room` px, has its characters at k * (em + (room - n * em) / (n - 1)):
// worked out here from the glyph width alone. Labels draw each piece at floor(x) (and stop 0.02 px short of the width, so the last
// piece's right edge stays inside the box); fields round it.
void plain_justified_text_is_drawn_at_the_stretched_positions_at_every_scale() {
    const std::u16string chinese = u"一二三四五六七八九十甲乙丙丁";
    int label_pieces = 0, field_pieces = 0;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        {  // a label 130 px wide
            Ui ui;
            auto handle = *ui.add_label(label("l", chinese, TextAlign::justify));
            Laid laid(ui, handle, 130, scale);
            const auto* p = laid.frame.find(handle);
            const int em = p->text_size;
            auto probe = painted(ui, laid.frame, handle, "l");
            std::size_t next = 0;
            for (std::size_t i = 0; i < p->lines.size(); ++i) {
                const auto& line = p->lines[i];
                const int n = static_cast<int>(line.text.size());
                if (line.segments.empty()) {  // the paragraph's last line: one piece at the left
                    check(i + 1 == p->lines.size() && next < probe.texts.size() && probe.texts[next].text == line.text && probe.texts[next].x == line.bounds.x, "The last line is drawn in one piece at the left");
                    ++next; continue;
                }
                const float extra = (130.f - static_cast<float>(n * em)) / static_cast<float>(n - 1);
                for (int k = 0; k < n; ++k, ++next, ++label_pieces) {
                    check(next < probe.texts.size() && probe.texts[next].text == line.text.substr(static_cast<std::size_t>(k), 1), "One draw call per character");
                    if (next >= probe.texts.size()) break;
                    const float ideal = static_cast<float>(k) * (static_cast<float>(em) + extra);
                    check(static_cast<float>(probe.texts[next].x) <= ideal + 0.001f && static_cast<float>(probe.texts[next].x) > ideal - 1.05f, "A label draws a piece at floor of its stretched position");
                }
                const int right_edge = probe.texts[next - 1].x + em;
                check(right_edge <= 130 && right_edge >= 128, "The last piece ends inside the box, within two pixels of its edge");
            }
            check(next == probe.texts.size(), "Nothing else is drawn");
        }
        {  // a multi-line field 190 px wide
            Ui ui;
            auto handle = field_of(ui, multi("t", twenty, TextAlign::justify, JustifyMethod::automatic, 12));  // tall enough to show every line at every scale
            Laid laid(ui, handle, 190, scale);
            const auto* p = laid.frame.find(handle);
            const int em = p->text_size;
            const int wrap = p->view.text.w - std::max(1, static_cast<int>(std::lround(static_cast<float>(inset) * scale)));
            auto probe = painted(ui, laid.frame, handle, "t");
            std::size_t next = 0;
            for (const auto& line : p->view.lines) {
                const int n = static_cast<int>(line.end - line.begin);
                if (line.segments.empty()) {
                    check(next < probe.texts.size() && probe.texts[next].x == p->view.text.x, "The last line is drawn in one piece at the left");
                    ++next; continue;
                }
                const float extra = (static_cast<float>(wrap) - static_cast<float>(n * em)) / static_cast<float>(n - 1);
                for (int k = 0; k < n; ++k, ++next, ++field_pieces) {
                    check(next < probe.texts.size(), "One draw call per character");
                    if (next >= probe.texts.size()) break;
                    const float ideal = static_cast<float>(p->view.text.x) + static_cast<float>(k) * (static_cast<float>(em) + extra);
                    check(std::abs(static_cast<float>(probe.texts[next].x) - ideal) <= 0.6f, "A field draws a piece at its rounded stretched position");
                }
            }
            check(next == probe.texts.size(), "Nothing else is drawn");
        }
    }
    check(label_pieces >= 4 * 8 && field_pieces >= 4 * 8, "The check really looked at stretched pieces at every scale");
}

// An input method composition inside a justified line: the preedit characters take part in the stretched line, so the candidate
// window's anchor (the caret rectangle), the hit test and the stops all sit where the characters are drawn.
void a_composition_on_a_justified_line_is_anchored_at_the_stretched_caret() {
    for (bool rich : {false, true}) {
        Ui ui;
        auto handle = rich ? rich_field_of(ui, multi("t", twenty, TextAlign::justify), {}) : field_of(ui, multi("t", twenty, TextAlign::justify));
        auto& host = detail::access(ui);
        check(bool(handle.focus()) && bool(handle.select(3, 3)), "Focus the field with the caret after the third character");
        check(bool(host.composition_update(u"測", 1)), "Start a composition of one character");
        Laid laid(ui, handle, 190);
        const auto* p = laid.frame.find(handle);
        const auto* session = host.edit("t");
        check(session && session->composing && session->visible_caret() == 4, "The caret is after the preedit character (visible offset 4)");
        // 21 characters now: nine on the first line, 6 px spare over eight gaps, so the stop before character k is 16.75 * k.
        const float wrap = static_cast<float>(p->view.text.w - inset);
        check(near(p->view.x_in_line(0, 9), wrap, 0.05f), "The first line still ends at the wrap edge");
        const Rect anchor = p->caret_rect(session->visible_caret());
        check(anchor.x == p->view.text.x + static_cast<int>(std::lround(4 * 16.75f)), "The candidate anchor is the stretched caret, 67 px in");
        check(p->caret_rect(session->begin()).x == p->view.text.x + static_cast<int>(std::lround(3 * 16.75f)), "The composition starts at the stretched stop before the preedit");
        check(laid.frame.text_offset(handle, anchor.x, anchor.y + 2) == session->visible_caret(), "Hitting the anchor gives the caret back");
        check(anchor.y == p->view.row_y(0) && anchor.h == p->view.row_height, "The anchor is on the first row");
    }
}

// ---- One generic driver: every subject x alignment x method x appearance x density ----------------
// Every kind reports the same rows: where the visible text of each display line starts and ends, measured
// from the left edge of the area the alignment works in, and which rows end their paragraph.
struct Row { bool last; float left, right; std::vector<float> extras; bool stops_match = true; };  // extras: the space added after each piece but the last
struct Rows { std::vector<Row> rows; float area = 0; };
struct Subject {
    const char* name;
    bool field;
    Rows (*lay)(Ui&, TextAlign, JustifyMethod, const std::u16string&, const Appearance&);
};
// The space between one piece and the next, worked out from the fake backend's own glyph widths (not from TextSegment::width).
std::vector<float> extras_of(const std::u16string& line, const std::vector<TextSegment>& segments) {
    FakeMeasure measure;
    std::vector<float> extras;
    for (std::size_t j = 0; j + 1 < segments.size(); ++j)
        extras.push_back(segments[j + 1].x - (segments[j].x + measure.measure(line.substr(segments[j].begin, segments[j].end - segments[j].begin), 16).width));
    return extras;
}
float content_right(const std::u16string& line, const std::vector<TextSegment>& segments, float origin) {
    FakeMeasure measure;
    const auto content_end = static_cast<unsigned>(line.find_last_not_of(u' ') + 1);
    float right = origin;
    for (const auto& s : segments)
        if (s.begin < content_end) right = origin + s.x + measure.measure(line.substr(s.begin, std::min(s.end, content_end) - s.begin), 16).width;
    return right;
}
Rows label_rows(Ui& ui, const LabelHandle& handle, const std::u16string& text, bool scrollable, const Appearance& look) {
    FakeMeasure measure; ScrollState outer, inner;
    auto node = scrollable ? layout::scroll(layout::column({layout::scroll_text(handle, inner, 400)})) : layout::component(handle);
    auto frame = arrange(node, ui, look, measure, 300, 400, 1.f, outer);
    const auto* p = frame.find(handle);
    Rows out; out.area = static_cast<float>(p->bounds.w);
    const auto pieces = wrap_marked(measure, text, 16, p->bounds.w);
    for (std::size_t i = 0; i < p->lines.size(); ++i) {
        const auto& line = p->lines[i];
        const float origin = static_cast<float>(line.bounds.x - p->bounds.x);
        const float left = line.segments.empty() ? origin : origin + line.segments.front().x;
        const float right = line.segments.empty() ? origin + measure.measure(std::u16string(line.text).substr(0, line.text.find_last_not_of(u' ') + 1), 16).width
                                                  : content_right(line.text, line.segments, origin);
        out.rows.push_back({!pieces[i].soft, left, right, extras_of(line.text, line.segments)});
    }
    return out;
}
Rows field_rows(Ui& ui, const TextHandle& handle, const std::u16string& text, const Appearance& look) {
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::component(handle), ui, look, measure, 300, 400, 1.f, scroll);
    const auto* p = frame.find(handle);
    Rows out;
    if (!p->multiline) {  // one line, the end of its paragraph
        out.area = static_cast<float>(p->field.editor.w);
        const float origin = p->shift;
        out.rows.push_back({true, origin, origin + p->run.x(static_cast<unsigned>(text.find_last_not_of(u' ') + 1))});
        return out;
    }
    out.area = static_cast<float>(p->view.text.w - look.component_presentation(ui.presentation().density).inset);
    for (unsigned i = 0; i < p->view.lines.size(); ++i) {
        // Positions come from the pieces and the fake backend's glyph widths, not from the stops being compared with themselves.
        const auto& line = p->view.lines[i];
        const auto visible = text.substr(line.begin, line.end - line.begin);
        const unsigned content_end = static_cast<unsigned>(visible.find_last_not_of(u' ') + 1);
        const float origin = line.shift;
        const float left = line.segments.empty() ? origin : origin + line.segments.front().x;
        const float right = line.segments.empty() ? origin + measure.measure(visible.substr(0, content_end), 16).width : content_right(visible, line.segments, origin);
        Row row{!line.soft, left, right, extras_of(visible, line.segments)};
        // The caret stops, the hit test and the pieces must tell the same story.
        for (const auto& segment : line.segments)
            row.stops_match &= near(p->view.x_in_line(i, line.begin + segment.begin), origin + segment.x, 0.05f);
        out.rows.push_back(std::move(row));
    }
    return out;
}
const Subject subjects[] = {
    {"label", false, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        return label_rows(ui, *ui.add_label(label("subject", t, a, m)), t, false, l); }},
    {"scrollable label", false, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        return label_rows(ui, *ui.add_label(label("subject", t, a, m)), t, true, l); }},
    {"single-line field", true, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        auto one = t; std::replace(one.begin(), one.end(), u'\n', u' ');  // a single line holds no line break
        return field_rows(ui, field_of(ui, [&] { auto c = single("subject", one, a); c.justify_method = m; return c; }()), one, l); }},
    {"multi-line field", true, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        return field_rows(ui, field_of(ui, multi("subject", t, a, m, 6)), t, l); }},
};
// Every line of both paragraphs has a space or Chinese text to stretch by, whatever the method asks for.
const std::u16string english = u"aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz\nlast paragraph of the text here";
const std::u16string mixed = u"中文 text 混排 sample 的 句子 要 對齊 兩 端 好 看 一 些 嗎 這 樣 才 能 比 較 清 楚\n第二段 也 要 對 齊 看 看 結 果";

void every_subject_stretches_every_non_last_line_flush_in_every_combination() {
    int stretched_lines = 0;
    for (const auto& subject : subjects) for (const auto* look : {&web_appearance(), &material_appearance()}) for (auto density : {Density::standard, Density::compact})
    for (auto align : {TextAlign::justify, TextAlign::distribute}) for (auto method : {JustifyMethod::automatic, JustifyMethod::spaces, JustifyMethod::per_character}) {
        if (subject.field && align == TextAlign::distribute) continue;  // a field refuses it (covered above)
        for (const auto* text : {&english, &mixed}) {
            Ui ui;
            check(bool(ui.set_presentation({.density = density})), "Set the density");
            Rows laid;
            try { laid = subject.lay(ui, align, method, *text, *look); }
            catch (const std::exception& e) { throw std::runtime_error(std::string(subject.name) + " " + std::to_string(static_cast<int>(align)) + "/" + std::to_string(static_cast<int>(method)) + ": " + e.what()); }
            check(!laid.rows.empty(), subject.name);
            // The same corpus laid out left-aligned tells which rows end a paragraph and how wide they naturally are.
            for (const auto& row : laid.rows) {
                const bool stretch = align == TextAlign::distribute || !row.last;
                if (std::string_view(subject.name) == "single-line field") { check(row.last && near(row.left, 0), subject.name); continue; }
                if (stretch) {
                    ++stretched_lines;
                    check(near(row.left, 0) && near(row.right, laid.area), subject.name);
                    // Even distribution: every added gap is the same size (within a hair), and there is at least one.
                    float smallest = 1e9f, largest = -1e9f; int gaps = 0;
                    for (float extra : row.extras) if (extra > 0.01f) { smallest = std::min(smallest, extra); largest = std::max(largest, extra); ++gaps; }
                    // (A line whose text already fills the width has no pieces and nothing to share.)
                    check(row.extras.empty() || (gaps > 0 && largest - smallest <= 0.05f), (std::string(subject.name) + ": the extra space is shared equally").c_str());
                    check(row.stops_match, (std::string(subject.name) + ": caret stops follow the drawn pieces").c_str());
                } else {
                    check(near(row.left, 0) && row.right < laid.area - 1, subject.name);
                }
            }
        }
    }
    check(stretched_lines > 200, "The driver really exercised stretched lines");
}

int main() {
    const test::Case cases[] = {
        {"a justified label spreads Chinese evenly between characters", a_justified_label_spreads_chinese_evenly_between_the_characters},
        {"the automatic method keeps English words whole and widens spaces", the_automatic_method_keeps_english_words_whole_and_widens_spaces},
        {"full-width punctuation keeps its width and takes a share", full_width_punctuation_keeps_its_width_and_takes_a_share_like_a_character},
        {"the spaces method only widens spaces", the_spaces_method_only_widens_spaces_and_a_chinese_line_stays_left},
        {"nowhere to stretch stays left; trailing spaces get nothing", a_line_with_nowhere_to_stretch_stays_left_and_trailing_spaces_get_nothing},
        {"the per-character method spreads Latin letters, no ligature", the_per_character_method_spreads_latin_letters_and_never_draws_a_ligature},
        {"per-character cuts inside a ligature cluster, the others do not", the_per_character_method_cuts_inside_a_ligature_cluster_and_the_others_do_not},
        {"per-character keeps kerning and cuts at clusters", the_per_character_method_keeps_kerning_and_cuts_at_clusters},
        {"distribute stretches the last line and a single line", the_distribute_alignment_stretches_the_last_line_and_a_single_line},
        {"a multi-code-point character is never cut", a_character_made_of_several_code_points_is_never_cut},
        {"a justified multi-line field stretches wrapped lines to the wrap edge", a_justified_multi_line_field_stretches_each_wrapped_line_to_the_wrap_edge},
        {"clicks and drags land on the drawn characters", clicks_and_drags_land_on_the_drawn_characters_of_a_justified_line},
        {"vertical keys and the input method follow the stretched positions", vertical_keys_and_the_input_method_follow_the_stretched_positions},
        {"a justified single-line field looks left-aligned", a_justified_single_line_field_looks_left_aligned},
        {"a field refuses distribute and a label accepts it", a_field_refuses_distribute_and_a_label_accepts_it},
        {"the justification method is readable and settable everywhere", the_justification_method_is_readable_and_settable_everywhere},
        {"a run-time change restretches the next frame", a_run_time_change_restretches_the_next_frame},
        {"the user setting draws stretched text left, keeping the properties", the_user_setting_draws_stretched_text_left_but_keeps_the_properties},
        {"a justified rich line keeps caret, selection, hits, glyphs and underline together", a_justified_rich_line_keeps_caret_selection_hits_glyphs_and_underline_together},
        {"a double click on a justified line selects the word under the pointer", a_double_click_on_a_justified_line_selects_the_word_under_the_pointer},
        {"a vertical label with justify or distribute reads from the top", a_vertical_label_with_justify_or_distribute_reads_from_the_top},
        {"plain justified text is drawn at the stretched positions at every scale", plain_justified_text_is_drawn_at_the_stretched_positions_at_every_scale},
        {"a composition on a justified line is anchored at the stretched caret", a_composition_on_a_justified_line_is_anchored_at_the_stretched_caret},
        {"every subject stretches every non-last line flush in every combination", every_subject_stretches_every_non_last_line_flush_in_every_combination},
    };
    return test::run("justify", cases);
}
