#pragma once
#include <windows.h>
#include "render.hpp"
#include "lcars/desktop.hpp"
#include <filesystem>
#include <chrono>
#include <memory>
namespace lcars::detail {
struct RendererTestHooks {
    bool fail_context_33 = false, fail_rebuild_once = false, hide_robustness = false, hide_khr_debug = false, misuse_once = false;
    std::string fragment_shader_override;
    int gl_error_at_frame = 0, reset_at_frame = 0;
    int rebuild_attempts = 0;
    unsigned injected_error = 0x0505;
};
struct StartupError { StartupFailure failure; };
struct FrameError { RendererFailure failure; std::vector<unsigned> codes; };
RenderDiagnostic frame_diagnostic(unsigned code);
class Renderer {
public:
    explicit Renderer(HWND window, RendererTestHooks* hooks = nullptr, DebugContext debug = DebugContext::build_default, std::function<void(RenderDiagnostic)> sink = {});
    ~Renderer();
    double draw(const DrawList& list, int width, int height, bool measure = false, bool offscreen = false, double* submission_ms = nullptr, std::chrono::steady_clock::time_point* presented = nullptr);
    Image read_back(const DrawList& list, int width, int height);
    std::string name() const;
    RendererCapabilities capabilities() const;
    void restore_atlas(const std::vector<const AtlasPage*>& pages);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
