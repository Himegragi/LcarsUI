#pragma once
#include <functional>
#include <string>
#include <string_view>
namespace settings_demo {
enum class SaveChoice { save, discard, cancel };
struct NativePrompt {
    virtual ~NativePrompt() = default;
    virtual bool retry(std::u16string_view reason) = 0;
    virtual SaveChoice save(bool blank_name) = 0;
    virtual void error(std::u16string_view message) = 0;
};
inline bool failure_close(NativePrompt& prompt, bool dirty, bool blank_name,
                          const std::function<std::u16string()>& save) {
    if (!dirty) return true;
    switch (prompt.save(blank_name)) {
    case SaveChoice::discard: return true;
    case SaveChoice::cancel: return false;
    case SaveChoice::save: {
        auto error = save();
        if (error.empty()) return true;
        prompt.error(error); return false;
    }
    }
    return false;
}
inline void failure_flow(NativePrompt& prompt, std::u16string_view reason,
                         const std::function<bool()>& dirty, const std::function<bool()>& rebuild,
                         const std::function<std::u16string()>& save, const std::function<void()>& close,
                         const std::function<bool()>& blank_name = [] { return false; }) {
    for (;;) {
        if (prompt.retry(reason)) { if (rebuild()) return; }
        else if (failure_close(prompt, dirty(), blank_name(), save)) { close(); return; }
    }
}
}
