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
enum class DiagnosticKind { apiMisuse, performance, shader, resource, deviceReset, other };
struct RenderDiagnostic {
    std::u16string backend;
    DiagnosticSeverity severity = DiagnosticSeverity::warning;
    DiagnosticKind kind = DiagnosticKind::other;
    std::string code;
    std::int64_t nativeCode = 0;
    std::string nativeMessage;
    std::chrono::system_clock::time_point receivedAt = std::chrono::system_clock::now();
    std::optional<std::chrono::system_clock::time_point> sourceTime;
    const void* uiEnvironment = nullptr;
    std::string componentId;
    std::string operationId;
    std::string backendDetail;
    std::size_t omittedBefore = 0;
    bool backendDetailUninterpreted = false;
};
// Loaded records cannot retain a live UI environment pointer. Backend details are
// preserved as opaque data and marked uninterpreted.
struct DiagnosticLoadResult { std::optional<RenderDiagnostic> firstFailure; std::vector<RenderDiagnostic> records; bool incomplete = false; };
class Diagnostics {
public:
    explicit Diagnostics(std::size_t capacity = 64);
    // Record observations normally; call recordFailure for an actual failure.
    // Severity alone does not imply that the renderer failed.
    void record(const RenderDiagnostic& event);
    void recordFailure(const RenderDiagnostic& event);
    const std::optional<RenderDiagnostic>& firstFailure() const;
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
