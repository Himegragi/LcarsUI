#pragma once
#include <cstddef>

namespace test {
// UIA HelpText exposes NodeSnapshot::description. Each seam supplies its observed length.
inline bool optionHelpTextIsEmpty(std::size_t length) { return length == 0; }
// Enabled here is effective enabled: the core seam combines the parent and item's settings;
// UIA supplies that combined value through IsEnabled.
inline bool disabledRowCannotTakeFocus(bool enabled, bool focusable) { return enabled || !focusable; }
}
