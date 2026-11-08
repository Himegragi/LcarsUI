#include "diagnostic_queue.hpp"
#include <windows.h>
#include <algorithm>
#include <utility>

namespace lcars::detail {
DiagnosticQueue::DiagnosticQueue(Callback callback, const void* environment)
    : callback_(std::move(callback)), environment_(environment) {}
void DiagnosticQueue::push(RenderDiagnostic event) {
    if(pending_.size() == 256) { ++omitted_; return; }
    event.ui_environment = environment_;
    pending_.push_back(std::move(event));
}
void DiagnosticQueue::drain() {
    if(dispatching_) return;
    dispatching_ = true;
    auto count = pending_.size();
    while(count--) {
        auto event = std::move(pending_.front()); pending_.pop_front();
        event.omitted_before = std::exchange(omitted_, std::size_t{0});
        if(callback_) { try { callback_(event); } catch (...) {} }
#ifndef NDEBUG
        else { std::wstring text(event.code.begin(),event.code.end()); text += L"\n"; OutputDebugStringW(text.c_str()); }
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
