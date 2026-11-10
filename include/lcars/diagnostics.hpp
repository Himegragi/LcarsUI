#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <deque>
#include <iosfwd>

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
    std::size_t omitted_before = 0;
    bool backend_detail_uninterpreted = false;
};
// Loaded records cannot retain a live UI environment pointer. Backend details are
// preserved as opaque data and marked uninterpreted.
struct DiagnosticLoadResult { std::optional<RenderDiagnostic> first_failure; std::vector<RenderDiagnostic> records; bool incomplete = false; };
class Diagnostics {
public:
    explicit Diagnostics(std::size_t capacity = 64);
    // Record observations normally; call record_failure for an actual failure.
    // Severity alone does not imply that the renderer failed.
    void record(const RenderDiagnostic& event);
    void record_failure(const RenderDiagnostic& event);
    const std::optional<RenderDiagnostic>& first_failure() const;
    const std::deque<RenderDiagnostic>& recent() const;
    // Save the first failure and recent events to a versioned, checksummed stream.
    bool save(std::ostream& out) const;
    static DiagnosticLoadResult load(std::istream& in);
private:
    std::size_t capacity_;
    std::optional<RenderDiagnostic> first_;
    std::deque<RenderDiagnostic> recent_;
};
}
