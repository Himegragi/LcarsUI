#pragma once
// Word boundaries for double-click selection, Ctrl+arrow movement and UIA text units (#83).
// One place decides where words begin and end, so the pointer, the keyboard, the semantic layer and
// UIA can never disagree. Offsets are UTF-16 and never fall inside a surrogate pair.
#include "lcars/core.hpp"
#include <string_view>
#include <vector>

namespace lcars::detail {
// The replaceable seam behind the dictionary mode. An implementation fills `out` with ascending
// boundary offsets (it may omit 0 and text.size()) and returns false when it cannot segment this text
// (for example the system library is missing). Tests swap in a breaker with fixed rules.
class WordBreaker {
public:
    virtual ~WordBreaker() = default;
    virtual bool boundaries(std::u16string_view text, std::vector<unsigned>& out) const = 0;
};
// Rule breakers, always available. Both isolate every punctuation mark, keep a run of spaces together,
// keep a run of Latin letters and digits together and give each line feed its own word.
// per_character also gives every Chinese or Japanese character its own word; script_run keeps a run of them.
const WordBreaker& per_character_breaker();
const WordBreaker& script_run_breaker();
// The Windows system ICU word BreakIterator for zh_TW, loaded on first use from System32. Fails
// (boundaries returns false) when icu.dll cannot be loaded or a break iterator cannot be opened.
const WordBreaker& system_icu_breaker();
// The result of segmenting one text: boundaries start with 0 and end with text.size() (just {0} for empty
// text), and `used` is the mode that actually produced them. Dictionary falls back to per_character when the
// breaker fails, so a failing dictionary never interrupts an operation.
struct Words {
    std::vector<unsigned> bounds;
    WordSegmentation used = WordSegmentation::per_character;
};
Words segment_words(std::u16string_view text, WordSegmentation mode, const WordBreaker& dictionary);
// Whitespace other than a line feed: Ctrl+arrow does not stop on it and a double click takes it along.
bool is_space_word(std::u16string_view text, unsigned begin, unsigned end);
// Caret stops of Ctrl+Left/Right: 0, the start of every word that is not whitespace, and text.size().
std::vector<unsigned> word_stops(std::u16string_view text, const Words& words);
// The range a double click at the character starting at `offset` selects (see the ticket): a word takes the
// whitespace after it, a punctuation mark or a run of spaces stands alone, and at the end of a paragraph the
// last word of that paragraph counts. An empty range means there is nothing to select.
TextSpan word_span(std::u16string_view text, const Words& words, unsigned offset);
// Where Ctrl+Right (forward) or Ctrl+Left goes from `offset`: the nearest word stop after (before) it, or the end
// (start) of the text when there is none.
unsigned next_word_stop(std::u16string_view text, const Words& words, unsigned offset, bool forward);
// The paragraph (text between line feeds, the line feed excluded) holding the character at `offset`.
TextSpan paragraph_span(std::u16string_view text, unsigned offset);
}
