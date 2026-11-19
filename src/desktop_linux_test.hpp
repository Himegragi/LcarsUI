#pragma once
#include "isoduo/desktop.hpp"
#include <chrono>

namespace isoduo::detail {
struct RendererTestHooks;
struct DrawList;
struct GpuComparison;
struct LinuxClientGeometry;
struct LinuxFrameMeasurement {
    const Frame& frame;
    const LinuxClientGeometry& geometry;
    std::chrono::steady_clock::time_point presented;
    double layoutMs, listMs, submissionMs, gpuMs, delayMs;
    bool focused, animating;
};
struct LinuxAtlasStats {
    unsigned rasterizations, clears, batches, peakPages;
};
class LinuxDesktopTest {
public:
    static CreateResult create(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&,
                               DesktopConfig, RendererTestHooks*);
    static GpuComparison compareDrawList(Desktop&, const DrawList&, int width, int height);
    static Frame layoutFrame(Desktop&, int width, int height, float scale, std::string_view reveal = {});
    static GpuComparison compareFrame(Desktop&, int width, int height, float scale, Frame&,
                                      int fault = 0, std::string_view reveal = {});
    static void observeFrames(Desktop&, std::function<void(const Frame&, const LinuxClientGeometry&)>);
    // Opt-in native-window performance probe. The renderer timing excludes swap wait.
    static void measureFrames(Desktop&, std::chrono::milliseconds delay,
                              std::function<void(const LinuxFrameMeasurement&)>, std::function<void()> redraw);
    static void configureAtlas(Desktop&, int size, int pages);
    static LinuxAtlasStats atlasStats(const Desktop&);
};
}
