#pragma once
#include "isoduo/diagnostics.hpp"
#include "isoduo/fonts.hpp"
#include <deque>
#include <functional>
#include <mutex>

namespace isoduo::detail {
class DiagnosticQueue {
public:
    using Callback = std::function<void(const RenderDiagnostic&)>;
    DiagnosticQueue(Callback callback, const void* environment);
    void push(RenderDiagnostic event);
    void drain();
    using Output = std::function<void(const std::wstring&)>;
    void setOutput(Output output) { output_ = std::move(output); }
private:
    Callback callback_;
    Output output_;
    std::mutex mutex_;
    const void* environment_;
    std::deque<RenderDiagnostic> pending_;
    std::size_t omitted_ = 0;
    bool dispatching_ = false;
};
void enqueueFontDiagnostics(DiagnosticQueue& queue, long catalogError, const FontResolution& resolution);
}
