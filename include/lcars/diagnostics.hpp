#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace lcars {
enum class DiagnosticSeverity { info, warning, error };
enum class DiagnosticCategory { api_misuse, performance, shader, resource, device_reset, other };
struct RenderDiagnostic {
    std::u16string backend;
    DiagnosticSeverity severity = DiagnosticSeverity::warning;
    DiagnosticCategory category = DiagnosticCategory::other;
    std::string code;
    std::int64_t native_code = 0;
    std::string native_message;
    std::chrono::system_clock::time_point received_at = std::chrono::system_clock::now();
    std::optional<std::chrono::system_clock::time_point> source_time;
    const void* ui_environment = nullptr;
    std::string component_id;
    std::string operation_id;
    std::string backend_detail;
};
}
