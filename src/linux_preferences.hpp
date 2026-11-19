#pragma once
#include <optional>

namespace isoduo::detail {
struct LinuxDesktopPreferences {
    std::optional<bool> animations;
    std::optional<bool> keyboardCues;
};

// Missing values leave the shared UI's normal motion and focus rules in effect.
LinuxDesktopPreferences readLinuxDesktopPreferences();
}
