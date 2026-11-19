#pragma once
#include "isoduo/semantics.hpp"
#include <memory>

typedef struct _GMainContext GMainContext;

namespace isoduo::detail {
class LinuxWindow;
class LinuxAccessibility {
public:
    LinuxAccessibility(GMainContext*, Ui&, LinuxWindow&, const Frame&, std::u16string_view title);
    ~LinuxAccessibility();
    void title(std::u16string_view value);
    void flushLayout();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
