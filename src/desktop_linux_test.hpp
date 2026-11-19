#pragma once
#include "isoduo/desktop.hpp"

namespace isoduo::detail {
struct RendererTestHooks;
class LinuxDesktopTest {
public:
    static CreateResult create(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&,
                               DesktopConfig, RendererTestHooks*);
};
}
