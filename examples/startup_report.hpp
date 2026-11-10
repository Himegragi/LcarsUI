#pragma once
#include "lcars/desktop.hpp"
#include "../src/utf.hpp"
#include <iostream>
inline void report_startup_failure(const std::optional<lcars::StartupFailure>& failure) {
    if (!failure) { std::cerr << "Desktop startup failed (no details)\n"; return; }
    const char* kind = "resource";
    switch (failure->kind) {
    case lcars::StartupFailureKind::window: kind = "window"; break;
    case lcars::StartupFailureKind::context_version: kind = "context_version"; break;
    case lcars::StartupFailureKind::context_creation: kind = "context_creation"; break;
    case lcars::StartupFailureKind::shader: kind = "shader"; break;
    case lcars::StartupFailureKind::resource: break;
    }
    std::cerr << "Desktop startup failed (" << kind << "): " << lcars::detail::utf8_from_utf16(failure->message) << '\n';
}
