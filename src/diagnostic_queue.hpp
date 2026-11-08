#pragma once
#include "lcars/diagnostics.hpp"
#include "lcars/fonts.hpp"
#include <deque>
#include <functional>

namespace lcars::detail {
class DiagnosticQueue {
public:
    using Callback = std::function<void(const RenderDiagnostic&)>;
    DiagnosticQueue(Callback callback, const void* environment);
    void push(RenderDiagnostic event);
    void drain();
private:
    Callback callback_;
    const void* environment_;
    std::deque<RenderDiagnostic> pending_;
    std::size_t omitted_ = 0;
    bool dispatching_ = false;
};
void enqueue_font_diagnostics(DiagnosticQueue& queue, long catalog_error, const FontResolution& resolution);
}
