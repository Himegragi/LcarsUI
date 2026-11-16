#pragma once
// What the vertical label of the demo's About page must show, asserted at several seams (core semantic layer,
// settings demo, UIA client). One shared predicate each, so the seams cannot drift apart.
#include <string_view>

namespace test::vertical {
inline constexpr std::u16string_view aboutSample = u"LcarsUI 設定管理示範（第 12 版），謹此致謝。";
// The box a screen reader gets is the box drawn: inside the window, with area, at least as tall as asked.
inline bool drawnBox(long left, long top, long right, long bottom, long windowLeft, long windowTop, long windowRight, long windowBottom, long minHeight) {
    return right > left && bottom - top >= minHeight && left >= windowLeft && right <= windowRight && top >= windowTop && bottom <= windowBottom;
}
}
