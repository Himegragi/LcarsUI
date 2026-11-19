#include "linux_preferences.hpp"
#include "check.hpp"
#include <cstdlib>

int main() {
    // A missing settings service or schema is a valid desktop state.
    ::setenv("XDG_CURRENT_DESKTOP", "XFCE", 1);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/isoduo-session-bus", 1);
    const auto preferences = isoduo::detail::readLinuxDesktopPreferences();
    test::check(!preferences.animations && !preferences.keyboardCues,
        "Absent desktop settings leave both choices to the framework");
}
