#pragma once
#include <sys/types.h>

namespace settingsDemo {
// X11 only. Makes the top-level window that process `child` maps transient for `parent`, the demo's main
// window, so the window manager keeps the prompt above it. Zenity 4 accepts --attach but ignores it, so the
// demo sets WM_TRANSIENT_FOR on the prompt's window itself. Waits while the child runs, at most timeoutMs.
// Returns true once the hint is set. Uses its own display connection, so any thread may call it.
bool attachPromptToParent(pid_t child, unsigned long parent, int timeoutMs);
}
