#include "linux_prompt_parent.hpp"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/wait.h>
#include <cerrno>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace settingsDemo {
namespace {
std::vector<Window> managedWindows(Display* display) {
    const Atom list = XInternAtom(display, "_NET_CLIENT_LIST", True);
    std::vector<Window> windows;
    if (list == None) return windows;
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, DefaultRootWindow(display), list, 0, 4096, False, XA_WINDOW, &type, &format,
                           &count, &remaining, &data) == Success && data && type == XA_WINDOW && format == 32) {
        const auto* ids = reinterpret_cast<const unsigned long*>(data);
        windows.assign(ids, ids + count);
    }
    if (data) XFree(data);
    return windows;
}
bool ownedBy(Display* display, Window window, pid_t child) {
    const Atom pid = XInternAtom(display, "_NET_WM_PID", True);
    if (pid == None) return false;
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    bool match = false;
    if (XGetWindowProperty(display, window, pid, 0, 1, False, XA_CARDINAL, &type, &format, &count, &remaining,
                           &data) == Success && data && type == XA_CARDINAL && format == 32 && count == 1)
        match = static_cast<pid_t>(*reinterpret_cast<const unsigned long*>(data)) == child;
    if (data) XFree(data);
    return match;
}
// True while the child runs. The child is not reaped here (WNOWAIT); ECHILD means GLib already reaped it.
bool running(pid_t child) {
    siginfo_t info{};
    if (waitid(P_PID, static_cast<id_t>(child), &info, WEXITED | WNOHANG | WNOWAIT) != 0) return errno == EINTR;
    return info.si_pid == 0;
}
}

bool attachPromptToParent(pid_t child, unsigned long parent, int timeoutMs) {
    std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), XCloseDisplay);
    if (!display || !parent) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline && running(child)) {
        for (auto window : managedWindows(display.get())) {
            if (!ownedBy(display.get(), window, child)) continue;
            XSetTransientForHint(display.get(), window, static_cast<Window>(parent));
            XSync(display.get(), False);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return false;
}
}
