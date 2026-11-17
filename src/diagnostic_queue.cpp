#include "diagnostic_queue.hpp"
#include "utf.hpp"
#include <windows.h>
#include <algorithm>
#include <utility>

namespace lcars::detail {
DiagnosticQueue::DiagnosticQueue(Callback callback, const void* environment)
    : callback_(std::move(callback)), environment_(environment) {}
void DiagnosticQueue::push(RenderDiagnostic event) {
    std::lock_guard lock(mutex_);
    if(pending_.size() == 256) { ++omitted_; return; }
    event.uiEnvironment = environment_;
    pending_.push_back(std::move(event));
}
void DiagnosticQueue::drain() {
    if(dispatching_) return;
    dispatching_ = true;
    std::deque<RenderDiagnostic> batch;
    std::size_t omitted;
    { std::lock_guard lock(mutex_); batch.swap(pending_); omitted=std::exchange(omitted_,std::size_t{0}); }
    for (auto& event : batch) {
        event.omittedBefore = std::exchange(omitted,std::size_t{0});
        if(callback_) { try { callback_(event); } catch (...) { callback_ = {}; } }
#ifndef NDEBUG
        else {
            std::wstring text(event.backend.begin(),event.backend.end());
            text += L" [" + std::to_wstring(event.nativeCode) + L"] ";
            text.append(event.code.begin(),event.code.end()); text += L" ";
            auto message = utf16FromUtf8(event.nativeMessage);
            text.append(message.begin(), message.end());
            text += L"\n"; if(output_) output_(text); else OutputDebugStringW(text.c_str());
        }
#endif
    }
    dispatching_ = false;
}
void enqueueFontDiagnostics(DiagnosticQueue& queue,long catalogError,const FontResolution& resolution) {
    if(catalogError<0) {
        RenderDiagnostic event; event.backend=u"DirectWrite"; event.code="font.catalog_unavailable";
        event.severity=DiagnosticSeverity::error; event.kind=DiagnosticKind::resource;
        event.nativeCode=catalogError; event.nativeMessage="DirectWrite system font catalog unavailable";
        queue.push(std::move(event));
    }
    if(!resolution.requestedAvailable) {
        RenderDiagnostic event; event.backend=u"DirectWrite"; event.code="font.family_unavailable";
        event.severity=DiagnosticSeverity::warning; event.kind=DiagnosticKind::resource;
        event.nativeMessage="requested="+utf8FromUtf16(resolution.requested)+"; fallback="+utf8FromUtf16(resolution.primary);
        queue.push(std::move(event));
    }
}
}
