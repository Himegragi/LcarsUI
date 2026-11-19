#include "check.hpp"
#include "core_internal.hpp"
#include "desktop_linux_test.hpp"
#include "draw_list_fixture.hpp"
#include "edit_form.hpp"
#include "list_page.hpp"
#include "presentation_matrix.hpp"
#include "render_role_checks.hpp"
#include "settings_demo.hpp"
#include "utf.hpp"

#include <array>
#include <fstream>
#include <optional>
#include <thread>

using namespace isoduo;
using namespace isoduo::detail;
using test::check;

namespace {
constexpr int width = 960, height = 720;

void reportContext(const Desktop& desktop, std::ostream& out) {
    const auto& caps = desktop.capabilities();
    const auto* window = desktop.nativeWindow();
    check(window && (window->backend == DesktopBackend::wayland ? window->surface != nullptr : window->windowId != 0),
          "The draw context belongs to a native desktop window");
    out << "# backend=" << (window->backend == DesktopBackend::wayland ? "wayland" : "x11")
        << " window=" << window->windowId << " renderer=" << utf8FromUtf16(caps.renderer)
        << " vendor=" << utf8FromUtf16(caps.vendor) << " version=" << utf8FromUtf16(caps.version)
        << " debugContext=" << caps.debugContext << " debugMessages=" << caps.debugMessages
        << " resetDetection=" << caps.resetDetection << std::endl;
}

std::unique_ptr<Desktop> openDesktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page,
                                     ScrollState& scroll, DesktopConfig config, RendererTestHooks* hooks = nullptr) {
    auto result = LinuxDesktopTest::create(ui, appearance, std::move(page), scroll, std::move(config), hooks);
    if (result.failure) std::cerr << utf8FromUtf16(result.failure->message) << '\n';
    check(result.status == Status::ok && result.desktop, "Open the native desktop window");
    return std::move(result.desktop);
}

// PAM preserves all four readback channels, including the transparent draw-list fixture.
void writeImage(const std::filesystem::path& path, const Image& image) {
    std::ofstream out(path, std::ios::binary);
    out << "P7\nWIDTH " << image.width << "\nHEIGHT " << image.height
        << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
    out.write(reinterpret_cast<const char*>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
    check(bool(out), "Write comparison image");
}

class Report {
public:
    explicit Report(std::filesystem::path folder) : folder_(std::move(folder)) {
        std::filesystem::create_directories(folder_);
        out_.open(folder_ / "comparison.tsv");
        check(bool(out_), "Open comparison report");
        out_ << "scene\tsize\tscale\tdifferent_pixels\tdifferent_fraction\tmaximum_difference"
                "\trole_mask\tfailed_role_mask\tsetup\trequired_regions\tstatus\tregions\n";
    }

    void row(std::string_view name, float scale, const GpuComparison& result, bool setup, bool cues) {
        const bool pass = setup && result && cues;
        failures_ += !pass;
        const auto& reference = result.reference;
        out_ << name << '\t' << reference.width << 'x' << reference.height << '\t' << scale << '\t'
             << result.comparison.differentPixels << '\t'
             << double(result.comparison.differentPixels) / (reference.width * reference.height) << '\t'
             << result.comparison.maximumDifference << '\t' << result.roles << '\t' << result.failedRoles << '\t'
             << setup << '\t' << cues << '\t' << (pass ? "PASS" : "FAIL") << '\t';
        for (const auto& region : result.regions) {
            const auto& box = region.bounds;
            out_ << region.owner << ':' << region.role << ':' << region.popup << ':'
                 << box.x << ',' << box.y << ',' << box.w << ',' << box.h << ':' << region.passed << ' ';
        }
        out_ << std::endl;
        check(bool(out_), "Write comparison report");
        const auto stem = folder_ / name;
        writeImage(stem.string() + "-cpu.pam", reference);
        if (!pass || name == "layered-draw-list") writeImage(stem.string() + "-gl.pam", result.actual);
        if (!pass) {
            auto diff = reference;
            for (std::size_t i = 0; i < diff.pixels.size(); i += 4) {
                int delta = 0;
                for (int c = 0; c < 4; ++c)
                    delta = std::max(delta, std::abs(int(reference.pixels[i+c]) - int(result.actual.pixels[i+c])));
                diff.pixels[i] = static_cast<unsigned char>(delta);
                diff.pixels[i+1] = diff.pixels[i+2] = 0;
                diff.pixels[i+3] = 255;
            }
            writeImage(stem.string() + "-diff.pam", diff);
        }
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
    }

    template<class Compare>
    std::optional<GpuComparison> compare(std::string_view name, int w, int h, float scale, bool setup, Compare&& compareFrame) {
        try {
            return compareFrame();
        } catch (const FrameError& error) {
            ++failures_;
            auto message = utf8FromUtf16(error.failure.message);
            for (auto& ch : message) if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
            out_ << name << '\t' << w << 'x' << h << '\t' << scale << "\t-\t-\t-\t-\t-\t" << setup
                 << "\t-\tFAIL\treadback_error=" << error.failure.nativeCode << ' ' << message << std::endl;
            check(bool(out_), "Write comparison failure");
            std::cerr << "FAIL " << name << ": " << message << '\n';
            return std::nullopt;
        }
    }

    int failures() const { return failures_; }
private:
    std::filesystem::path folder_;
    std::ofstream out_;
    int failures_ = 0;
};

void key(UiState& state, Key value) {
    state.keyDown({value});
    state.keyUp(value);
}

void drawList(DesktopConfig config, Report& report) {
    Ui ui;
    ScrollState scroll;
    auto desktop = openDesktop(ui, webAppearance(), [] { return layout::column({}); }, scroll, config);
    reportContext(*desktop, std::cout);
    const auto result = report.compare("layered-draw-list", 8, 6, 1.f, true, [&] {
        return LinuxDesktopTest::compareDrawList(*desktop, test::layeredDrawList(), 8, 6);
    });
    if (result) report.row("layered-draw-list", 1.f, *result, test::matchesLayeredDrawList(result->reference), true);
}

void showcase(DesktopConfig config, const Appearance& appearance, std::string_view appearanceName, Report& report) {
    constexpr std::array names{"v01-a", "v01-b", "v01-c", "v01-d", "v01-e",
                               "v02-a", "v02-b", "v02-c", "v03-a", "v03-b"};
    for (std::size_t scene = 0; scene < names.size(); ++scene) for (float scale : {1.f, 1.5f}) {
        config.width = scale == 1.f ? 960 : 640;
        config.height = scale == 1.f ? 720 : 480;
        editForm::Form form(editForm::defaultIds(), true);
        auto desktop = openDesktop(form.ui, appearance, [&] { return form.page(); }, form.scroll, config);
        if (scene == 0 && scale == 1.f) reportContext(*desktop, std::cout);
        auto& handles = form.handles;
        const auto& ids = form.ids();
        auto& state = access(form.ui);
        auto& list = form.list->handles;
        Frame frame;
        bool setup = true;
        std::string reveal;
        std::uint64_t now = 1000;
        switch (scene) {
        case 0:
            setup = bool(handles.name.focus());
            break;
        case 1:
            setup = handles.name.focus() && state.select(ids.name, 0, 2);
            break;
        case 2:
            setup = handles.name.focus() && state.compositionUpdate(u"\u3118\u311c\u02cb", 3);
            break;
        case 3:
            setup = handles.name.focus() && handles.name.setValue(u"");
            handles.apply.invoke();
            setup &= handles.name.describe()->invalid;
            break;
        case 4:
            setup = bool(handles.region.focus());
            reveal = ids.region;
            LinuxDesktopTest::layoutFrame(*desktop, width, height, scale, reveal);
            setup &= bool(handles.region.open());
            key(state, Key::down);
            key(state, Key::down);
            setup &= handles.region.describe()->candidate == OptionValue{"south"};
            break;
        case 5:
            state.setSystemKeyboardCues(true);
            setup = handles.tabs.activate({"v02"}) && list.twenty.invoke() && list.list.select({"T003"}) && list.list.focus();
            break;
        case 6:
            state.setSystemKeyboardCues(true);
            setup = handles.tabs.activate({"v02"}) && list.hundred.invoke() && list.list.focus();
            key(state, Key::end);
            setup &= form.list->selection() == OptionValue{"T100"} && list.list.describe()->candidate == OptionValue{"T100"};
            reveal = list.list.describe()->id;
            break;
        case 7:
            setup = handles.tabs.activate({"v02"}) && list.zero.invoke();
            break;
        case 8:
            // Establish the focused window state used by the offscreen comparison.
            state.windowActive(true);
            state.tooltipClock([&] { return now; });
            setup = handles.tabs.activate({"v03"}) && handles.progressHalf.focus();
            key(state, Key::right);
            LinuxDesktopTest::layoutFrame(*desktop, width, height, scale, state.focused());
            state.tooltipTime(now += 600);
            setup &= *handles.progressTip.visible();
            state.tooltipClock({});
            break;
        case 9:
            state.setSystemKeyboardCues(true);
            setup = handles.tabs.activate({"v03"}) && handles.dialogOpen.invoke() && *handles.dialog.isOpen();
            break;
        }
        const auto name = std::string(names[scene]) + "-" + std::string(appearanceName) +
            (scale == 1.f ? "-100" : "-150");
        const auto result = report.compare(name, width, height, scale, setup, [&] {
            return LinuxDesktopTest::compareFrame(*desktop, width, height, scale, frame, 0, reveal);
        });
        if (!result) continue;
        auto has = [&](std::string_view owner, PaintRole role, bool popup = false) {
            return test::hasPaintRole(*result, owner, role, popup);
        };
        bool cues = true;
        switch (scene) {
        case 0: cues = has(ids.name, PaintRole::focusIndicator) && has(ids.name, PaintRole::caret); break;
        case 1: cues = has(ids.name, PaintRole::selection); break;
        case 2: cues = has(ids.name, PaintRole::composition) && has(ids.name, PaintRole::caret); break;
        case 3:
            cues = has(ids.name, PaintRole::errorIndicator) &&
                (has(ids.name, PaintRole::errorText) || has(ids.nameError, PaintRole::errorText));
            break;
        case 4: cues = has(ids.region, PaintRole::focusIndicator) && has(ids.region, PaintRole::selection, true); break;
        case 6: {
            const auto* placement = frame.find(list.list);
            setup &= placement && !placement->options.empty() &&
                intersect(placement->options.back(), placement->clip) == placement->options.back();
            [[fallthrough]];
        }
        case 5: {
            const auto id = list.list.describe()->id;
            cues = has(id, PaintRole::focusIndicator) && has(id, PaintRole::selection);
            break;
        }
        case 8:
            cues = std::any_of(frame.overlays.begin(), frame.overlays.end(), [&](const auto& placement) {
                return placement.tooltip && placement.id == handles.progressHalf.describe()->id;
            });
            break;
        case 9: cues = has(handles.dialogCancel.describe()->id, PaintRole::focusIndicator); break;
        }
        report.row(name, scale, *result, setup, cues);
        if (scene == 0 && scale == 1.f && setup && *result && cues) {
            const auto shifted = report.compare(name + "-shifted-caret", width, height, scale, true, [&] {
                return LinuxDesktopTest::compareFrame(*desktop, width, height, scale, frame, 1);
            });
            const auto missing = report.compare(name + "-missing-focus", width, height, scale, true, [&] {
                return LinuxDesktopTest::compareFrame(*desktop, width, height, scale, frame, 2);
            });
            if (!shifted || !missing) continue;
            check(!*shifted && (shifted->failedRoles & static_cast<unsigned>(PaintRole::caret)),
                  "Move the caret by one pixel and fail its region");
            check(!*missing && (missing->failedRoles & static_cast<unsigned>(PaintRole::focusIndicator)),
                  "Omit the focus indicator and fail its region");
            std::cout << "PASS role fault controls " << appearanceName << '\n';
        }
    }
}

void demoScenes(DesktopConfig config, const Appearance& appearance, std::string_view appearanceName, Report& report) {
    for (bool dark : {false, true}) for (float scale : {1.f, 1.5f}) for (int scene = 0; scene < 9; ++scene) {
        config.width = scale == 1.f ? 960 : 640;
        config.height = scale == 1.f ? 720 : 480;
        settingsDemo::AboutInfo info;
        info.licenses.push_back({{"isoduo"}, u"Isoduo", u"License text"});
        for (int i = 0; i < 40; ++i) {
            auto name = u"Font " + utf16FromUtf8(std::to_string(i));
            info.fonts.push_back({name, name});
        }
        settingsDemo::Demo demo(std::move(info));
        auto desktop = openDesktop(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, config);
        if (scene == 0 && !dark && scale == 1.f) reportContext(*desktop, std::cout);
        auto& state = access(demo.ui);
        auto settings = demo.ui.presentation();
        settings.variant = dark ? ThemeVariant::dark() : ThemeVariant::light();
        bool setup = bool(demo.ui.setPresentation(settings));
        if (scene == 2 || scene == 7) state.setSystemKeyboardCues(true);
        Frame frame;
        switch (scene) {
        case 0: setup &= bool(demo.name.focus()); break;
        case 1:
            setup &= demo.name.focus() && demo.name.setValue(u"");
            key(state, Key::tab);
            setup &= demo.name.describe()->invalid;
            break;
        case 2:
        case 3:
            setup &= demo.nav.select({"items"}) && demo.itemsList.select({"T003"}) && demo.itemsList.focus();
            if (scene == 3) setup &= demo.itemsEdit.invoke() && demo.itemDialog.isOpen().value.value_or(false);
            break;
        case 4: {
            setup &= demo.nav.select({"appearance"}) && demo.font.focus() && demo.font.open();
            for (int i = 0; i < 30; ++i) key(state, Key::down);
            const auto font = demo.font.describe();
            setup &= font && font->expanded && font->candidate && font->candidate->id != "default";
            break;
        }
        case 5: setup &= demo.nav.select({"about"}) && demo.licenses.select({"isoduo"}); break;
        case 6:
            setup &= bool(demo.notices.toggle());
            setup &= !demo.requestClose() && demo.closeDialog.isOpen().value.value_or(false);
            break;
        case 7:
            setup &= demo.nav.select({"about"}) && demo.nav.focus();
            frame = LinuxDesktopTest::layoutFrame(*desktop, width, height, scale);
            if (const auto* nav = frame.find(demo.nav))
                demo.scroll.offset += nav->bounds.y + nav->bounds.h / 2 - height / 2;
            break;
        case 8: setup &= bool(demo.nav.select({"about"})); break;
        }
        const auto name = std::string("SD-") + char('a' + scene) + "-" + std::string(appearanceName) +
            (dark ? "-dark" : "-light") + (scale == 1.f ? "-100" : "-150");
        const auto result = report.compare(name, width, height, scale, setup, [&] {
            return LinuxDesktopTest::compareFrame(*desktop, width, height, scale, frame);
        });
        if (!result) continue;
        auto has = [&](std::string_view owner, PaintRole role) { return test::hasPaintRole(*result, owner, role); };
        bool cues = true;
        switch (scene) {
        case 0: cues = has("general.name", PaintRole::focusIndicator) && has("general.name", PaintRole::caret); break;
        case 1:
            cues = has("general.name", PaintRole::errorIndicator) &&
                (has("general.name", PaintRole::errorText) || has("general.name.error", PaintRole::errorText));
            break;
        case 2: cues = has("items.list", PaintRole::focusIndicator) && has("items.list", PaintRole::selection); break;
        case 7: cues = has("app.nav", PaintRole::focusIndicator); break;
        case 8: cues = test::matchesVerticalLabel(*result, frame.find(demo.aboutVertical())); break;
        }
        report.row(name, scale, *result, setup, cues);
    }
}

bool verifyPresentationMatrix(DesktopConfig config, const Appearance& appearance, std::string_view appearanceName,
                              const std::filesystem::path& folder) {
    try {
        unsigned comparisons = 0;
        presentationMatrix::run(editForm::defaultIds(), appearance, [&](editForm::Form& form) {
            std::shared_ptr<Desktop> desktop = openDesktop(form.ui, appearance, [&form] { return form.page(); },
                                                          form.scroll, config);
            if (comparisons == 0) reportContext(*desktop, std::cout);
            return presentationMatrix::Driver{
                [desktop](int width, int height, float scale, std::string_view reveal) {
                    return LinuxDesktopTest::layoutFrame(*desktop, width, height, scale, reveal);
                },
                [desktop, &folder, appearanceName, &comparisons](int width, int height, float scale) {
                    Frame frame;
                    auto result = LinuxDesktopTest::compareFrame(*desktop, width, height, scale, frame);
                    ++comparisons;
                    if (!result) {
                        const auto stem = (folder / (std::string(appearanceName) + "-" + std::to_string(comparisons))).string();
                        writeImage(stem + "-cpu.pam", result.reference);
                        writeImage(stem + "-gl.pam", result.actual);
                        std::cerr << stem << " " << width << 'x' << height << " scale=" << scale
                                  << " different_pixels=" << result.comparison.differentPixels
                                  << " failed_role_mask=" << result.failedRoles << '\n';
                    }
                    return bool(result);
                }};
        }, [](bool ok, const std::string& message) { check(ok, message.c_str()); });
        std::cout << "PASS " << appearanceName << " presentation matrix, " << comparisons
                  << " offscreen comparisons in a native EGL context\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << appearanceName << " presentation matrix: " << error.what() << '\n';
        return false;
    }
}

void diagnostics(DesktopConfig config, bool receiver) {
    Ui ui;
    ScrollState scroll;
    ComponentConfig field;
    field.id = "diagnostic-fixture";
    field.label = u"Native OpenGL diagnostic";
    auto label = ui.addLabel(field).value.value();
    RendererTestHooks hooks;
    hooks.misuseOnce = true; // glEnable with an invalid enum; captured through GL_KHR_debug
    if (!receiver) hooks.errorAtFrame = 1; // Terminate the no-receiver run deterministically.
    config.debugContext = DebugContext::on;
    const auto owner = std::this_thread::get_id();
    bool correctThread = true;
    Desktop* running = nullptr;
    std::vector<RenderDiagnostic> errors;
    if (receiver) config.onDiagnostic = [&](const RenderDiagnostic& event) {
        correctThread &= std::this_thread::get_id() == owner;
        if (event.kind == DiagnosticKind::apiMisuse) {
            errors.push_back(event);
            running->close();
        }
    };
    const auto start = std::chrono::system_clock::now();
    auto desktop = openDesktop(ui, webAppearance(), [&] { return layout::column({layout::component(label)}); },
                               scroll, config, &hooks);
    running = desktop.get();
    reportContext(*desktop, std::cout);
    check(desktop->capabilities().debugContext && desktop->capabilities().debugMessages, "Enable native OpenGL debug messages");
    auto result = desktop->run();
    if (!receiver) {
        check(result.status == Status::rendererFailed && result.failure &&
              result.failure->kind == RendererFailureKind::frameError, "Fixture terminates after injected frame error");
        return;
    }
    check(result.status == Status::ok, "Desktop presents and closes from the diagnostic callback");
    check(correctThread && errors.size() == 1, "Receive exactly one OpenGL misuse on the UI thread");
    const auto& error = errors.front();
    check(error.backend == u"OpenGL" && error.severity == DiagnosticSeverity::error && error.nativeCode != 0 &&
          !error.nativeMessage.empty() && error.uiEnvironment == &ui, "Receive the OpenGL diagnostic payload");
    check(error.receivedAt >= start && error.receivedAt <= std::chrono::system_clock::now() &&
          !error.sourceTime, "Keep receipt time and an absent source time");
    std::cout << "PASS GL misuse, UI thread, backend=" << utf8FromUtf16(error.backend)
              << " severity=error category=apiMisuse nativeCode=" << error.nativeCode
              << " receivedAt=" << error.receivedAt.time_since_epoch().count()
              << " sourceTime=absent message=" << error.nativeMessage << '\n';
}
}

int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Usage: isoduo_linux_render_verify <wayland|x11> <draw-list|compare|presentation-matrix|diagnostics|no-receiver> [evidence]");
        std::string_view backend = argv[1], mode = argv[2];
        check(backend == "wayland" || backend == "x11", "Specify a native desktop backend");
        DesktopConfig config;
        config.backend = backend == "wayland" ? DesktopBackend::wayland : DesktopBackend::x11;
        config.title = u"Isoduo render verification";
        config.width = width;
        config.height = height;
        if (mode == "diagnostics" || mode == "no-receiver") {
            diagnostics(config, mode == "diagnostics");
            return 0;
        }
        if (mode == "presentation-matrix") {
            check(argc == 4, "Specify the presentation matrix evidence folder");
            const std::filesystem::path folder(argv[3]);
            std::filesystem::create_directories(folder);
            const bool web = verifyPresentationMatrix(config, webAppearance(), "web", folder);
            const bool material = verifyPresentationMatrix(config, materialAppearance(), "material", folder);
            return web && material ? 0 : 1;
        }
        check(argc == 4 && (mode == "draw-list" || mode == "compare"), "Specify comparison mode and evidence folder");
        Report report(argv[3]);
        config.debugContext = DebugContext::off;
        if (mode == "draw-list") drawList(config, report);
        else for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
            const auto name = appearance == &webAppearance() ? "web" : "material";
            showcase(config, *appearance, name, report);
            demoScenes(config, *appearance, name, report);
        }
        return report.failures() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
