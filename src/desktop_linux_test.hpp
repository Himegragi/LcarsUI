#pragma once
#include "isoduo/desktop.hpp"

namespace isoduo::detail {
struct RendererTestHooks;
struct DrawList;
struct GpuComparison;
class LinuxDesktopTest {
public:
    static CreateResult create(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&,
                               DesktopConfig, RendererTestHooks*);
    static GpuComparison compareDrawList(Desktop&, const DrawList&, int width, int height);
    static Frame layoutFrame(Desktop&, int width, int height, float scale, std::string_view reveal = {});
    static GpuComparison compareFrame(Desktop&, int width, int height, float scale, Frame&,
                                      int fault = 0, std::string_view reveal = {});
};
}
