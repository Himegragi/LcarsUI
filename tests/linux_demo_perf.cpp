#include "desktop_linux_test.hpp"
#include "about_desktop.hpp"
#include "linux_window.hpp"
#include "perf.hpp"
#include "settings_demo.hpp"
#include "settings_store.hpp"
#include "utf.hpp"
#include <charconv>
#include <climits>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace isoduo;
using namespace detail;

namespace {
int parseNumber(std::string_view value, int min, int max) {
    int number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || number < min || number > max)
        throw std::invalid_argument("Invalid numeric argument");
    return number;
}

int seed(const std::filesystem::path& directory, int count, std::u16string font) {
    settingsDemo::Demo demo;
    auto document = demo.document();
    document.items.resize(std::min(document.items.size(), static_cast<std::size_t>(count)));
    while (document.items.size() < static_cast<std::size_t>(count)) {
        auto id = std::to_string(1001 + document.items.size());
        document.items.push_back({OptionValue{"T" + id}, u"Performance item", u"", {}});
    }
    document.nextItemId = 1001 + count;
    document.presentation.fontFamily = std::move(font);
    auto saved = settingsDemo::SettingsStore(directory).save(document);
    if (!saved.empty()) throw std::runtime_error(utf8FromUtf16(saved));
    return 0;
}

double monotonicMs(std::chrono::steady_clock::time_point time) {
    return std::chrono::duration<double, std::milli>(time.time_since_epoch()).count();
}

int run(char** argv) {
    const std::string_view backend = argv[1], appearance = argv[2], scenario = argv[3];
    if (backend != "wayland" && backend != "x11") throw std::invalid_argument("Expected wayland or x11");
    if (appearance != "web" && appearance != "material") throw std::invalid_argument("Expected web or material");
    const bool list = scenario == "list-hold-down", typing = scenario == "typing";
    const bool slider = scenario == "slider-drag-text-size", animation = scenario == "idle-after-animation";
    if (!list && !typing && !slider && !animation && scenario != "startup")
        throw std::invalid_argument("Unknown performance scenario");
    DesktopConfig config;
    config.backend = backend == "wayland" ? DesktopBackend::wayland : DesktopBackend::x11;
    config.width = parseNumber(argv[4], 1, 8192);
    config.height = parseNumber(argv[5], 1, 8192);
    config.title = u"Isoduo Demo Performance";
    const int delay = parseNumber(argv[6], 0, 1000);
    const settingsDemo::SettingsStore store(argv[7]);
    const auto loaded = store.load();
    const auto expectedItems = list ? 100u : 20u;
    if (!loaded.document || loaded.document->items.size() != expectedItems)
        throw std::runtime_error("Wrong settings fixture");
    const auto& theme = appearance == "web" ? webAppearance() : materialAppearance();
    auto about = settingsDemo::desktopAboutInfo(theme);
    about.settingsPath = store.path().u16string();
    settingsDemo::Demo demo(std::move(about));
    demo.loadDocument(*loaded.document);
    if (list && (!demo.nav.select({"items"}) || !demo.itemsList.focus()))
        throw std::runtime_error("Cannot focus demo list");
    if (typing && !demo.name.focus()) throw std::runtime_error("Cannot focus demo name");
    if (animation && !demo.volume.focus()) throw std::runtime_error("Cannot focus demo volume");
    if (slider && !demo.nav.select({"appearance"})) throw std::runtime_error("Cannot open appearance page");

    auto creation = Desktop::create(demo.ui, theme, [&] { return demo.page(); }, demo.scroll, config);
    if (!creation.desktop) throw std::runtime_error(creation.failure ? utf8FromUtf16(creation.failure->message) : "Cannot open desktop");
    auto& desktop = *creation.desktop;
    const auto caps = desktop.capabilities();
    demo.setRenderer(caps.version + u" — " + caps.renderer);
    std::cout << "context\t" << backend << '\t' << desktop.nativeWindow()->windowId << '\t'
              << utf8FromUtf16(caps.vendor) << '\t' << utf8FromUtf16(caps.renderer) << '\t'
              << utf8FromUtf16(caps.version) << '\t' << utf8FromUtf16(demo.ui.presentation().fontFamily) << '\n';

    LinuxDesktopTest::measureFrames(desktop, std::chrono::milliseconds(delay),
        [&](const LinuxFrameMeasurement& sample) {
            std::cout << "frame\t" << monotonicMs(sample.presented) << '\t'
                      << sample.geometry.width << '\t' << sample.geometry.height << '\t'
                      << sample.geometry.scale << '\t' << sample.layoutMs << '\t'
                      << sample.listMs << '\t' << sample.submissionMs << '\t' << sample.gpuMs
                      << '\t' << sample.delayMs << '\t' << sample.focused << '\n';
            if (list) {
                const auto candidate = demo.itemsList.describe().value->candidate;
                std::cout << "list\t" << (candidate ? candidate->id : "") << '\n';
            }
            if (typing) std::cout << "text\t" << utf8FromUtf16(*demo.name.draft().value) << '\n';
            if (animation)
                std::cout << "animation\t" << *demo.volume.draft().value << '\t' << sample.animating << '\n';
            if (slider) {
                const auto* placement = sample.frame.find(demo.textSize);
                if (placement) {
                    const auto& track = placement->sliderTrack;
                    std::cout << "slider\t" << *demo.textSize.draft().value << '\t' << track.x << '\t'
                              << track.y + track.h / 2 << '\t' << track.w << '\n';
                }
            }
            std::cout.flush();
            if (scenario == "startup") desktop.close();
        },
        [] { std::cout << "redraw\t" << monotonicMs(std::chrono::steady_clock::now()) << std::endl; });
    return desktop.run().status == Status::ok ? 0 : 1;
}

int summarize(char** argv) {
    const std::string_view scenario = argv[2];
    const bool delay = parseNumber(argv[3], 0, 1000) != 0;
    const double startupMs = std::stod(argv[4]), cpuMs = std::stod(argv[7]);
    const int presents = parseNumber(argv[5], 0, INT_MAX), redraws = parseNumber(argv[6], 0, INT_MAX);
    PerfSamples samples;
    PerfBreakdown stages;
    double age, layout, list, submit, gpu, injected;
    while (std::cin >> age) {
        if (!(std::cin >> layout >> list >> submit >> gpu >> injected))
            throw std::invalid_argument("Incomplete performance sample");
        samples.add(age, layout + list + submit + gpu + injected);
        stages.add(age, layout, list, submit + gpu);
    }
    if (!std::cin.eof()) throw std::invalid_argument("Invalid performance sample");
    const auto steady = samples.summary(false);
    const char* verdict = "FAIL";
    if (scenario == "startup") verdict = startupPass(startupMs) ? "PASS" : "FAIL";
    else if (scenario == "idle") verdict = idlePass(presents, redraws, cpuMs) ? "PASS" : "FAIL";
    else if (scenario == "slider-drag-text-size") verdict = steady.count ? "REPORT" : "FAIL";
    else if (scenario == "typing" || scenario == "list-hold-down")
        verdict = activePerfPass(steady, delay) ? "PASS" : "FAIL";
    else throw std::invalid_argument("Unknown report scenario");
    std::cout << "status\t" << verdict << '\n';
    std::cout << "phase\tcount\tp50_ms\tp95_ms\tp99_ms\tmax_ms\tover_16_7";
    for (auto stage : {"layout", "list", "submission_and_gpu"})
        for (auto percentile : {"p50", "p95", "p99", "max"})
            std::cout << '\t' << stage << '_' << percentile << "_ms";
    std::cout << '\n';
    for (bool warm : {true, false}) {
        const auto sample = samples.summary(warm);
        const auto phase = stages.summary(warm);
        std::cout << (warm ? "warmup" : "steady") << '\t' << sample.count << '\t' << sample.p50
                  << '\t' << sample.p95 << '\t' << sample.p99 << '\t' << sample.maximum << '\t' << sample.overBudget;
        for (auto* stage : {&phase.layout, &phase.list, &phase.gpu})
            std::cout << '\t' << stage->p50 << '\t' << stage->p95 << '\t' << stage->p99 << '\t' << stage->maximum;
        std::cout << '\n';
    }
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::fixed << std::setprecision(6);
        if (argc == 5 && std::string_view(argv[1]) == "seed")
            return seed(argv[2], parseNumber(argv[3], 20, 100), utf16FromUtf8(argv[4]));
        if (argc == 8 && std::string_view(argv[1]) == "summarize") return summarize(argv);
        if (argc == 8) return run(argv);
        std::cerr << "Usage: isoduo_linux_demo_perf seed DIRECTORY ITEM_COUNT FONT\n"
                     "       isoduo_linux_demo_perf BACKEND APPEARANCE SCENARIO WIDTH HEIGHT DELAY_MS DIRECTORY\n"
                     "       isoduo_linux_demo_perf summarize SCENARIO DELAY_MS STARTUP_MS PRESENTS REDRAWS CPU_MS\n"
                     "       stdin sample columns: age_ms layout_ms list_ms submission_ms gpu_ms injected_ms\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
