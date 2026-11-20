#pragma once
// Native desktop host: one native window that arranges `page` with `appearance`, paints it
// through OpenGL and routes native keyboard, pointer, clipboard and IME input into `ui`.
// Requires ISODUO_BUILD_DESKTOP; core and layout never depend on it.
#include "isoduo/layout.hpp"
#include "isoduo/diagnostics.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace isoduo {
// The native desktop backend a host runs on.
enum class DesktopBackendKind { windows, wayland, x11 };
#ifdef __linux__
// Borrowed native objects. On Wayland display/surface are wl_display*/wl_surface*.
// On X11 display is Display* and windowId is the XID. Valid until the host closes.
struct NativeWindow {
    DesktopBackendKind backend;
    void* display;
    void* surface = nullptr;
    unsigned long windowId = 0;
};
#endif
enum class DebugContext { buildDefault, on, off };
enum class RendererFailureKind { frameError, deviceReset, presentFailed, resourceFailed };
struct RendererFailure { RendererFailureKind kind; std::uint32_t nativeCode = 0; std::u16string message; };
enum class StartupFailureKind { window, contextVersion, contextCreation, shader, resource };
// backend is the backend that failed; it is empty when automatic selection found no native desktop session.
struct StartupFailure { StartupFailureKind kind; std::u16string required, actual, message; std::optional<DesktopBackendKind> backend = std::nullopt; };
struct RendererCapabilities { std::u16string version, vendor, renderer; bool debugContext = false, debugMessages = false, resetDetection = false; };
enum class CloseDecision { close, keepOpen };
class Desktop;
struct CreateResult;
namespace detail { class DesktopHost; class LinuxDesktopTest; }
struct DesktopConfig {
    std::u16string title = u"Isoduo";
    int width = 960, height = 720;  // client size in DIP
    std::function<void(const RenderDiagnostic&)> onDiagnostic;
    DebugContext debugContext = DebugContext::buildDefault;
    std::function<void(Desktop&, const RendererFailure&)> onRendererFailed;
    std::function<CloseDecision(Desktop&)> onCloseRequested;
    // Fixed per host at creation. Disabling prevents assistive tools from reading or
    // operating Isoduo components through platform accessibility (UIA on Windows, AT-SPI on Linux).
    // Semantic queries, operations and events remain available; native window
    // information supplied by the operating system may still be exposed.
    bool platformAccessibilityEnabled = true;
    // Empty selects automatically: on Linux the login session decides. An explicit backend never falls back.
    std::optional<DesktopBackendKind> backend;
};
struct CreateResult;
struct RunResult { Status status = Status::ok; std::optional<RendererFailure> failure; std::optional<StartupFailure> startup; };
class Desktop {
public:
    ~Desktop();
    static CreateResult create(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&, DesktopConfig = {});
    RunResult run();
    Result rebuildRenderer();
    Result close();
#ifdef __linux__
    const NativeWindow* nativeWindow() const;
#else
    void* nativeWindow() const;
#endif
    Result setTitle(std::u16string title);
    // The one member any thread may call. Queues work to run once on the desktop thread inside run(), after
    // the native input already read, in the order posted; it wakes an idle host. Returns notFound once the
    // host has closed: work that has not run by then is destroyed without running. The caller keeps the
    // Desktop alive while it posts.
    Result post(std::function<void()> work);
    bool rendererFailed() const;
    RendererCapabilities capabilities() const;
private:
    friend class detail::DesktopHost;
    friend class detail::LinuxDesktopTest;
    struct Impl;
    explicit Desktop(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
struct CreateResult { Status status; std::optional<StartupFailure> failure; std::unique_ptr<Desktop> desktop; };
RunResult runDesktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                DesktopConfig config = {});
}
