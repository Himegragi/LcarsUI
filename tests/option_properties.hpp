#pragma once
#include <cstddef>

namespace test {
// UIA HelpText exposes NodeSnapshot::description. Each seam supplies its observed length.
inline bool optionHelpTextIsEmpty(std::size_t length) { return length == 0; }
// Enabled here is effective enabled: the core seam combines the parent and item's settings;
// UIA supplies that combined value through IsEnabled.
inline bool disabledRowCannotTakeFocus(bool enabled, bool focusable) { return enabled || !focusable; }
// Both semantic snapshots and UIA must identify the expected option, and focus implies focusability.
inline bool optionFocusMatches(bool focused, bool focusable, bool expected) {
    return focused == expected && (!focused || focusable);
}
inline bool focusPreservesSelection(bool originalSelected, bool targetSelected) {
    return originalSelected && !targetSelected;
}
inline bool optionFocusabilityMatches(bool focusable, bool parentFocusable, bool enabled, bool popupAllowsFocus = true) {
    return focusable == (parentFocusable && enabled && popupAllowsFocus);
}
}
