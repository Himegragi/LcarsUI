#pragma once
// Windows desktop host: one native window that arranges `page` with `appearance`, paints it
// through OpenGL and routes native keyboard, pointer, clipboard and IME input into `ui`.
// Requires LCARS_BUILD_DESKTOP; core and layout never depend on it.
#include "lcars/layout.hpp"
#include "lcars/diagnostics.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lcars {
enum class DebugContext { build_default, on, off };
enum class RendererFailureKind { frame_error, device_reset, present_failed, resource_failed };
struct RendererFailure { RendererFailureKind kind; std::uint32_t native_code = 0; std::u16string message; };
enum class StartupFailureKind { window, context_version, context_creation, shader, resource };
struct StartupFailure { StartupFailureKind kind; std::u16string required, actual, message; };
struct RendererCapabilities { std::u16string version, vendor, renderer; bool debug_context = false, debug_messages = false, reset_detection = false; };
enum class CloseDecision { close, keep_open };
class Desktop;
struct CreateResult;
namespace detail { class DesktopHost; }
struct DesktopConfig {
    std::u16string title = u"LcarsUI";
    int width = 960, height = 720;  // client size in DIP
    std::function<void(const RenderDiagnostic&)> on_diagnostic;
    DebugContext debug_context = DebugContext::build_default;
    std::function<void(Desktop&, const RendererFailure&)> on_renderer_failed;
    std::function<CloseDecision(Desktop&)> on_close_requested;
};
struct CreateResult;
struct RunResult { Status status = Status::ok; std::optional<RendererFailure> failure; std::optional<StartupFailure> startup; };
class Desktop {
public:
    ~Desktop();
    static CreateResult create(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&, DesktopConfig = {});
    RunResult run();
    Result rebuild_renderer();
    Result close();
    void* native_window() const;
    Result set_title(std::u16string title);
    bool renderer_failed() const;
    RendererCapabilities capabilities() const;
private:
    friend class detail::DesktopHost;
    struct Impl;
    explicit Desktop(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
struct CreateResult { Status status; std::optional<StartupFailure> failure; std::unique_ptr<Desktop> desktop; };
RunResult run_desktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                DesktopConfig config = {});
}
