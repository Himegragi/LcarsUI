#pragma once
#include "lcars/desktop.hpp"
#include "lcars/diagnostics.hpp"
#include <string>
#include <string_view>
namespace settings_demo {
// Turns the renderer failure the demo is told about into the diagnostic record it keeps
// (TD01): the in-memory Diagnostics module stores it as the first failure, and a
// --diagnostics-out run saves it with the recent events.
inline std::string failure_utf8(std::u16string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
        else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}
inline lcars::RenderDiagnostic failure_diagnostic(const lcars::RendererFailure& failure) {
    lcars::RenderDiagnostic d;
    d.backend = u"OpenGL";
    d.severity = lcars::DiagnosticSeverity::error;
    d.category = failure.kind == lcars::RendererFailureKind::device_reset ? lcars::DiagnosticCategory::device_reset : lcars::DiagnosticCategory::other;
    switch (failure.kind) {
    case lcars::RendererFailureKind::frame_error: d.code = "renderer_failed.frame_error"; break;
    case lcars::RendererFailureKind::device_reset: d.code = "renderer_failed.device_reset"; break;
    case lcars::RendererFailureKind::present_failed: d.code = "renderer_failed.present_failed"; break;
    case lcars::RendererFailureKind::resource_failed: d.code = "renderer_failed.resource_failed"; break;
    }
    d.native_code = failure.native_code;
    d.native_message = failure_utf8(failure.message);
    return d;
}
}
