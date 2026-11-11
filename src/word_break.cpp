#include "word_break.hpp"
#include <algorithm>
#include <cstdint>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace lcars::detail {
namespace {
enum class Kind { letters, han, space, punctuation, newline };

char32_t code_point(std::u16string_view text, unsigned at, unsigned* width = nullptr) {
    const char16_t first = text[at];
    if (first >= 0xd800 && first <= 0xdbff && at + 1 < text.size() && text[at + 1] >= 0xdc00 && text[at + 1] <= 0xdfff) {
        if (width) *width = 2;
        return 0x10000 + ((static_cast<char32_t>(first) - 0xd800) << 10) + (static_cast<char32_t>(text[at + 1]) - 0xdc00);
    }
    if (width) *width = 1;
    return first;
}
bool in(char32_t c, char32_t low, char32_t high) { return c >= low && c <= high; }
bool is_han_or_kana(char32_t c) {
    return in(c, 0x2e80, 0x2fdf) || c == 0x3005 || c == 0x3007 || in(c, 0x3021, 0x3029) || in(c, 0x3038, 0x303b) ||
           in(c, 0x3041, 0x309f) || (in(c, 0x30a0, 0x30ff) && c != 0x30fb) || in(c, 0x3100, 0x312f) || in(c, 0x31a0, 0x31ff) ||
           in(c, 0x3400, 0x4dbf) || in(c, 0x4e00, 0x9fff) || in(c, 0xf900, 0xfaff) || in(c, 0xff66, 0xff9f) ||
           in(c, 0x1b000, 0x1b16f) || in(c, 0x20000, 0x323af);
}
bool is_space(char32_t c) {
    return c == u' ' || c == u'\t' || c == 0x00a0 || c == 0x1680 || in(c, 0x2000, 0x200a) || c == 0x202f || c == 0x205f || c == 0x3000;
}
// Everything that is neither a letter, a digit nor the connector '_' counts as punctuation, symbols included.
bool is_punctuation(char32_t c) {
    if (c < 0x80) return !((c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || c == u'_');
    if (c < 0x100) return (c >= 0xa1 && c <= 0xbf && c != 0xaa && c != 0xb5 && c != 0xba && c != 0xb2 && c != 0xb3 && c != 0xb9) || c == 0xd7 || c == 0xf7;
    if (in(c, 0x2010, 0x206f) || in(c, 0x2190, 0x2bff) || in(c, 0x2e00, 0x2e7f)) return true;
    if (in(c, 0x3001, 0x3004) || in(c, 0x3008, 0x3020) || c == 0x3030 || in(c, 0x303d, 0x303f) || c == 0x30fb) return true;
    if (in(c, 0xff01, 0xff0f) || in(c, 0xff1a, 0xff20) || in(c, 0xff3b, 0xff40) || in(c, 0xff5b, 0xff65) || in(c, 0xffe0, 0xffef)) return true;
    return in(c, 0xfe10, 0xfe6f) || in(c, 0x1f000, 0x1faff);
}
Kind classify(char32_t c) {
    if (c == u'\n') return Kind::newline;
    if (is_space(c)) return Kind::space;
    if (is_han_or_kana(c)) return Kind::han;
    if (is_punctuation(c)) return Kind::punctuation;
    return Kind::letters;
}
bool rule_boundaries(std::u16string_view text, bool han_each, std::vector<unsigned>& out) {
    Kind previous = Kind::newline;
    for (unsigned at = 0; at < text.size();) {
        unsigned width = 1;
        const Kind kind = classify(code_point(text, at, &width));
        const bool joins = at > 0 && kind == previous && (kind == Kind::letters || kind == Kind::space || (kind == Kind::han && !han_each));
        if (!joins) out.push_back(at);
        previous = kind;
        at += width;
    }
    return true;
}
struct PerCharacter final : WordBreaker {
    bool boundaries(std::u16string_view text, std::vector<unsigned>& out) const override { return rule_boundaries(text, true, out); }
};
struct ScriptRun final : WordBreaker {
    bool boundaries(std::u16string_view text, std::vector<unsigned>& out) const override { return rule_boundaries(text, false, out); }
};

#ifdef _WIN32
// The C API of the system icu.dll (Windows 10 1903 and later). Loaded lazily, never linked.
struct Icu {
    using Open = void*(__cdecl*)(int type, const char* locale, const char16_t* text, std::int32_t length, int* status);
    using Close = void(__cdecl*)(void*);
    using Step = std::int32_t(__cdecl*)(void*);
    Open open = nullptr; Close close = nullptr; Step first = nullptr; Step next = nullptr;
    Icu() {
        HMODULE module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return;
        auto symbol = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(module, name)); };
        open = reinterpret_cast<Open>(symbol("ubrk_open")); close = reinterpret_cast<Close>(symbol("ubrk_close"));
        first = reinterpret_cast<Step>(symbol("ubrk_first")); next = reinterpret_cast<Step>(symbol("ubrk_next"));
        if (!open || !close || !first || !next) open = nullptr;
    }
};
struct SystemIcu final : WordBreaker {
    bool boundaries(std::u16string_view text, std::vector<unsigned>& out) const override {
        static const Icu icu;
        if (!icu.open) return false;
        if (text.empty()) return true;
        int status = 0;
        // ICU does not copy the text: it must outlive the iterator, and it does (closed before returning).
        void* iterator = icu.open(1 /* UBRK_WORD */, "zh_TW", text.data(), static_cast<std::int32_t>(text.size()), &status);
        if (!iterator || status > 0) { if (iterator) icu.close(iterator); return false; }
        for (std::int32_t at = icu.first(iterator); at != -1; at = icu.next(iterator))
            if (at >= 0 && at < static_cast<std::int32_t>(text.size())) out.push_back(static_cast<unsigned>(at));
        icu.close(iterator);
        return true;
    }
};
#else
struct SystemIcu final : WordBreaker {
    bool boundaries(std::u16string_view, std::vector<unsigned>&) const override { return false; }
};
#endif

bool surrogate_tail(std::u16string_view text, unsigned at) {
    return at > 0 && at < text.size() && text[at] >= 0xdc00 && text[at] <= 0xdfff && text[at - 1] >= 0xd800 && text[at - 1] <= 0xdbff;
}
}  // namespace

const WordBreaker& per_character_breaker() { static const PerCharacter breaker; return breaker; }
const WordBreaker& script_run_breaker() { static const ScriptRun breaker; return breaker; }
const WordBreaker& system_icu_breaker() { static const SystemIcu breaker; return breaker; }

Words segment_words(std::u16string_view text, WordSegmentation mode, const WordBreaker& dictionary) {
    Words words;
    std::vector<unsigned> raw;
    const WordBreaker* breaker = mode == WordSegmentation::per_character ? &per_character_breaker()
                               : mode == WordSegmentation::script_run ? &script_run_breaker() : &dictionary;
    words.used = mode;
    if (!breaker->boundaries(text, raw)) {
        raw.clear();
        per_character_breaker().boundaries(text, raw);
        words.used = WordSegmentation::per_character;
    }
    // Whatever the breaker said: every line feed is a word of its own, no boundary splits a surrogate pair.
    for (unsigned at = 0; at < text.size(); ++at)
        if (text[at] == u'\n') { raw.push_back(at); raw.push_back(at + 1); }
    raw.push_back(0);
    raw.push_back(static_cast<unsigned>(text.size()));
    std::sort(raw.begin(), raw.end());
    raw.erase(std::unique(raw.begin(), raw.end()), raw.end());
    for (unsigned at : raw)
        if (at <= text.size() && !surrogate_tail(text, at)) words.bounds.push_back(at);
    return words;
}

namespace {
// A word proper (letters, digits, Chinese or Japanese characters), as opposed to punctuation, spaces or a line feed.
bool is_letter_word(std::u16string_view text, unsigned begin, unsigned end) {
    if (begin >= end) return false;
    const Kind kind = classify(code_point(text, begin));
    return kind == Kind::letters || kind == Kind::han;
}
}
bool is_space_word(std::u16string_view text, unsigned begin, unsigned end) {
    if (begin >= end) return false;
    for (unsigned at = begin; at < end;) {
        unsigned width = 1;
        if (classify(code_point(text, at, &width)) != Kind::space) return false;
        at += width;
    }
    return true;
}
std::vector<unsigned> word_stops(std::u16string_view text, const Words& words) {
    std::vector<unsigned> stops{0};
    for (std::size_t i = 0; i + 1 < words.bounds.size(); ++i)
        if (words.bounds[i] > 0 && !is_space_word(text, words.bounds[i], words.bounds[i + 1])) stops.push_back(words.bounds[i]);
    if (text.size() > 0) stops.push_back(static_cast<unsigned>(text.size()));
    return stops;
}
unsigned next_word_stop(std::u16string_view text, const Words& words, unsigned offset, bool forward) {
    const auto stops = word_stops(text, words);
    if (forward) {
        auto it = std::upper_bound(stops.begin(), stops.end(), offset);
        return it == stops.end() ? static_cast<unsigned>(text.size()) : *it;
    }
    auto it = std::lower_bound(stops.begin(), stops.end(), offset);
    return it == stops.begin() ? 0u : *(it - 1);
}
TextSpan paragraph_span(std::u16string_view text, unsigned offset) {
    offset = std::min<unsigned>(offset, static_cast<unsigned>(text.size()));
    const auto before = offset == 0 ? std::u16string_view::npos : text.substr(0, offset).rfind(u'\n');
    const auto after = text.find(u'\n', offset);
    return {before == std::u16string_view::npos ? 0u : static_cast<unsigned>(before) + 1,
            after == std::u16string_view::npos ? static_cast<unsigned>(text.size()) : static_cast<unsigned>(after)};
}
TextSpan word_span(std::u16string_view text, const Words& words, unsigned offset) {
    const auto& b = words.bounds;
    if (b.size() < 2) return {0, 0};
    offset = std::min<unsigned>(offset, static_cast<unsigned>(text.size()));
    const auto paragraph = paragraph_span(text, offset);
    auto segment_at = [&](unsigned at) {  // the index i with b[i] <= at < b[i + 1]
        auto it = std::upper_bound(b.begin(), b.end(), at);
        return static_cast<std::size_t>(it - b.begin()) - 1;
    };
    // Take the whitespace after a word along (Windows behaviour), never past the paragraph.
    auto with_space = [&](std::size_t i) -> TextSpan {
        TextSpan span{b[i], b[i + 1]};
        if (is_letter_word(text, span.begin, span.end) && i + 2 < b.size() && b[i + 1] < paragraph.end && is_space_word(text, b[i + 1], b[i + 2]))
            span.end = b[i + 2];
        return span;
    };
    if (offset >= paragraph.end) {  // past the end of the line: the last word on it
        std::size_t i = paragraph.end == paragraph.begin ? b.size() - 1 : segment_at(paragraph.end - 1);
        if (paragraph.end == paragraph.begin) return {offset, offset};
        while (i > 0 && b[i] > paragraph.begin && is_space_word(text, b[i], b[i + 1])) --i;
        return with_space(i);
    }
    return with_space(segment_at(offset));
}
}  // namespace lcars::detail
