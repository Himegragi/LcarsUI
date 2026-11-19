#include "isoduo/desktop.hpp"
#include "check.hpp"
#include <cstdlib>

using namespace isoduo;

namespace {
void expectWindowFailure(DesktopBackend requested, DesktopBackend selected) {
    Ui ui;
    ScrollState scroll;
    auto page = [] { return layout::column({}); };
    DesktopConfig config;
    config.backend = requested;
    auto created = Desktop::create(ui, webAppearance(), page, scroll, config);
    test::check(created.status == Status::failed, "Unavailable display fails creation");
    test::check(!created.desktop && created.failure, "Failed creation carries a diagnostic and no desktop");
    test::check(created.failure->kind == StartupFailureKind::window, "Unavailable display reports a window failure");
    test::check(created.failure->backend == selected && !created.failure->message.empty(),
        "Failure identifies the selected backend and reason");
    auto result = runDesktop(ui, webAppearance(), page, scroll, config);
    test::check(result.status == Status::failed && result.startup, "runDesktop propagates startup failure");
    test::check(result.startup->backend == selected, "runDesktop reports selected backend");
}
void noDisplay() {
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("WAYLAND_SOCKET");
    unsetenv("XDG_SESSION_TYPE");
    unsetenv("XDG_RUNTIME_DIR");
    expectWindowFailure(DesktopBackend::automatic, DesktopBackend::automatic);
    expectWindowFailure(DesktopBackend::wayland, DesktopBackend::wayland);
    expectWindowFailure(DesktopBackend::x11, DesktopBackend::x11);
    expectWindowFailure(DesktopBackend::windows, DesktopBackend::windows);
}
void bothDisplayVariables() {
    unsetenv("WAYLAND_SOCKET");
    setenv("DISPLAY", "invalid-display", 1);
    setenv("WAYLAND_DISPLAY", "/nonexistent/isoduo-test-wayland", 1);
    setenv("XDG_SESSION_TYPE", "x11", 1);
    expectWindowFailure(DesktopBackend::automatic, DesktopBackend::x11);
    expectWindowFailure(DesktopBackend::wayland, DesktopBackend::wayland);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    expectWindowFailure(DesktopBackend::automatic, DesktopBackend::wayland);
    expectWindowFailure(DesktopBackend::x11, DesktopBackend::x11);
}
}

int main() {
    const test::Case cases[] = {
        {"Unavailable native displays report structured failures", noDisplay},
        {"The login session chooses the backend unless explicitly overridden", bothDisplayVariables},
    };
    return test::run("Linux desktop startup", cases);
}
