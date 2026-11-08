#include "diagnostic_queue.hpp"
#include <windows.h>
#include <algorithm>
#include <utility>

namespace lcars::detail {
DiagnosticQueue::DiagnosticQueue(Callback callback, const void* environment)
    : callback_(std::move(callback)), environment_(environment) {}
void DiagnosticQueue::push(RenderDiagnostic event) {
    std::lock_guard lock(mutex_);
    if(pending_.size() == 256) { ++omitted_; return; }
    event.ui_environment = environment_;
    pending_.push_back(std::move(event));
}
void DiagnosticQueue::drain() {
    if(dispatching_) return;
    dispatching_ = true;
    std::deque<RenderDiagnostic> batch;
    std::size_t omitted;
    { std::lock_guard lock(mutex_); batch.swap(pending_); omitted=std::exchange(omitted_,std::size_t{0}); }
    for (auto& event : batch) {
        event.omitted_before = std::exchange(omitted,std::size_t{0});
        if(callback_) { try { callback_(event); } catch (...) { callback_ = {}; } }
#ifndef NDEBUG
        else {
            std::wstring text(event.backend.begin(),event.backend.end());
            text += L" [" + std::to_wstring(event.native_code) + L"] ";
            text.append(event.code.begin(),event.code.end()); text += L" ";
            auto size=MultiByteToWideChar(CP_UTF8,0,event.native_message.data(),static_cast<int>(event.native_message.size()),nullptr,0);
            if (size>0) { std::wstring message(static_cast<std::size_t>(size),L'\0'); MultiByteToWideChar(CP_UTF8,0,event.native_message.data(),static_cast<int>(event.native_message.size()),message.data(),size); text+=message; }
            text += L"\n"; if(output_) output_(text); else OutputDebugStringW(text.c_str());
        }
#endif
    }
    dispatching_ = false;
}
void enqueue_font_diagnostics(DiagnosticQueue& queue,long catalog_error,const FontResolution& resolution) {
    if(catalog_error<0) {
        RenderDiagnostic event; event.backend=u"DirectWrite"; event.code="font.catalog_unavailable";
        event.severity=DiagnosticSeverity::error; event.category=DiagnosticCategory::resource;
        event.native_code=catalog_error; event.native_message="DirectWrite system font catalog unavailable";
        queue.push(std::move(event));
    }
    if(!resolution.requested_available) {
        auto utf8=[](std::u16string_view value) {
            auto data=reinterpret_cast<const wchar_t*>(value.data());
            int n=WideCharToMultiByte(CP_UTF8,0,data,static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
            std::string result(n,'\0');
            if(n) WideCharToMultiByte(CP_UTF8,0,data,static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);
            return result;
        };
        RenderDiagnostic event; event.backend=u"DirectWrite"; event.code="font.family_unavailable";
        event.severity=DiagnosticSeverity::warning; event.category=DiagnosticCategory::resource;
        event.native_message="requested="+utf8(resolution.requested)+"; fallback="+utf8(resolution.primary);
        queue.push(std::move(event));
    }
}
}
