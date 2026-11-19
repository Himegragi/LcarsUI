#pragma once
#include "isoduo/appearance.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace isoduo::detail {
struct LinuxImeState {
    std::uint64_t target;
    Rect caret; // client pixels
    std::u16string text;
    unsigned anchor, cursor; // UTF-16 offsets in committed text
    bool multiline;
};

struct LinuxImeEdit {
    std::uint64_t target = 0;
    std::u16string preedit, commit;
    unsigned cursor = 0; // UTF-16 in preedit
    unsigned deleteBefore = 0, deleteAfter = 0; // UTF-8 bytes outside selection
    bool replacePreedit = false;
};
}
