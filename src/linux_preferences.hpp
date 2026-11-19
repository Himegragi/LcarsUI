#pragma once
#include <memory>
#include <optional>
#include <span>

struct pollfd;
typedef struct _GMainContext GMainContext;

namespace isoduo::detail {
struct LinuxDesktopPreferences {
    std::optional<bool> animations;
    std::optional<bool> keyboardCues;
};

// Missing values leave the shared UI's normal motion and focus rules in effect.
LinuxDesktopPreferences readLinuxDesktopPreferences();

// Owns the desktop preference subscriptions on the UI thread. Its sources
// wake the native poll alongside the display socket, even when the UI is idle.
class LinuxPreferenceMonitor {
public:
    LinuxPreferenceMonitor();
    ~LinuxPreferenceMonitor();
    bool changed();
    // Sources attached here run on the desktop's owner thread after native input dispatch.
    static GMainContext* context();
    int poll(std::span<pollfd> native, int timeout);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
