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
    template<class Handle> Laid(Ui& ui, const Handle& handle, int width = 100, float scale = 1.f, const Appearance& appearance = webAppearance()) {
        frame = arrange(layout::component(handle), ui, appearance, measure, width, 400, scale, scroll);
    }
};
ComponentConfig label(std::string id, std::u16string value, TextAlign align, JustifyMethod method = JustifyMethod::automatic) {
    return {.id = std::move(id), .value = std::move(value), .textAlign = align, .justifyMethod = method};
}
// What a drawn line looks like: where its pieces start and end. A line that is not justified is one piece.
struct Edges { float left = 0, right = 0; std::size_t pieces = 0; };
Edges edgesOf(const Line& line, int pixels = 16) {
    FakeMeasure measure;
    if (line.segments.empty()) return {static_cast<float>(line.bounds.x), line.bounds.x + measure.measure(line.text, pixels).width, 1};
    const auto& last = line.segments.back();
    const auto tail = std::u16string_view(line.text).substr(last.begin, last.end - last.begin);
    return {line.segments.front().x, last.x + measure.measure(tail, pixels).width, line.segments.size()};
}
bool near(float a, float b, float tolerance = 1.f) { return std::abs(a - b) <= tolerance; }

// 14 wide characters in a 100 px label: 6 + 6 + 2 per line (a character is 16 px).
void aJustifiedLabelSpreadsChineseEvenlyBetweenTheCharacters() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"一二三四五六七八九十甲乙丙丁", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 3, "Three lines");
    // 96 px of text in 100 px: 4 px over five gaps, 0.8 px each.
    const auto& first = p->lines[0];
    check(first.segments.size() == 6, "Every character is its own piece");
    for (std::size_t i = 0; i < first.segments.size(); ++i)
        check(near(first.segments[i].x, 16.8f * static_cast<float>(i), 0.05f), "Pieces are 16.8 px apart: the character plus an equal share");
    const auto edges = edgesOf(first);
    check(near(edges.left, 0) && near(edges.right, 100), "Both ends are flush with the label");
    check(near(edgesOf(p->lines[1]).left, 0) && near(edgesOf(p->lines[1]).right, 100), "So is the second line");
    check(p->lines[2].segments.empty() && p->lines[2].bounds.x == 0, "The last line of the paragraph stays left-aligned");
}

struct Piece { unsigned begin, end; float x; };
std::vector<Piece> piecesOf(const Line& line) {
    std::vector<Piece> out;
    for (const auto& s : line.segments) out.push_back({s.begin, s.end, s.x});
    return out;
}
bool samePieces(const std::vector<Piece>& got, const std::vector<Piece>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i].begin != want[i].begin || got[i].end != want[i].end || !near(got[i].x, want[i].x, 0.05f)) return false;
    return true;
}

// "我愛 cat 中" is 88 px in a 100 px label; the 12 px spare goes to three places, 4 px each: after 我, after
// the first space, after the second space. "cat" itself and the space after 愛 are not touched.
void theAutomaticMethodKeepsEnglishWordsWholeAndWidensSpaces() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"我愛 cat 中文字", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 2 && p->lines[0].text == u"我愛 cat 中", "The paragraph wraps after 中");
    check(samePieces(piecesOf(p->lines[0]), {{0, 1, 0}, {1, 3, 20}, {3, 7, 48}, {7, 8, 84}}), "Pieces: 我 | 愛_ | cat_ | 中, 4 px added after each of the first three");
    check(near(edgesOf(p->lines[0]).right, 100), "Flush right");
    check(p->lines[1].segments.empty(), "The last line is left-aligned");
}

void fullWidthPunctuationKeepsItsWidthAndTakesAShareLikeACharacter() {
    Ui ui;
    // 一，二，三，四，五，六，七 : 13 characters of 16 px; a 100 px label holds six per line.
    auto handle = *ui.addLabel(label("l", u"一，二，三，四，五，六，七", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 3 && p->lines[0].text == u"一，二，三，", "Six characters per line");
    // 96 px in 100: 4 px over five gaps. Each mark keeps one 16 px cell and is spaced exactly like a character.
    check(samePieces(piecesOf(p->lines[0]), {{0, 1, 0}, {1, 2, 16.8f}, {2, 3, 33.6f}, {3, 4, 50.4f}, {4, 5, 67.2f}, {5, 6, 84}}),
          "Characters and marks sit 16.8 px apart");
}

void theSpacesMethodOnlyWidensSpacesAndAChineseLineStaysLeft() {
    {
        Ui ui;
        auto handle = *ui.addLabel(label("l", u"一二三四五六七八九十甲乙丙丁", TextAlign::justify, JustifyMethod::spaces));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p && p->lines.size() == 3 && p->lines[0].segments.empty() && p->lines[0].bounds.x == 0, "A line without spaces is not stretched");
    }
    Ui ui;
    // A 100 px label holds 12 characters of 8 px: "aa bb cc dd " breaks after the space. Its content "aa bb cc dd"
    // is 88 px, so 12 px go to the three spaces inside it, 4 px each; the trailing space gets nothing.
    auto handle = *ui.addLabel(label("l", u"aa bb cc dd ee ff", TextAlign::justify, JustifyMethod::spaces));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines.size() == 2 && p->lines[0].text == u"aa bb cc dd ", "One wrapped line, ending in its space");
    check(samePieces(piecesOf(p->lines[0]), {{0, 3, 0}, {3, 6, 28}, {6, 9, 56}, {9, 11, 84}, {11, 12, 100}}), "Only the spaces inside the content are widened");
}

void aLineWithNowhereToStretchStaysLeftAndTrailingSpacesGetNothing() {
    Ui ui;
    // One long word: no gap in either automatic or spaces mode. "abcdefghijkl" is 96 px, then more text wraps it.
    for (auto method : {JustifyMethod::automatic, JustifyMethod::spaces}) {
        auto handle = *ui.addLabel(label(method == JustifyMethod::automatic ? "a" : "s", u"abcdefghijklmnopqrstuvwxyz", TextAlign::justify, method));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p && p->lines.size() == 3 && p->lines[0].segments.empty() && p->lines[0].bounds.x == 0, "A word alone on a line stays left-aligned");
    }
    // The line "一二三四五" + two spaces: the trailing spaces are neither part of the width nor given a share, so the
    // five characters (80 px) spread over the 100 px with 5 px per gap, and the spaces follow at the edge.
    auto handle = *ui.addLabel(label("t", u"一二三四五    六", TextAlign::justify));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p && p->lines[0].text == u"一二三四五  ", "The line keeps two trailing spaces");
    check(samePieces(piecesOf(p->lines[0]), {{0, 1, 0}, {1, 2, 21}, {2, 3, 42}, {3, 4, 63}, {4, 5, 84}, {5, 7, 100}}),
          "The visible text reaches the right edge; the spaces start there");
}

void thePerCharacterMethodSpreadsLatinLettersAndNeverDrawsALigature() {
    // "difficult " is 80 px with its space; the nine letters are 72 px. Spread to 100: 28 px over eight gaps, 3.5 each.
    Ui ui;
    auto perCharacter = *ui.addLabel(label("p", u"difficult fiasco", TextAlign::justify, JustifyMethod::perCharacter));
    auto automatic = *ui.addLabel(label("a", u"difficult fiasco", TextAlign::justify, JustifyMethod::automatic));
    {
        Laid laid(ui, perCharacter);
        const auto* p = laid.frame.find(perCharacter);
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
    TextRun measureApart(std::u16string_view text, int pixels, FontStyle) override { return base.measure(text, pixels); }
    int lineHeight(int pixels) override { return base.lineHeight(pixels); }
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
    int lineHeight(int pixels) override { return pixels * 3 / 2; }
};
// Code review 2026-11-12 item 10: the per-character method measures the line once (ligatures off, kerning on) and
// takes the piece positions from that run, cutting at its clusters; it does not measure each letter alone.
void thePerCharacterMethodKeepsKerningAndCutsAtClusters() {
    KernMeasure measure;
    const std::u16string kerned = u"AVA";
    const auto natural = measure.measure(kerned, 16);  // A 4 (kerned) + V 8 + A 8 = 20
    auto stretched = detail::justifyLine(measure, kerned, 16, natural, 60.f, JustifyMethod::perCharacter);
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
    auto split = detail::justifyLine(measure, clustered, 16, run, 100.f, JustifyMethod::perCharacter);
    check(split && split->segments.size() == 2, "Two clusters, two pieces");
    if (split) for (const auto& s : split->segments) check(s.begin != 1 && s.end != 1, "No cut inside the two-code-point cluster");
}

void thePerCharacterMethodCutsInsideALigatureClusterAndTheOthersDoNot() {
    Ui ui;
    auto perCharacter = *ui.addLabel(label("p", u"office is open now", TextAlign::justify, JustifyMethod::perCharacter));
    auto automatic = *ui.addLabel(label("a", u"office is open now", TextAlign::justify, JustifyMethod::automatic));
    LigatureMeasure measure; ScrollState scroll;
    auto first = arrange(layout::component(perCharacter), ui, webAppearance(), measure, 100, 400, 1.f, scroll);
    const auto& line = first.find(perCharacter)->lines[0];
    check(line.text == u"office is " && std::count_if(line.segments.begin(), line.segments.end(), [&](const TextSegment& s) { return s.end - s.begin == 1; }) >= 8,
          "Every letter is a piece of its own, though the shaper clusters ffi");
    for (const auto& s : line.segments) check(line.text.substr(s.begin, s.end - s.begin).find(u"fi") == std::u16string::npos, "No piece can form the ligature");
    auto second = arrange(layout::component(automatic), ui, webAppearance(), measure, 100, 400, 1.f, scroll);
    const auto& plain = second.find(automatic)->lines[0];
    check(!plain.segments.empty() && plain.segments[0].end == 7, "The automatic method keeps the word, ligature and all, in one piece");
}

void theDistributeAlignmentStretchesTheLastLineAndASingleLine() {
    Ui ui;
    auto name = *ui.addLabel(label("n", u"姓名", TextAlign::distribute));
    auto spaced = *ui.addLabel(label("s", u"姓　名", TextAlign::distribute));
    auto plain = *ui.addLabel(label("j", u"姓名", TextAlign::justify));
    auto one = *ui.addLabel(label("o", u"姓", TextAlign::distribute));
    {
        Laid laid(ui, name);
        // 32 px of text in 100: the 68 px spare goes to the single gap.
        check(samePieces(piecesOf(laid.frame.find(name)->lines[0]), {{0, 1, 0}, {1, 2, 84}}), "Two characters sit at the two ends");
    }
    {
        Laid laid(ui, spaced);
        // Code review item 11: U+3000 is a CJK character like any other (clreq: gaps on both sides of every ideograph and
        // full-width punctuation). 姓, U+3000 and 名 are 48 px; the 52 px spare goes to the two gaps, 25.99 px each.
        check(samePieces(piecesOf(laid.frame.find(spaced)->lines[0]), {{0, 1, 0}, {1, 2, 41.99f}, {2, 3, 83.98f}}), "A full-width space is a piece with a gap on each side");
    }
    {
        Laid laid(ui, plain);
        check(laid.frame.find(plain)->lines[0].segments.empty(), "Justify leaves a one-line text left-aligned");
    }
    Laid laid(ui, one);
    check(laid.frame.find(one)->lines[0].segments.empty(), "A single character has no gap, so it stays left");
}

void aCharacterMadeOfSeveralCodePointsIsNeverCut() {
    Ui ui;
    // 一 + a variation selector, then five more characters: the selector is a unit of its own for the fake
    // backend, but a cut between it and its base is never made.
    auto selector = *ui.addLabel(label("v", u"一︀二三四五六七八九", TextAlign::justify, JustifyMethod::perCharacter));
    {
        Laid laid(ui, selector);
        const auto* p = laid.frame.find(selector);
        check(p && p->lines[0].segments.size() > 1, "The line is stretched");
        for (const auto& s : p->lines[0].segments) check(s.begin != 1 && s.end != 1, "No cut between a character and its variation selector");
        check(samePieces({piecesOf(p->lines[0])[0]}, {{0, 2, 0}}), "The first piece is the character with its selector");
    }
    // A ZWJ emoji sequence: woman (a surrogate pair), joiner, laptop (a pair), offsets 2..6.
    auto emoji = *ui.addLabel(label("e", u"一二\U0001F469‍\U0001F4BB三四五六七八九", TextAlign::justify, JustifyMethod::perCharacter));
    Laid laid(ui, emoji);
    const auto* p = laid.frame.find(emoji);
    check(p && p->lines[0].segments.size() > 1, "The emoji line is stretched");
    for (const auto& s : p->lines[0].segments)
        for (unsigned inside : {3u, 4u, 5u, 6u}) check(s.begin != inside && s.end != inside, "The emoji sequence stays in one piece");
}
}

std::size_t laidSegments(Ui& ui, const LabelHandle& handle) {
    Laid laid(ui, handle);
    return laid.frame.find(handle)->lines[0].segments.size();
}

// ---- Text fields ----------------------------------------------------------------------------------
TextFieldHandle fieldOf(Ui& ui, ComponentConfig control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto field = *ui.addTextField(std::move(control));
    check(bool(field.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; })), "Connect a write");
    return field;
}
ComponentConfig multi(std::string id, std::u16string value, TextAlign align, JustifyMethod method = JustifyMethod::automatic, unsigned lines = 3) {
    return {.id = std::move(id), .value = std::move(value), .multiline = true, .visibleLines = lines, .textAlign = align, .justifyMethod = method};
}
ComponentConfig single(std::string id, std::u16string value, TextAlign align) {
    return {.id = std::move(id), .value = std::move(value), .textAlign = align};
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
    TextProbe probe(detail::access(ui), *item, semantics, webAppearance(), 1.f, true);
    detail::componentType(item->kind).paint(probe);
    return probe;
}
// 20 wide characters in a field 190 px wide: the text area is 152 px, lines wrap at 150, so nine fit (144 px).
const std::u16string twenty = u"一二三四五六七八九十甲乙丙丁戊己庚辛壬癸";
const int inset = webAppearance().componentPresentation(Density::standard).inset;

void aJustifiedMultiLineFieldStretchesEachWrappedLineToTheWrapEdge() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", twenty, TextAlign::justify));
    Laid laid(ui, handle, 190);
    const auto* p = laid.frame.find(handle);
    check(p && p->multiline && p->view.lines.size() == 3, "9 + 9 + 2 characters");
    const float wrap = static_cast<float>(p->view.text.w - inset);
    // 6 px spare over eight gaps: 0.75 px per gap, so the caret stop before character k is 16.75 * k.
    for (unsigned k : {0u, 1u, 3u, 8u}) check(near(p->view.xInLine(0, k), 16.75f * static_cast<float>(k), 0.05f), "Stops follow the stretched characters");
    check(near(p->view.xInLine(0, 9), wrap, 0.05f) && near(p->view.xInLine(1, 18), wrap, 0.05f), "The first two lines end at the wrap edge");
    check(near(p->view.xInLine(1, 12), 3 * 16.75f, 0.05f), "The second line is stretched the same way");
    check(p->view.lines[2].segments.empty() && near(p->view.xInLine(2, 20), 32, 0.05f), "The paragraph's last line is as wide as its text, at the left");
    // The caret is drawn where the stops put it, and a wrap point belongs to the next line.
    check(p->caretX(3) == p->view.text.x + static_cast<int>(std::lround(50.25f)) && p->caretX(9) == p->view.text.x, "caret_x follows the stretched stops");
}

void clicksAndDragsLandOnTheDrawnCharactersOfAJustifiedLine() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", twenty, TextAlign::justify));
    Laid laid(ui, handle, 190);
    const auto* p = laid.frame.find(handle);
    const int x = p->view.text.x, y0 = p->view.text.y + 5, y1 = y0 + 24;
    // Stops of line 0 at 0, 16.75, 33.5, 50.25, 67, ...: the nearest wins.
    check(laid.frame.textOffset(handle, x + 8, y0) == 0 && laid.frame.textOffset(handle, x + 9, y0) == 1, "Halfway between the first two stops");
    check(laid.frame.textOffset(handle, x + 50, y0) == 3 && laid.frame.textOffset(handle, x + 60, y0) == 4, "A click near a stop goes to it");
    check(laid.frame.textOffset(handle, x + 140, y0) == 8 && laid.frame.textOffset(handle, x + 149, y0) == 9, "Near the right edge: the end of the line");
    check(laid.frame.textOffset(handle, x + 50, y1) == 12, "Line 1 maps the same way");
    auto& host = detail::access(ui);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 0, 0);
    check(laid.frame.selectTo(ui, handle, x + 52, y1) && host.edit("t")->selection.anchor == 0 && host.edit("t")->selection.caret == 12, "A drag into the second line selects up to the character under the pointer");
    // The selection highlight is drawn from stop to stop: line 0 whole, line 1 up to 50.25 px.
    Laid after(ui, handle, 190);
    auto probe = painted(ui, after.frame, handle, "t");
    check(probe.selections.size() == 2 && probe.selections[0].x == x && near(static_cast<float>(probe.selections[0].w), 150, 1) && near(static_cast<float>(probe.selections[1].w), 50.25f, 1), "The highlight follows the stretched stops");
}

void verticalKeysAndTheInputMethodFollowTheStretchedPositions() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", twenty, TextAlign::justify));
    auto& host = detail::access(ui);
    Laid first(ui, handle, 190);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 20, 20);  // the end of the short last line, 32 px from the left
    Laid start(ui, handle, 190);
    check(bool(host.keyDown({.key = Key::up})), "Up");
    // The stretched line's stops: 2 * 16.75 = 33.5 is the nearest to 32, so the caret lands before the third character.
    Laid above(ui, handle, 190);
    check(host.edit("t")->selection.caret == 11, "Up keeps the drawn x: the nearest stop of the stretched line");
    check(bool(host.keyDown({.key = Key::down})), "Down");
    Laid below(ui, handle, 190);
    // From stop 33.5 down to the last line, whose stops are 0, 16, 32: the nearest is 32, the end.
    check(host.edit("t")->selection.caret == 20, "And back to the end of the last line");
    // The input method anchors at the caret as drawn.
    host.select("t", 3, 3);
    Laid laid(ui, handle, 190);
    const auto* p = laid.frame.find(handle);
    check(p->caretRect(3).x == p->caretX(3) && p->caretRect(3).x == p->view.text.x + 50, "The input method's anchor is the stretched caret");
}

void aJustifiedSingleLineFieldLooksLeftAligned() {
    Ui plainUi, justifiedUi;
    auto plain = fieldOf(plainUi, single("t", u"一二三四五", TextAlign::left));
    auto justified = fieldOf(justifiedUi, single("t", u"一二三四五", TextAlign::justify));
    Laid a(plainUi, plain, 200), b(justifiedUi, justified, 200);
    const auto *pa = a.frame.find(plain), *pb = b.frame.find(justified);
    check(pb->shift == 0 && pa->shift == 0, "No shift");
    for (unsigned k = 0; k <= 5; ++k) check(pa->caretX(k) == pb->caretX(k), "The same caret stops as a left-aligned field");
    check(painted(justifiedUi, b.frame, justified, "t").texts.size() == painted(plainUi, a.frame, plain, "t").texts.size(), "Drawn in one piece, as before");
    check(*justified.textAlign() == TextAlign::justify, "The property still reads justify");
}

void aFieldRefusesDistributeAndALabelAcceptsIt() {
    Ui ui;
    check(ui.addTextField(single("bad", u"x", TextAlign::distribute)).status == Status::invalidValue, "A single-line field is not created distributed");
    check(ui.addTextField(multi("bad", u"x", TextAlign::distribute)).status == Status::invalidValue, "Nor a multi-line one");
    check(ui.addButton({.id = "b", .textAlign = TextAlign::justify}).status == Status::invalidValue, "A button has no alignment at all");
    check(ui.addButton({.id = "b2", .justifyMethod = JustifyMethod::spaces}).status == Status::invalidValue, "Nor a justification method");
    check(ui.addLabel(label("bad", u"x", TextAlign::left, static_cast<JustifyMethod>(9))).status == Status::invalidValue, "An unknown method is invalid at creation");
    auto field = fieldOf(ui, multi("t", u"abc", TextAlign::center, JustifyMethod::spaces));
    auto name = *ui.addLabel(label("l", u"abc", TextAlign::distribute));
    check(*name.textAlign() == TextAlign::distribute, "A label can be distributed");
    check(field.setTextAlign(TextAlign::distribute).status == Status::invalidValue && *field.textAlign() == TextAlign::center, "The handle refuses and leaves the field unchanged");
    auto& tree = ui.semantics();
    check(tree.perform({"t"}, Verb::setTextAlign, TextAlign::distribute).status == Status::invalidValue && *field.textAlign() == TextAlign::center, "So does the semantic layer, with the same status");
    check(bool(field.setTextAlign(TextAlign::justify)) && *field.textAlign() == TextAlign::justify, "justify is fine");
    check(field.setTextAlign(static_cast<TextAlign>(9)).status == Status::invalidValue && name.setTextAlign(static_cast<TextAlign>(9)).status == Status::invalidValue, "An unknown alignment is invalid");
}

void theJustificationMethodIsReadableAndSettableEverywhere() {
    Ui ui;
    auto name = *ui.addLabel(label("l", u"一二三", TextAlign::justify));
    auto field = fieldOf(ui, multi("t", u"abc", TextAlign::justify, JustifyMethod::spaces));
    auto button = *ui.addButton({.id = "b"});
    check(*name.justifyMethod() == JustifyMethod::automatic && *field.justifyMethod() == JustifyMethod::spaces, "Defaults and creation values");
    for (auto method : {JustifyMethod::perCharacter, JustifyMethod::spaces, JustifyMethod::automatic}) {
        check(bool(name.setJustifyMethod(method)) && *name.justifyMethod() == method, "A label takes every method");
        check(bool(field.setJustifyMethod(method)) && *field.justifyMethod() == method, "So does a field");
    }
    check(name.setJustifyMethod(static_cast<JustifyMethod>(7)).status == Status::invalidValue && *name.justifyMethod() == JustifyMethod::automatic, "An unknown method is invalid and changes nothing");
    check(field.describe()->justifyMethod == JustifyMethod::automatic, "describe reports it");
    auto& tree = ui.semantics();
    auto node = tree.get({"l"});
    check(node && node->justifyMethod == JustifyMethod::automatic && node->textAlign == TextAlign::justify, "The semantic node reports alignment and method");
    check(std::find(node->verbs.begin(), node->verbs.end(), Verb::setJustifyMethod) != node->verbs.end(), "The verb is offered");
    const auto buttonNode = *tree.get({"b"});
    check(std::find(buttonNode.verbs.begin(), buttonNode.verbs.end(), Verb::setJustifyMethod) == buttonNode.verbs.end(), "A button is not offered it");
    check(tree.perform({"b"}, Verb::setJustifyMethod, JustifyMethod::spaces).status == Status::notSupported, "A button refuses it");
    std::vector<std::string> changed;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::propertyChanged) changed.push_back(e.property); });
    check(bool(tree.perform({"l"}, Verb::setJustifyMethod, JustifyMethod::perCharacter)) && *name.justifyMethod() == JustifyMethod::perCharacter, "The semantic write reaches the typed read");
    check(bool(field.setJustifyMethod(JustifyMethod::spaces)) && tree.get({"t"})->justifyMethod == JustifyMethod::spaces, "And the typed write reaches the semantic read");
    tree.dispatchEvents();
    check(std::count(changed.begin(), changed.end(), "justify_method") >= 1, "A change raises property_changed");
    check(tree.perform({"l"}, Verb::setJustifyMethod, static_cast<JustifyMethod>(7)).status == name.setJustifyMethod(static_cast<JustifyMethod>(7)).status, "The same status for an unknown method");
    check(tree.perform({"l"}, Verb::setJustifyMethod, 1).status == Status::invalidValue, "The wrong argument type is invalid");
    check(tree.perform({"l"}, Verb::setTextAlign, TextAlign::distribute).status == Status::ok && tree.get({"l"})->textAlign == TextAlign::distribute, "The semantic layer reads and writes both new alignments");
    check(tree.perform({"missing"}, Verb::setJustifyMethod, JustifyMethod::spaces).status == Status::notFound, "An unknown component is not found");
}

void aRunTimeChangeRestretchesTheNextFrame() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"一二三四五六七八九十", TextAlign::left));
    check(laidSegments(ui, handle) == 0, "Left at first");
    check(bool(handle.setTextAlign(TextAlign::justify)) && laidSegments(ui, handle) == 6, "The next arrange is stretched: six pieces on the first line");
    check(bool(handle.setJustifyMethod(JustifyMethod::spaces)) && laidSegments(ui, handle) == 0, "A method with no spaces to use leaves it left");
    check(bool(handle.setJustifyMethod(JustifyMethod::perCharacter)) && laidSegments(ui, handle) == 6, "And back");
}

void theUserSettingDrawsStretchedTextLeftButKeepsTheProperties() {
    Ui ui;
    auto name = *ui.addLabel(label("l", twenty, TextAlign::justify));
    auto spread = *ui.addLabel(label("d", u"姓名", TextAlign::distribute));
    auto field = fieldOf(ui, multi("t", twenty, TextAlign::justify));
    check(laidSegments(ui, name) == 6, "Stretched by default");
    auto settings = ui.presentation();
    settings.justifyAsLeft = true;
    check(bool(ui.setPresentation(settings)), "Turn the setting on");
    check(laidSegments(ui, name) == 0 && laidSegments(ui, spread) == 0, "Labels draw left-aligned");
    {
        Laid laid(ui, field, 190);
        const auto* p = laid.frame.find(field);
        check(p->view.lines[0].segments.empty() && near(p->view.xInLine(0, 9), 144, 0.05f) && p->caretX(3) == p->view.text.x + 48, "A field draws left-aligned: stops are the natural ones");
        auto probe = painted(ui, laid.frame, field, "t");
        check(probe.texts.size() == 3 && probe.texts[0].text.size() == 9, "Each line is drawn in one piece");
    }
    check(*name.textAlign() == TextAlign::justify && *spread.textAlign() == TextAlign::distribute && *field.textAlign() == TextAlign::justify, "The property values are unchanged");
    check(ui.semantics().get({"l"})->textAlign == TextAlign::justify && ui.semantics().get({"d"})->textAlign == TextAlign::distribute, "So are the semantic reads");
    settings.justifyAsLeft = false;
    check(bool(ui.setPresentation(settings)) && laidSegments(ui, name) == 6, "Turning it off stretches again");
    check(laidSegments(ui, spread) == 2, "A distributed label too");
}

// ---- #81 x #84: justified rich text -----------------------------------------------------------------
// FakeMeasure at 16 px: an ASCII character is 8 px, a bold one 10 px. The words are two letters, so with the
// spans below "bb cc" is bold and underlined (10 + 10 + 10 + 10 + 10 = 50 px) and the space after it is plain.
const std::u16string words = u"aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp";
const std::vector<StyleSpan> richSpans = {{.start = 3, .length = 5, .bold = true, .underline = true, .color = {}}, {.start = 27, .length = 5, .italic = true, .color = {}}};
TextFieldHandle richFieldOf(Ui& ui, ComponentConfig control, std::vector<StyleSpan> spans = richSpans) {
    control.rich = true;
    auto model = std::make_shared<RichText>(RichText{control.value, std::move(spans)});
    auto field = *ui.addTextField(std::move(control));
    check(bool(field.connectRichSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a rich source");
    check(bool(field.connectRichConfirmed([](const Change<TextFieldHandle, RichText>&) { return Result{}; })), "Connect a rich write");
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
    void styledText(std::u16string_view value, int, FontStyle style, int x, int y, Color) override { draws.push_back({std::u16string(value), style, x, y}); }
};

void aJustifiedRichLineKeepsCaretSelectionHitsGlyphsAndUnderlineTogether() {
    for (auto method : {JustifyMethod::automatic, JustifyMethod::spaces, JustifyMethod::perCharacter}) {
        Ui ui;
        auto handle = richFieldOf(ui, multi("t", words, TextAlign::justify, method, 4));
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
            check(near(view.xInLine(0, segment.begin), segment.x, 0.01f), "The caret stop before a segment is where the segment is drawn");
        // The edge: the content of every stretched wrapped line ends at the wrap edge, in every method.
        for (unsigned i = 0; i + 1 < view.lines.size(); ++i) {
            const auto& item = view.lines[i];
            const auto visible = std::u16string_view(words).substr(item.begin, item.end - item.begin);
            const unsigned contentEnd = item.begin + static_cast<unsigned>(visible.find_last_not_of(u' ') + 1);
            if (!item.segments.empty()) check(near(view.xInLine(i, contentEnd), wrap, 1.f), "A stretched rich line ends flush at the wrap edge");
        }
        // The bold characters are measured bold: 'b' is 10 px wide.
        if (method == JustifyMethod::perCharacter) check(view.xInLine(0, 4) - view.xInLine(0, 3) > 10.f, "Per character: the stop after a bold letter includes the gap added after it");
        else check(near(view.xInLine(0, 4) - view.xInLine(0, 3), 10.f, 0.01f), "A bold character advances by its bold width");
        // Drawn glyphs: every call starts at the caret stop of its first character, in the face of its run.
        auto semantics = *detail::access(ui).describe("t");
        StyledProbe probe(detail::access(ui), *p, semantics, webAppearance(), 1.f, true);
        detail::componentType(ComponentKind::textField).paint(probe);
        unsigned offset = 0;
        bool drawnWhereStopsAre = !probe.draws.empty(), facesRight = true;
        for (const auto& draw : probe.draws) {
            const unsigned at = view.lineOf(offset);
            drawnWhereStopsAre &= draw.x == view.text.x + static_cast<int>(std::lround(view.xInLine(at, offset)));
            facesRight &= draw.style.bold == (offset >= 3 && offset < 8) && draw.style.italic == (offset >= 27 && offset < 32);
            offset += static_cast<unsigned>(draw.text.size());
        }
        check(drawnWhereStopsAre && facesRight, "Every drawn piece sits at its caret stop and keeps its run's face");
        const unsigned lastVisible = std::min<unsigned>(view.first + view.visible, static_cast<unsigned>(view.lines.size())) - 1;
        check(offset == view.lines[lastVisible].end, "The visible lines are drawn whole: no character dropped, none drawn twice");
        // The underline spans the whole bold run, the added gap inside it included.
        const int lineY = view.rowY(0) + (view.rowHeight - view.lineHeight) / 2 + view.lineHeight * 5 / 6;
        const auto& bold = line.pieces[1];
        const int left = view.text.x + static_cast<int>(std::lround(bold.x)), right = view.text.x + static_cast<int>(std::lround(bold.x + bold.width));
        const auto rules = std::count_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::content && f.rect.h == 1 && f.rect.y == lineY && f.rect.x == left && f.rect.w == right - left; });
        const auto thin = std::count_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::content && f.rect.h == 1 && f.rect.y >= view.text.y; });
        check(rules == 1 && thin == 1, "One underline, from the first to the last segment of the bold run");
        if (method == JustifyMethod::automatic) check(right - left > 51, "It is wider than the 50 px of the bold text: it spans the gap that was added inside it");
        // The selection highlight is drawn from stop to stop.
        const int from = view.text.x + static_cast<int>(std::lround(view.xInLine(0, 1))), to = view.text.x + static_cast<int>(std::lround(view.xInLine(0, 4)));
        const auto selected = std::count_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::selection; });
        const auto selection = std::find_if(probe.fills.begin(), probe.fills.end(), [&](const auto& f) { return f.role == detail::PaintRole::selection; });
        check(selected == 1 && selection->rect.x == from && selection->rect.w == to - from && to - from > 0, "The selection highlight runs from the stop of the second character to the stop inside the bold run");
        // A click on every drawn stop of the line puts the caret there; the caret is drawn at the same stop.
        for (const auto& [at, x] : line.run.stops) {
            const int px = view.text.x + static_cast<int>(std::lround(line.shift + x));
            check(laid.frame.textOffset(handle, px, view.text.y + 2) == line.begin + at, "A click on a drawn stop lands on its offset");
            check(line.begin + at == line.end || p->caretX(line.begin + at) == px, "caret_x follows the same stop");
        }
    }
}

void aDoubleClickOnAJustifiedLineSelectsTheWordUnderThePointer() {
    // "ee " is drawn several pixels right of where unstretched text would put it: the click must follow the drawn text.
    for (bool rich : {false, true}) {
        Ui ui;
        auto control = multi("t", words, TextAlign::justify, JustifyMethod::automatic, 4);
        auto handle = rich ? richFieldOf(ui, std::move(control), {{.start = 3, .length = 5, .bold = true, .color = {}}}) : fieldOf(ui, std::move(control));
        check(bool(handle.setWordSegmentation(WordSegmentation::scriptRun)), "Words are runs of one script: no system dictionary involved");
        auto& host = detail::access(ui);
        std::uint64_t now = 0;
        host.setClickClock([&] { return now; });
        host.setClickSettings({500, 4, 4});
        Laid laid(ui, handle, 190);
        const auto* p = laid.frame.find(handle);
        const auto& view = p->view;
        check(view.lines.size() >= 3 && !view.lines[0].segments.empty() && view.lines[0].end >= 15, "The first line holds at least six words and is stretched");
        check(view.xInLine(0, 12) > 12 * 8.f + 4, "The word really moved right because of the stretching");
        auto doubleClickOn = [&](unsigned offset, unsigned lineIndex = 0) {
            const int x = view.text.x + static_cast<int>(std::lround(view.xInLine(lineIndex, offset))) + 3, y = view.rowY(lineIndex) + view.rowHeight / 2;
            now += 1000;
            host.textPointerDown("t", detail::placementTextHit(laid.frame, "t", x, y), detail::placementTextCell(laid.frame, "t", x, y), x, y, false);
            now += 100;
            host.textPointerDown("t", detail::placementTextHit(laid.frame, "t", x, y), detail::placementTextCell(laid.frame, "t", x, y), x, y, false);
            const auto s = host.edit("t")->selection;
            return TextSpan{std::min(s.anchor, s.caret), std::max(s.anchor, s.caret)};
        };
        check(doubleClickOn(12) == TextSpan{12, 15}, "A double click on the drawn 'ee' selects ee and the space after it");
        check(doubleClickOn(9) == TextSpan{9, 12}, "dd, a word to its left");
        check(doubleClickOn(0) == TextSpan{0, 3}, "aa, at the start of the line");
        const unsigned second = view.lines[1].begin;
        check(doubleClickOn(second + 6, 1) == TextSpan{second + 6, second + 9}, "A word on the second stretched line");
    }
}

void aVerticalLabelWithJustifyOrDistributeReadsFromTheTop() {
    const std::u16string text = u"縱書標籤的第一段落，文字夠長而折成數欄。\n第二段";
    for (auto align : {TextAlign::justify, TextAlign::distribute}) {
        Ui ui, plainUi;
        auto spread = *ui.addLabel({.id = "v", .value = text, .textAlign = align, .justifyMethod = JustifyMethod::perCharacter, .writingMode = WritingMode::vertical});
        auto left = *plainUi.addLabel({.id = "v", .value = text, .textAlign = TextAlign::left, .writingMode = WritingMode::vertical});
        Laid a(ui, spread, 200), b(plainUi, left, 200);
        const auto* got = a.frame.find(spread);
        const auto* want = b.frame.find(left);
        check(got && want && got->vertical && got->columnLayout.columns.size() >= 2, "A vertical label with justify flows into columns");
        check(got->columnLayout.columns.size() == want->columnLayout.columns.size() && got->columnLayout.pitch == want->columnLayout.pitch && got->columnLayout.extent == want->columnLayout.extent,
              "Same columns as the left-aligned label");
        bool same = true;
        for (std::size_t i = 0; i < got->columnLayout.columns.size() && i < want->columnLayout.columns.size(); ++i) {
            const auto& g = got->columnLayout.columns[i];
            const auto& w = want->columnLayout.columns[i];
            same &= g.bounds.x == w.bounds.x && g.bounds.y == w.bounds.y && g.bounds.w == w.bounds.w && g.bounds.h == w.bounds.h && g.begin == w.begin && g.end == w.end && g.pieces.size() == w.pieces.size();
        }
        check(same, "Each column is placed exactly as top alignment places it: justify and distribute read as top");
        check(std::all_of(got->lines.begin(), got->lines.end(), [](const Line& l) { return l.segments.empty(); }), "No horizontal pieces are made");
        // The property still reads as set; run-time changes and the user's setting never break the layout.
        check(*spread.textAlign() == align && *spread.justifyMethod() == JustifyMethod::perCharacter, "The property reads as set");
        check(bool(spread.setTextAlign(TextAlign::left)) && bool(spread.setTextAlign(align)), "Switching to and from it works at run time");
        auto settings = ui.presentation();
        settings.justifyAsLeft = true;
        check(bool(ui.setPresentation(settings)), "The user's setting is on");
        Laid again(ui, spread, 200);
        check(again.frame.find(spread)->columnLayout.columns.size() == want->columnLayout.columns.size(), "Still the same columns with the setting on");
        auto probe = painted(ui, again.frame, spread, "v");
        check(probe.texts.empty() || !probe.texts.front().text.empty(), "The label paints without error");
    }
}

// ---- Code review 2026-11-12 item 15: what is really drawn ---------------------------------------------------
// A line of n Chinese characters of one em each, stretched to `room` px, has its characters at k * (em + (room - n * em) / (n - 1)):
// worked out here from the glyph width alone. Labels draw each piece at floor(x) (and stop 0.02 px short of the width, so the last
// piece's right edge stays inside the box); fields round it.
void plainJustifiedTextIsDrawnAtTheStretchedPositionsAtEveryScale() {
    const std::u16string chinese = u"一二三四五六七八九十甲乙丙丁";
    int labelPieces = 0, fieldPieces = 0;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        {  // a label 130 px wide
            Ui ui;
            auto handle = *ui.addLabel(label("l", chinese, TextAlign::justify));
            Laid laid(ui, handle, 130, scale);
            const auto* p = laid.frame.find(handle);
            const int em = p->textSize;
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
                for (int k = 0; k < n; ++k, ++next, ++labelPieces) {
                    check(next < probe.texts.size() && probe.texts[next].text == line.text.substr(static_cast<std::size_t>(k), 1), "One draw call per character");
                    if (next >= probe.texts.size()) break;
                    const float ideal = static_cast<float>(k) * (static_cast<float>(em) + extra);
                    check(static_cast<float>(probe.texts[next].x) <= ideal + 0.001f && static_cast<float>(probe.texts[next].x) > ideal - 1.05f, "A label draws a piece at floor of its stretched position");
                }
                const int rightEdge = probe.texts[next - 1].x + em;
                check(rightEdge <= 130 && rightEdge >= 128, "The last piece ends inside the box, within two pixels of its edge");
            }
            check(next == probe.texts.size(), "Nothing else is drawn");
        }
        {  // a multi-line field 190 px wide
            Ui ui;
            auto handle = fieldOf(ui, multi("t", twenty, TextAlign::justify, JustifyMethod::automatic, 12));  // tall enough to show every line at every scale
            Laid laid(ui, handle, 190, scale);
            const auto* p = laid.frame.find(handle);
            const int em = p->textSize;
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
                for (int k = 0; k < n; ++k, ++next, ++fieldPieces) {
                    check(next < probe.texts.size(), "One draw call per character");
                    if (next >= probe.texts.size()) break;
                    const float ideal = static_cast<float>(p->view.text.x) + static_cast<float>(k) * (static_cast<float>(em) + extra);
                    check(std::abs(static_cast<float>(probe.texts[next].x) - ideal) <= 0.6f, "A field draws a piece at its rounded stretched position");
                }
            }
            check(next == probe.texts.size(), "Nothing else is drawn");
        }
    }
    check(labelPieces >= 4 * 8 && fieldPieces >= 4 * 8, "The check really looked at stretched pieces at every scale");
}

// An input method composition inside a justified line: the preedit characters take part in the stretched line, so the candidate
// window's anchor (the caret rectangle), the hit test and the stops all sit where the characters are drawn.
void aCompositionOnAJustifiedLineIsAnchoredAtTheStretchedCaret() {
    for (bool rich : {false, true}) {
        Ui ui;
        auto handle = rich ? richFieldOf(ui, multi("t", twenty, TextAlign::justify), {}) : fieldOf(ui, multi("t", twenty, TextAlign::justify));
        auto& host = detail::access(ui);
        check(bool(handle.focus()) && bool(handle.select(3, 3)), "Focus the field with the caret after the third character");
        check(bool(host.compositionUpdate(u"測", 1)), "Start a composition of one character");
        Laid laid(ui, handle, 190);
        const auto* p = laid.frame.find(handle);
        const auto* session = host.edit("t");
        check(session && session->composing && session->visibleCaret() == 4, "The caret is after the preedit character (visible offset 4)");
        // 21 characters now: nine on the first line, 6 px spare over eight gaps, so the stop before character k is 16.75 * k.
        const float wrap = static_cast<float>(p->view.text.w - inset);
        check(near(p->view.xInLine(0, 9), wrap, 0.05f), "The first line still ends at the wrap edge");
        const Rect anchor = p->caretRect(session->visibleCaret());
        check(anchor.x == p->view.text.x + static_cast<int>(std::lround(4 * 16.75f)), "The candidate anchor is the stretched caret, 67 px in");
        check(p->caretRect(session->begin()).x == p->view.text.x + static_cast<int>(std::lround(3 * 16.75f)), "The composition starts at the stretched stop before the preedit");
        check(laid.frame.textOffset(handle, anchor.x, anchor.y + 2) == session->visibleCaret(), "Hitting the anchor gives the caret back");
        check(anchor.y == p->view.rowY(0) && anchor.h == p->view.rowHeight, "The anchor is on the first row");
    }
}

// ---- One generic driver: every subject x alignment x method x appearance x density ----------------
// Every kind reports the same rows: where the visible text of each display line starts and ends, measured
// from the left edge of the area the alignment works in, and which rows end their paragraph.
struct Row { bool last; float left, right; std::vector<float> extras; bool stopsMatch = true; };  // extras: the space added after each piece but the last
struct Rows { std::vector<Row> rows; float area = 0; };
struct Subject {
    const char* name;
    bool field;
    Rows (*lay)(Ui&, TextAlign, JustifyMethod, const std::u16string&, const Appearance&);
};
// The space between one piece and the next, worked out from the fake backend's own glyph widths (not from TextSegment::width).
std::vector<float> extrasOf(const std::u16string& line, const std::vector<TextSegment>& segments) {
    FakeMeasure measure;
    std::vector<float> extras;
    for (std::size_t j = 0; j + 1 < segments.size(); ++j)
        extras.push_back(segments[j + 1].x - (segments[j].x + measure.measure(line.substr(segments[j].begin, segments[j].end - segments[j].begin), 16).width));
    return extras;
}
float contentRight(const std::u16string& line, const std::vector<TextSegment>& segments, float origin) {
    FakeMeasure measure;
    const auto contentEnd = static_cast<unsigned>(line.find_last_not_of(u' ') + 1);
    float right = origin;
    for (const auto& s : segments)
        if (s.begin < contentEnd) right = origin + s.x + measure.measure(line.substr(s.begin, std::min(s.end, contentEnd) - s.begin), 16).width;
    return right;
}
Rows labelRows(Ui& ui, const LabelHandle& handle, const std::u16string& text, bool scrollable, const Appearance& look) {
    FakeMeasure measure; ScrollState outer, inner;
    auto node = scrollable ? layout::scroll(layout::column({layout::scrollText(handle, inner, 400)})) : layout::component(handle);
    auto frame = arrange(node, ui, look, measure, 300, 400, 1.f, outer);
    const auto* p = frame.find(handle);
    Rows out; out.area = static_cast<float>(p->bounds.w);
    const auto pieces = wrapMarked(measure, text, 16, p->bounds.w);
    for (std::size_t i = 0; i < p->lines.size(); ++i) {
        const auto& line = p->lines[i];
        const float origin = static_cast<float>(line.bounds.x - p->bounds.x);
        const float left = line.segments.empty() ? origin : origin + line.segments.front().x;
        const float right = line.segments.empty() ? origin + measure.measure(std::u16string(line.text).substr(0, line.text.find_last_not_of(u' ') + 1), 16).width
                                                  : contentRight(line.text, line.segments, origin);
        out.rows.push_back({!pieces[i].soft, left, right, extrasOf(line.text, line.segments)});
    }
    return out;
}
Rows fieldRows(Ui& ui, const TextFieldHandle& handle, const std::u16string& text, const Appearance& look) {
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::component(handle), ui, look, measure, 300, 400, 1.f, scroll);
    const auto* p = frame.find(handle);
    Rows out;
    if (!p->multiline) {  // one line, the end of its paragraph
        out.area = static_cast<float>(p->textField.editor.w);
        const float origin = p->shift;
        out.rows.push_back({true, origin, origin + p->run.x(static_cast<unsigned>(text.find_last_not_of(u' ') + 1))});
        return out;
    }
    out.area = static_cast<float>(p->view.text.w - look.componentPresentation(ui.presentation().density).inset);
    for (unsigned i = 0; i < p->view.lines.size(); ++i) {
        // Positions come from the pieces and the fake backend's glyph widths, not from the stops being compared with themselves.
        const auto& line = p->view.lines[i];
        const auto visible = text.substr(line.begin, line.end - line.begin);
        const unsigned contentEnd = static_cast<unsigned>(visible.find_last_not_of(u' ') + 1);
        const float origin = line.shift;
        const float left = line.segments.empty() ? origin : origin + line.segments.front().x;
        const float right = line.segments.empty() ? origin + measure.measure(visible.substr(0, contentEnd), 16).width : contentRight(visible, line.segments, origin);
        Row row{!line.soft, left, right, extrasOf(visible, line.segments)};
        // The caret stops, the hit test and the pieces must tell the same story.
        for (const auto& segment : line.segments)
            row.stopsMatch &= near(p->view.xInLine(i, line.begin + segment.begin), origin + segment.x, 0.05f);
        out.rows.push_back(std::move(row));
    }
    return out;
}
const Subject subjects[] = {
    {"label", false, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        return labelRows(ui, *ui.addLabel(label("subject", t, a, m)), t, false, l); }},
    {"scrollable label", false, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        return labelRows(ui, *ui.addLabel(label("subject", t, a, m)), t, true, l); }},
    {"single-line field", true, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        auto one = t; std::replace(one.begin(), one.end(), u'\n', u' ');  // a single line holds no line break
        return fieldRows(ui, fieldOf(ui, [&] { auto c = single("subject", one, a); c.justifyMethod = m; return c; }()), one, l); }},
    {"multi-line field", true, [](Ui& ui, TextAlign a, JustifyMethod m, const std::u16string& t, const Appearance& l) {
        return fieldRows(ui, fieldOf(ui, multi("subject", t, a, m, 6)), t, l); }},
};
// Every line of both paragraphs has a space or Chinese text to stretch by, whatever the method asks for.
const std::u16string english = u"aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz\nlast paragraph of the text here";
const std::u16string mixed = u"中文 text 混排 sample 的 句子 要 對齊 兩 端 好 看 一 些 嗎 這 樣 才 能 比 較 清 楚\n第二段 也 要 對 齊 看 看 結 果";

void everySubjectStretchesEveryNonLastLineFlushInEveryCombination() {
    int stretchedLines = 0;
    for (const auto& subject : subjects) for (const auto* look : {&webAppearance(), &materialAppearance()}) for (auto density : {Density::standard, Density::compact})
    for (auto align : {TextAlign::justify, TextAlign::distribute}) for (auto method : {JustifyMethod::automatic, JustifyMethod::spaces, JustifyMethod::perCharacter}) {
        if (subject.field && align == TextAlign::distribute) continue;  // a field refuses it (covered above)
        for (const auto* text : {&english, &mixed}) {
            Ui ui;
            check(bool(ui.setPresentation({.density = density})), "Set the density");
            Rows laid;
            try { laid = subject.lay(ui, align, method, *text, *look); }
            catch (const std::exception& e) { throw std::runtime_error(std::string(subject.name) + " " + std::to_string(static_cast<int>(align)) + "/" + std::to_string(static_cast<int>(method)) + ": " + e.what()); }
            check(!laid.rows.empty(), subject.name);
            // The same corpus laid out left-aligned tells which rows end a paragraph and how wide they naturally are.
            for (const auto& row : laid.rows) {
                const bool stretch = align == TextAlign::distribute || !row.last;
                if (std::string_view(subject.name) == "single-line field") { check(row.last && near(row.left, 0), subject.name); continue; }
                if (stretch) {
                    ++stretchedLines;
                    check(near(row.left, 0) && near(row.right, laid.area), subject.name);
                    // Even distribution: every added gap is the same size (within a hair), and there is at least one.
                    float smallest = 1e9f, largest = -1e9f; int gaps = 0;
                    for (float extra : row.extras) if (extra > 0.01f) { smallest = std::min(smallest, extra); largest = std::max(largest, extra); ++gaps; }
                    // (A line whose text already fills the width has no pieces and nothing to share.)
                    check(row.extras.empty() || (gaps > 0 && largest - smallest <= 0.05f), (std::string(subject.name) + ": the extra space is shared equally").c_str());
                    check(row.stopsMatch, (std::string(subject.name) + ": caret stops follow the drawn pieces").c_str());
                } else {
                    check(near(row.left, 0) && row.right < laid.area - 1, subject.name);
                }
            }
        }
    }
    check(stretchedLines > 200, "The driver really exercised stretched lines");
}

int main() {
    const test::Case cases[] = {
        {"a justified label spreads Chinese evenly between characters", aJustifiedLabelSpreadsChineseEvenlyBetweenTheCharacters},
        {"the automatic method keeps English words whole and widens spaces", theAutomaticMethodKeepsEnglishWordsWholeAndWidensSpaces},
        {"full-width punctuation keeps its width and takes a share", fullWidthPunctuationKeepsItsWidthAndTakesAShareLikeACharacter},
        {"the spaces method only widens spaces", theSpacesMethodOnlyWidensSpacesAndAChineseLineStaysLeft},
        {"nowhere to stretch stays left; trailing spaces get nothing", aLineWithNowhereToStretchStaysLeftAndTrailingSpacesGetNothing},
        {"the per-character method spreads Latin letters, no ligature", thePerCharacterMethodSpreadsLatinLettersAndNeverDrawsALigature},
        {"per-character cuts inside a ligature cluster, the others do not", thePerCharacterMethodCutsInsideALigatureClusterAndTheOthersDoNot},
        {"per-character keeps kerning and cuts at clusters", thePerCharacterMethodKeepsKerningAndCutsAtClusters},
        {"distribute stretches the last line and a single line", theDistributeAlignmentStretchesTheLastLineAndASingleLine},
        {"a multi-code-point character is never cut", aCharacterMadeOfSeveralCodePointsIsNeverCut},
        {"a justified multi-line field stretches wrapped lines to the wrap edge", aJustifiedMultiLineFieldStretchesEachWrappedLineToTheWrapEdge},
        {"clicks and drags land on the drawn characters", clicksAndDragsLandOnTheDrawnCharactersOfAJustifiedLine},
        {"vertical keys and the input method follow the stretched positions", verticalKeysAndTheInputMethodFollowTheStretchedPositions},
        {"a justified single-line field looks left-aligned", aJustifiedSingleLineFieldLooksLeftAligned},
        {"a field refuses distribute and a label accepts it", aFieldRefusesDistributeAndALabelAcceptsIt},
        {"the justification method is readable and settable everywhere", theJustificationMethodIsReadableAndSettableEverywhere},
        {"a run-time change restretches the next frame", aRunTimeChangeRestretchesTheNextFrame},
        {"the user setting draws stretched text left, keeping the properties", theUserSettingDrawsStretchedTextLeftButKeepsTheProperties},
        {"a justified rich line keeps caret, selection, hits, glyphs and underline together", aJustifiedRichLineKeepsCaretSelectionHitsGlyphsAndUnderlineTogether},
        {"a double click on a justified line selects the word under the pointer", aDoubleClickOnAJustifiedLineSelectsTheWordUnderThePointer},
        {"a vertical label with justify or distribute reads from the top", aVerticalLabelWithJustifyOrDistributeReadsFromTheTop},
        {"plain justified text is drawn at the stretched positions at every scale", plainJustifiedTextIsDrawnAtTheStretchedPositionsAtEveryScale},
        {"a composition on a justified line is anchored at the stretched caret", aCompositionOnAJustifiedLineIsAnchoredAtTheStretchedCaret},
        {"every subject stretches every non-last line flush in every combination", everySubjectStretchesEveryNonLastLineFlushInEveryCombination},
    };
    return test::run("justify", cases);
}
