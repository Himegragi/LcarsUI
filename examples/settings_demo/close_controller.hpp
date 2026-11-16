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
        if (!desktop.rendererFailed()) return demo_.request_close() ? lcars::CloseDecision::close : lcars::CloseDecision::keepOpen;
        if (prompt_active_) return lcars::CloseDecision::keepOpen;
        prompt_active_ = true;
        bool close = failure_close(prompt, demo_.is_dirty(), demo_.blank_name_draft(), [&] { return demo_.save_failed_renderer(); });
        prompt_active_ = false;
        return close ? lcars::CloseDecision::close : lcars::CloseDecision::keepOpen;
    }
    void failure(lcars::Desktop& desktop, const lcars::RendererFailure& failure, NativePrompt& prompt) {
        if (prompt_active_) return;
        prompt_active_ = true;
        failure_flow(prompt, failure.message, [&] { return demo_.is_dirty(); }, [&] { return bool(desktop.rebuildRenderer()); }, [&] { return demo_.save_failed_renderer(); }, [&] { desktop.close(); }, [&] { return demo_.blank_name_draft(); });
        prompt_active_ = false;
    }
};
}
