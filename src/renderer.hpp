#pragma once
#include <windows.h>
#include "render.hpp"
#include "lcars/desktop.hpp"
#include <filesystem>
#include <chrono>
#include <memory>
namespace lcars::detail {
struct RendererTestHooks {
    bool failContext33 = false, failRebuildOnce = false, hideRobustness = false, hideKhrDebug = false, misuseOnce = false;
    std::string fragmentShaderOverride;
    std::string startupResourceError;
    int errorAtFrame = 0, resetAtFrame = 0;
    // From this frame on the backend reports a lost device on every error query, as after a real TDR.
    int contextLostAtFrame = 0;
    int rebuildAttempts = 0;
    unsigned injectedError = 0x0505;
};
struct StartupError { StartupFailure failure; };
struct FrameError { RendererFailure failure; std::vector<unsigned> codes; };
RenderDiagnostic frameDiagnostic(unsigned code);
class Renderer {
public:
    explicit Renderer(HWND window, RendererTestHooks* hooks = nullptr, DebugContext debug = DebugContext::buildDefault, std::function<void(RenderDiagnostic)> sink = {});
    ~Renderer();
    double draw(const DrawList& list, int width, int height, bool measure = false, bool offscreen = false, double* submissionMs = nullptr, std::chrono::steady_clock::time_point* presented = nullptr);
    Image readBack(const DrawList& list, int width, int height);
    std::string name() const;
    RendererCapabilities capabilities() const;
    void restoreAtlas(const std::vector<const AtlasPage*>& pages);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
