#pragma once
#include "settings_demo.hpp"
#include "failure_flow.hpp"
#include "isoduo/desktop.hpp"
namespace settingsDemo {
// Shared by the desktop entry point and host verification; prompt lifetime is supplied by the caller.
class CloseController {
    Demo& demo_;
    bool promptActive_ = false;
public:
    explicit CloseController(Demo& demo) : demo_(demo) {}
    isoduo::CloseDecision request(isoduo::Desktop& desktop, NativePrompt& prompt) {
        if (!desktop.rendererFailed()) return demo_.requestClose() ? isoduo::CloseDecision::close : isoduo::CloseDecision::keepOpen;
        if (promptActive_) return isoduo::CloseDecision::keepOpen;
        promptActive_ = true;
        bool close = failureClose(prompt, demo_.isDirty(), demo_.blankNameDraft(), [&] { return demo_.saveFailedRenderer(); });
        promptActive_ = false;
        return close ? isoduo::CloseDecision::close : isoduo::CloseDecision::keepOpen;
    }
    void failure(isoduo::Desktop& desktop, const isoduo::RendererFailure& failure, NativePrompt& prompt) {
        if (promptActive_) return;
        promptActive_ = true;
        failureFlow(prompt, failure.message, [&] { return demo_.isDirty(); }, [&] { return bool(desktop.rebuildRenderer()); }, [&] { return demo_.saveFailedRenderer(); }, [&] { desktop.close(); }, [&] { return demo_.blankNameDraft(); });
        promptActive_ = false;
    }
};
}
