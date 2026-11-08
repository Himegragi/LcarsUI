#pragma once
#include "settings_demo.hpp"
#include "failure_flow.hpp"
#include "lcars/desktop.hpp"
namespace settings_demo {
// Shared by the desktop entry point and host verification; prompt lifetime is supplied by the caller.
class CloseController {
    Demo& demo_;
    bool prompt_active_ = false;
public:
    explicit CloseController(Demo& demo) : demo_(demo) {}
    lcars::CloseDecision request(lcars::Desktop& desktop, NativePrompt& prompt) {
        if (!desktop.renderer_failed()) return demo_.request_close() ? lcars::CloseDecision::close : lcars::CloseDecision::keep_open;
        if (prompt_active_) return lcars::CloseDecision::keep_open;
        prompt_active_ = true;
        bool close = failure_close(prompt, demo_.is_dirty(), demo_.blank_name_draft(), [&] { return demo_.save_failed_renderer(); });
        prompt_active_ = false;
        return close ? lcars::CloseDecision::close : lcars::CloseDecision::keep_open;
    }
    void failure(lcars::Desktop& desktop, const lcars::RendererFailure& failure, NativePrompt& prompt) {
        if (prompt_active_) return;
        prompt_active_ = true;
        failure_flow(prompt, failure.message, [&] { return demo_.is_dirty(); }, [&] { return bool(desktop.rebuild_renderer()); }, [&] { return demo_.save_failed_renderer(); }, [&] { desktop.close(); }, [&] { return demo_.blank_name_draft(); });
        prompt_active_ = false;
    }
};
}
