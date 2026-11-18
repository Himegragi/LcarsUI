#pragma once
#include "isoduo/desktop.hpp"
#include "isoduo/diagnostics.hpp"
#include <string>
#include <string_view>
namespace settingsDemo {
// Turns the renderer failure the demo is told about into the diagnostic record it keeps
// (TD01): the in-memory Diagnostics module stores it as the first failure, and a
// --diagnostics-out run saves it with the recent events.
inline std::string failureUtf8(std::u16string_view text) {
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
inline isoduo::RenderDiagnostic failureDiagnostic(const isoduo::RendererFailure& failure) {
    isoduo::RenderDiagnostic d;
    d.backend = u"OpenGL";
    d.severity = isoduo::DiagnosticSeverity::error;
    d.kind = failure.kind == isoduo::RendererFailureKind::deviceReset ? isoduo::DiagnosticKind::deviceReset : isoduo::DiagnosticKind::other;
    switch (failure.kind) {
    case isoduo::RendererFailureKind::frameError: d.code = "renderer_failed.frame_error"; break;
    case isoduo::RendererFailureKind::deviceReset: d.code = "renderer_failed.device_reset"; break;
    case isoduo::RendererFailureKind::presentFailed: d.code = "renderer_failed.present_failed"; break;
    case isoduo::RendererFailureKind::resourceFailed: d.code = "renderer_failed.resource_failed"; break;
    }
    d.nativeCode = failure.nativeCode;
    d.nativeMessage = failureUtf8(failure.message);
    return d;
}
}
