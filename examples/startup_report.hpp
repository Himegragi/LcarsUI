#pragma once
#include "isoduo/desktop.hpp"
#include "../src/utf.hpp"
#include <iostream>
inline void reportStartupFailure(const std::optional<isoduo::StartupFailure>& failure) {
    if (!failure) { std::cerr << "Desktop startup failed (no details)\n"; return; }
    const char* kind = "resource";
    switch (failure->kind) {
    case isoduo::StartupFailureKind::window: kind = "window"; break;
    case isoduo::StartupFailureKind::contextVersion: kind = "context_version"; break;
    case isoduo::StartupFailureKind::contextCreation: kind = "context_creation"; break;
    case isoduo::StartupFailureKind::shader: kind = "shader"; break;
    case isoduo::StartupFailureKind::resource: break;
    }
    std::cerr << "Desktop startup failed (" << kind << "): " << isoduo::detail::utf8FromUtf16(failure->message) << '\n';
}
