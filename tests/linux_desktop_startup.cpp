#include "isoduo/desktop.hpp"
#include "check.hpp"
#include <cstdlib>

using namespace isoduo;

namespace {
// An empty backend requests automatic selection; an empty selection means no native session was found.
void expectWindowFailure(std::optional<DesktopBackendKind> requested, std::optional<DesktopBackendKind> selected) {
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
    expectWindowFailure(std::nullopt, std::nullopt);
    expectWindowFailure(DesktopBackendKind::wayland, DesktopBackendKind::wayland);
    expectWindowFailure(DesktopBackendKind::x11, DesktopBackendKind::x11);
    expectWindowFailure(DesktopBackendKind::windows, DesktopBackendKind::windows);
}
void bothDisplayVariables() {
    unsetenv("WAYLAND_SOCKET");
    setenv("DISPLAY", "invalid-display", 1);
    setenv("WAYLAND_DISPLAY", "/nonexistent/isoduo-test-wayland", 1);
    setenv("XDG_SESSION_TYPE", "x11", 1);
    expectWindowFailure(std::nullopt, DesktopBackendKind::x11);
    expectWindowFailure(DesktopBackendKind::wayland, DesktopBackendKind::wayland);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    expectWindowFailure(std::nullopt, DesktopBackendKind::wayland);
    expectWindowFailure(DesktopBackendKind::x11, DesktopBackendKind::x11);
}
}

int main() {
    const test::Case cases[] = {
        {"Unavailable native displays report structured failures", noDisplay},
        {"The login session chooses the backend unless explicitly overridden", bothDisplayVariables},
    };
    return test::run("Linux desktop startup", cases);
}
