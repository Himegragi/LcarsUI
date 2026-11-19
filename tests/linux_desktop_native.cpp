#include "isoduo/desktop.hpp"
#include "desktop_linux_test.hpp"
#include "renderer.hpp"
#include "check.hpp"
#include "utf.hpp"
#include <cstdio>
#include <thread>

using namespace isoduo;
using namespace isoduo::detail;

namespace {
void check(bool condition, const char* expectation) { test::check(condition, expectation); }
void printCapabilities(const RendererCapabilities& caps) {
    std::cout << "Version " << utf8FromUtf16(caps.version)
              << "\nVendor " << utf8FromUtf16(caps.vendor)
              << "\nRenderer " << utf8FromUtf16(caps.renderer)
              << "\nDebug context " << caps.debugContext << ", messages " << caps.debugMessages
              << ", reset detection " << caps.resetDetection << '\n';
    int major = 0, minor = 0;
    check(std::sscanf(utf8FromUtf16(caps.version).c_str(), "%d.%d", &major, &minor) == 2 &&
        (major > 3 || (major == 3 && minor >= 3)), "Context version is at least 3.3");
    check(!caps.vendor.empty() && !caps.renderer.empty(), "Context vendor and renderer are reported");
}
void checkStartup(const CreateResult& result) {
    if (result.failure) {
        std::cerr << "Startup failed " << static_cast<int>(result.failure->kind) << ' '
                  << utf8FromUtf16(result.failure->message) << " actual " << utf8FromUtf16(result.failure->actual) << '\n';
    }
    check(result.status == Status::ok && !!result.desktop, "Native desktop opens");
}
void checkFaults(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    auto page = [] { return layout::column({}); };
    auto create = [&](RendererTestHooks& hooks) {
        auto result = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
        if (!result.desktop)
            check(result.status == Status::failed && result.failure && result.failure->backend == config.backend,
                "Structured startup failure names the selected backend");
        return result;
    };
    RendererTestHooks hooks;
    hooks.failContext33 = true;
    auto result = create(hooks);
    check(result.failure && result.failure->kind == StartupFailureKind::contextVersion &&
        result.failure->required == u"OpenGL 3.3 core" && result.failure->actual == u"2.1", "Version failure includes both versions");
    hooks = {};
    hooks.fragmentShaderOverride = "invalid shader";
    result = create(hooks);
    check(result.failure && result.failure->kind == StartupFailureKind::shader &&
        !result.failure->message.empty(), "Shader compile failure includes driver message");
    hooks = {};
    hooks.startupResourceError = "resource 中文";
    result = create(hooks);
    check(result.failure && result.failure->kind == StartupFailureKind::resource &&
        result.failure->message == u"resource 中文", "Resource error keeps its Unicode text");

    hooks = {};
    hooks.hideKhrDebug = hooks.hideRobustness = true;
    config.debugContext = DebugContext::on;
    result = create(hooks);
    checkStartup(result);
    auto caps = result.desktop->capabilities();
    check(caps.debugContext && !caps.debugMessages && !caps.resetDetection,
        "Debug context remains available without optional messages or reset detection");
    auto* desktop = result.desktop.get();
    Status status = Status::ok;
    std::thread worker([&] { status = desktop->close().status; });
    worker.join();
    check(status == Status::wrongThread, "Wrong thread cannot close");
    check(desktop->nativeWindow() != nullptr, "Wrong-thread close preserves the window");
    check(bool(desktop->close()), "Owner can close before running");
    check(desktop->run().status == Status::ok && !desktop->nativeWindow(), "Run returns and releases closed window");
    result.desktop.reset();

    hooks = {};
    hooks.resetAtFrame = 1;
    config.debugContext = DebugContext::off;
    int recovered = 0;
    const NativeWindow* reference = nullptr;
    config.onRendererFailed = [&](Desktop& host, const RendererFailure& failure) {
        check(failure.kind == RendererFailureKind::deviceReset, "Run reports a simulated graphics reset");
        check(host.rendererFailed(), "Host enters renderer failure");
        check(bool(host.rebuildRenderer()), "Owner rebuilds renderer explicitly");
        check(!host.rendererFailed() && host.nativeWindow() == reference, "Rebuild preserves the native window reference");
        ++recovered;
        host.close();
    };
    result = create(hooks);
    checkStartup(result);
    reference = result.desktop->nativeWindow();
    check(result.desktop->run().status == Status::ok && recovered == 1, "Run returns after the recovery callback closes");
    std::cout << "PASS startup fixtures, close thread, renderer rebuild\n";
}
void lowVersion(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    auto result = Desktop::create(ui, webAppearance(), [] { return layout::column({}); }, scroll, config);
    check(!result.desktop && result.failure && result.failure->backend == config.backend &&
        result.failure->kind == StartupFailureKind::contextVersion, "A driver below minimum GL version fails startup");
    check(result.failure->required == u"OpenGL 3.3 core" && result.failure->actual.starts_with(u"2.1"),
        "Version failure reports the driver version");
    std::cout << "Required " << utf8FromUtf16(result.failure->required) << ", actual "
              << utf8FromUtf16(result.failure->actual) << '\n';
}
void interactive(DesktopConfig config, std::string_view mode) {
    Ui ui;
    ScrollState scroll;
    ComponentConfig component;
    component.id = "action";
    component.label = u"Activate after first close";
    auto button = ui.addButton(component).value.value();
    int activations = 0;
    button.bind([&] { std::cout << "Activated " << ++activations << std::endl; return Result{}; });
    auto page = [&] { return layout::column({layout::component(button)}); };
    if (mode == "wrapper") {
        check(runDesktop(ui, materialAppearance(), page, scroll, config).status == Status::ok,
            "runDesktop returns success on native close");
        check(activations > 0, "runDesktop window receives input");
        return;
    }

    int requests = 0;
    const NativeWindow* reference = nullptr;
    config.onCloseRequested = [&](Desktop& host) {
        check(host.nativeWindow() == reference, "Close callback sees the same native window");
        std::cout << "Close request " << ++requests << std::endl;
        if (requests == 1) return CloseDecision::keepOpen;
        check(activations > 0, "Window receives input after close veto");
        if (mode == "program") {
            check(bool(host.close()), "Programmatic close bypasses veto");
            return CloseDecision::keepOpen;
        }
        return CloseDecision::close;
    };
    auto created = Desktop::create(ui, webAppearance(), page, scroll, config);
    checkStartup(created);
    auto& desktop = *created.desktop;
    reference = desktop.nativeWindow();
    check(reference && reference->display && (reference->backend == DesktopBackend::x11 ?
        reference->windowId != 0 && !reference->surface : reference->surface && !reference->windowId),
        "Tagged native reference identifies the display and window");
    if (config.backend != DesktopBackend::automatic)
        check(reference->backend == config.backend, "Native reference uses the selected backend");
    check(bool(desktop.setTitle(u"Isoduo native verification 中文")), "Title update accepted");
    std::cout << "Backend " << (reference->backend == DesktopBackend::wayland ? "wayland" : "x11") << '\n';
    printCapabilities(desktop.capabilities());
    check(desktop.capabilities().debugContext == (config.debugContext == DebugContext::on),
        "Context matches its requested debug mode");
    std::cout << "Window " << reference->windowId << std::endl;
    check(desktop.run().status == Status::ok && requests == 2 && !desktop.nativeWindow(), "Run returns and releases window");
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: isoduo_linux_desktop_native <wayland|x11|auto> <faults|close|program|wrapper|low-version>\n";
        return 2;
    }
    try {
        DesktopConfig config;
        const std::string_view backend = argv[1], mode = argv[2];
        if (backend == "wayland") config.backend = DesktopBackend::wayland;
        else if (backend == "x11") config.backend = DesktopBackend::x11;
        else check(backend == "auto", "Backend must be wayland, x11, or auto");
        config.width = 480;
        config.height = 320;
        config.title = u"Isoduo native verification";
        config.debugContext = mode == "program" ? DebugContext::on : DebugContext::off;
        if (mode == "faults") checkFaults(config);
        else if (mode == "low-version") lowVersion(config);
        else {
            check(mode == "close" || mode == "program" || mode == "wrapper", "Unknown verification mode");
            interactive(config, mode);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
