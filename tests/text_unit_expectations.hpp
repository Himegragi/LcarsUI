#pragma once
// One worked example of #83's text units, shared by every seam that asserts it: the core seam (tests/word_select.cpp:
// word_unit_bounds, double-click words) and the UIA seam (tests/uia_demo_client.cpp: TextUnit_Word / TextUnit_Paragraph).
// ASCII only, so no dictionary can change the words. Words are what a double click selects: a word with the spaces after
// it, each punctuation mark, each line feed, a run of spaces that follows no word. Paragraphs run through their line feed.
#include <string>
#include <vector>

namespace test::units {
inline constexpr const char16_t* text = u"ab cd,ef\nxy  zw";
inline const std::vector<std::u16string>& words() {
    static const std::vector<std::u16string> value{u"ab ", u"cd", u",", u"ef", u"\n", u"xy  ", u"zw"};
    return value;
}
inline const std::vector<std::u16string>& paragraphs() {
    static const std::vector<std::u16string> value{u"ab cd,ef\n", u"xy  zw"};
    return value;
}
// The pieces from `first` up to but excluding `last`, joined.
inline std::u16string join(const std::vector<std::u16string>& pieces, std::size_t first, std::size_t last) {
    std::u16string out;
    for (auto i = first; i < last; ++i) out += pieces[i];
    return out;
}
}
