#include "vertical.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
// Vertical writing for labels (#82), core seam: classification, flow, properties, layout, scrolling.
using namespace lcars;
using test::check;
using detail::segment_vertical;

namespace {
struct Cut { std::u16string text; VerticalForm form; };
std::vector<Cut> cuts(std::u16string_view text, int digits = 3) {
    std::vector<Cut> result;
    for (const auto& segment : segment_vertical(text, digits))
        result.push_back({std::u16string(text.substr(segment.begin, segment.end - segment.begin)), segment.form});
    return result;
}
bool is(const std::vector<Cut>& actual, std::vector<Cut> expected) {
    if (actual.size() != expected.size()) return false;
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i].text != expected[i].text || actual[i].form != expected[i].form) return false;
    return true;
}
using F = VerticalForm;

void han_fullwidth_and_emoji_stay_upright() {
    check(is(cuts(u"設定"), {{u"設", F::upright}, {u"定", F::upright}}), "Han characters are upright, one cell each");
    check(is(cuts(u"ＡＢ"), {{u"Ａ", F::upright}, {u"Ｂ", F::upright}}), "Full-width Latin is upright");
    check(is(cuts(u"😀"), {{u"😀", F::upright}}), "An emoji (surrogate pair) is one upright cell");
}
void latin_words_and_sentences_rotate_as_one_run() {
    check(is(cuts(u"LcarsUI"), {{u"LcarsUI", F::sideways}}), "A Latin word is one sideways run");
    check(is(cuts(u"Hello World"), {{u"Hello World", F::sideways}}), "A Latin sentence is one sideways run");
    check(is(cuts(u"版 A 版"), {{u"版", F::upright}, {u" A ", F::sideways}, {u"版", F::upright}}), "A single letter is sideways, never upright");
    check(is(cuts(u"GDP"), {{u"GDP", F::sideways}}), "An abbreviation is sideways");
}
void digits_follow_the_combine_limit() {
    check(is(cuts(u"第5版"), {{u"第", F::upright}, {u"5", F::upright}, {u"版", F::upright}}), "One digit is upright");
    check(is(cuts(u"第12版"), {{u"第", F::upright}, {u"12", F::combined}, {u"版", F::upright}}), "Two digits are combined into one cell");
    check(is(cuts(u"第123版"), {{u"第", F::upright}, {u"123", F::combined}, {u"版", F::upright}}), "Three digits are combined into one cell");
    check(is(cuts(u"第1234版"), {{u"第", F::upright}, {u"1234", F::sideways}, {u"版", F::upright}}), "Four digits rotate");
    check(is(cuts(u"第123版", 2), {{u"第", F::upright}, {u"123", F::sideways}, {u"版", F::upright}}), "With a limit of 2, three digits rotate");
    check(is(cuts(u"第12版", 0), {{u"第", F::upright}, {u"12", F::sideways}, {u"版", F::upright}}), "With a limit of 0, even two digits rotate");
    check(is(cuts(u"第5版", 0), {{u"第", F::upright}, {u"5", F::upright}, {u"版", F::upright}}), "A single digit stays upright when combining is off");
    check(is(cuts(u"UI12"), {{u"UI12", F::sideways}}), "Digits touching Latin letters stay part of the sideways word");
}
void punctuation_gets_the_taiwan_placement_classes() {
    for (auto mark : {u"，", u"。", u"、", u"；", u"：", u"！", u"？"})
        check(is(cuts(mark), {{mark, F::point}}), "Point punctuation is centered, not rotated");
    for (auto mark : {u"「", u"」", u"『", u"』", u"（", u"）", u"《", u"》", u"〈", u"〉", u"【", u"】", u"〔", u"〕"})
        check(is(cuts(mark), {{mark, F::bracket}}), "Brackets and quotes turn to face up and down");
    for (auto mark : {u"—", u"…", u"─"})
        check(is(cuts(mark), {{mark, F::line}}), "Dashes and ellipses become centered lines");
    for (auto mark : {u"·", u"‧", u"・"})
        check(is(cuts(mark), {{mark, F::middle_dot}}), "Middle dots are centered upright");
    check(is(cuts(u"——"), {{u"—", F::line}, {u"—", F::line}}), "A double dash is two line cells");
}
}

int main() {
    static const test::Case cases[] = {
        {"han_fullwidth_and_emoji_stay_upright", han_fullwidth_and_emoji_stay_upright},
        {"latin_words_and_sentences_rotate_as_one_run", latin_words_and_sentences_rotate_as_one_run},
        {"digits_follow_the_combine_limit", digits_follow_the_combine_limit},
        {"punctuation_gets_the_taiwan_placement_classes", punctuation_gets_the_taiwan_placement_classes},
    };
    return test::run("vertical", cases);
}
