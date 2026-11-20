#include "linux_prompt_parent.hpp"
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <sys/wait.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

// Uses XCB rather than Xlib: an X error, such as a listed window closing before its properties are read, comes
// back with that one reply instead of reaching Xlib's process-wide error handler, which exits the process.
namespace settingsDemo {
namespace {
using Connection = std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)>;
template <class T> using Reply = std::unique_ptr<T, decltype(&std::free)>;

xcb_atom_t existingAtom(xcb_connection_t* connection, const char* name) {
    const auto cookie = xcb_intern_atom(connection, true, static_cast<std::uint16_t>(std::strlen(name)), name);
    Reply<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(connection, cookie, nullptr), std::free);
    return reply ? reply->atom : XCB_ATOM_NONE;
}
// An empty result when the property is missing, has another shape, or the window is gone.
std::vector<std::uint32_t> cardinals(xcb_connection_t* connection, xcb_window_t window, xcb_atom_t property,
                                     xcb_atom_t type, std::uint32_t maxCount) {
    const auto cookie = xcb_get_property(connection, false, window, property, type, 0, maxCount);
    xcb_generic_error_t* rawError = nullptr;
    Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(connection, cookie, &rawError), std::free);
    Reply<xcb_generic_error_t> error(rawError, std::free);
    if (error || !reply || reply->type != type || reply->format != 32) return {};
    const auto* values = static_cast<const std::uint32_t*>(xcb_get_property_value(reply.get()));
    return {values, values + xcb_get_property_value_length(reply.get()) / 4};
}
// True while the child runs. The child is not reaped here (WNOWAIT); ECHILD means GLib already reaped it.
bool running(pid_t child) {
    siginfo_t info{};
    if (waitid(P_PID, static_cast<id_t>(child), &info, WEXITED | WNOHANG | WNOWAIT) != 0) return errno == EINTR;
    return info.si_pid == 0;
}
}

bool attachPromptToParent(pid_t child, unsigned long parent, int timeoutMs) {
    int screenNumber = 0;
    Connection connection(xcb_connect(nullptr, &screenNumber), xcb_disconnect);
    if (!parent || xcb_connection_has_error(connection.get())) return false;
    auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
    for (int i = 0; i < screenNumber && screens.rem; ++i) xcb_screen_next(&screens);
    if (!screens.rem) return false;
    const auto root = screens.data->root;
    const auto clientList = existingAtom(connection.get(), "_NET_CLIENT_LIST");
    const auto pidAtom = existingAtom(connection.get(), "_NET_WM_PID");
    if (clientList == XCB_ATOM_NONE || pidAtom == XCB_ATOM_NONE) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline && running(child)) {
        if (xcb_connection_has_error(connection.get())) return false;
        for (auto window : cardinals(connection.get(), root, clientList, XCB_ATOM_WINDOW, 4096)) {
            const auto pid = cardinals(connection.get(), window, pidAtom, XCB_ATOM_CARDINAL, 1);
            if (pid.size() != 1 || static_cast<pid_t>(pid[0]) != child) continue;
            const auto owner = static_cast<xcb_window_t>(parent);
            const auto cookie = xcb_change_property_checked(connection.get(), XCB_PROP_MODE_REPLACE, window,
                                                            XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1, &owner);
            // The prompt may close between the lookup and this request; then there is nothing left to attach.
            Reply<xcb_generic_error_t> error(xcb_request_check(connection.get(), cookie), std::free);
            return !error;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return false;
}
}
