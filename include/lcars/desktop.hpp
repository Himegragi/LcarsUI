#pragma once
// Windows desktop host: one native window that arranges `page` with `appearance`, paints it
// through OpenGL and routes native keyboard, pointer, clipboard and IME input into `ui`.
// Requires LCARS_BUILD_DESKTOP; core and layout never depend on it.
#include "lcars/layout.hpp"
#include <functional>
#include <string>

namespace lcars {
struct DesktopOptions {
    std::u16string title = u"LcarsUI";
    int width = 960, height = 720;  // client size in DIP
};
int run_desktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                DesktopOptions options = {});
}
