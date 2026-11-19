#pragma once
#include "isoduo/layout.hpp"

namespace test {
inline bool caretMatchesHit(const isoduo::Frame& frame, const isoduo::TextFieldHandle& field,
                           unsigned line, unsigned offset) {
    const auto* placement = frame.find(field);
    if (!placement || !placement->multiline) return false;
    const auto& view = placement->view;
    const auto caret = placement->caretRect(offset);
    return view.lineOf(offset) == line && caret.y == view.rowY(line) &&
           frame.hit(caret.x, caret.y + caret.h / 2) == placement &&
           frame.textOffset(field, caret.x, caret.y + caret.h / 2) == offset;
}
}
