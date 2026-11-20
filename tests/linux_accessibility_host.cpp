#include "isoduo/desktop.hpp"
#include "isoduo/semantics.hpp"
#include "core_internal.hpp"
#include "desktop_linux_test.hpp"
#include "linux_accessibility.hpp"
#include "linux_window.hpp"
#include "renderer.hpp"
#include <glib.h>
#include <iostream>
#include <stdexcept>
#include <syncstream>
#include <thread>

using namespace isoduo;

namespace {
constexpr auto boundaryText = u"First words. Second sentence!";
constexpr auto unicodeBoundaryText = u"Caf\u00e9,\u00a0\U00020000! \nLast?";

// The accessibility bridge can answer text requests with committed text alone.
class HeadlessWindow final : public detail::LinuxWindow {
    NativeWindow reference_;
public:
    const NativeWindow& reference() const override { return reference_; }
    detail::EglWindow& renderWindow() override { throw std::logic_error("No headless renderer"); }
    detail::AsyncClipboard& clipboard() override { throw std::logic_error("No headless clipboard"); }
    void title(std::string_view) override {}
    bool poll(int) override { return false; }
};

int runTextFixture(bool headless) {
    Ui ui;
    auto text = ui.addTextField({.id = "fixture.text", .value = boundaryText, .multiline = true});
    auto unicode = ui.addTextField({.id = "fixture.unicode", .value = unicodeBoundaryText, .multiline = true});
    if (!text || !unicode) return 1;
    constexpr auto title = u"Isoduo Accessibility text-boundaries";
    if (!headless) {
        ScrollState scroll;
        DesktopConfig config;
        config.title = title;
        auto created = Desktop::create(ui, webAppearance(), [&] {
            return layout::column({layout::component(*text), layout::component(*unicode)});
        }, scroll, config);
        return created.desktop && created.desktop->run().status == Status::ok ? 0 : 2;
    }
    HeadlessWindow window;
    Frame frame;
    const bool closing = false;
    auto* context = g_main_context_default();
    detail::LinuxAccessibility accessibility(context, ui, window, frame, title, closing);
    auto loop = std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)>(g_main_loop_new(context, FALSE), g_main_loop_unref);
    g_main_loop_run(loop.get());
    return 0;
}

int runFixture(std::string mode) {
    const bool enabled = mode != "disabled";
    const bool injectFailure = mode == "renderer-failure";
    const std::string prefix = enabled ? "fixture." : "private.";
    const auto id = [&](std::string_view suffix) { return prefix + std::string(suffix); };
    Ui ui;
    ScrollState scroll;
    Desktop* host = nullptr;
    detail::RendererTestHooks hooks;
    auto button = ui.addButton({.id = id("invoke"), .label = u"Activate"}).value.value();
    auto text = ui.addTextField({.id = id("text"), .label = u"Name"}).value.value();
    auto label = ui.addLabel({.id = id("label"), .value = u"Display name",
                             .target = id("text"), .labelKind = LabelKind::name}).value.value();
    auto close = ui.addButton({.id = id("close"), .label = u"Close"}).value.value();
    auto remove = ui.addButton({.id = id("remove"), .label = u"Replace"}).value.value();
    auto replaced = ui.addButton({.id = id("replaced"), .label = u"Original"}).value.value();
    auto check = ui.addCheckbox({.id = id("check"), .label = u"Check"}).value.value();
    auto slider = ui.addSlider({.id = id("slider"), .label = u"Volume"}, {0, 10, 2}).value.value();
    auto progress = ui.addProgress({.id = id("progress"), .label = u"Progress"}).value.value();
    auto dropdown = ui.addDropdown({.id = id("dropdown"), .label = u"Option"},
        {{{"one"}, u"One"}, {{"two"}, u"Two"}, {{"disabled"}, u"Disabled", false}}).value.value();
    const auto subscribe = [](auto) { return [] {}; };
    std::u16string saved = injectFailure ? u"Retained draft" : u"A\U0001F642\u4e2d";
    std::u16string replacementValue = u"New";
    bool checked = false;
    int volume = 2;
    std::optional<OptionValue> choice{{"one"}};
    if (!text.connectSource({[&] { return saved; }, subscribe}) ||
        !text.connectImmediate([&](const auto& change) { saved = change.value; return Result{}; }) ||
        !check.connectSource({[&] { return checked; }, subscribe}) ||
        !check.connectImmediate([&](const auto& change) { checked = change.value; return Result{}; }) ||
        !slider.connectSource({[&] { return volume; }, subscribe}) ||
        !slider.connectImmediate([&](const auto& change) { volume = change.value; return Result{}; }) ||
        !progress.connectSource({[] { return 42; }, subscribe}) ||
        !dropdown.connectSource({[&] { return choice; }, subscribe}) ||
        !dropdown.connectImmediate([&](const auto& change) { choice = change.value; return Result{}; }))
        return 1;
    auto queuedChanges = [&]() -> Result {
        if (mode == "queued-replacement") {
            if (auto changed = text.setValue(u"Old edit"); !changed) return changed;
            if (auto removed = text.remove(); !removed) return removed;
            auto replacement = ui.addTextField({.id = id("text"), .label = u"Replacement", .value = replacementValue});
            if (!replacement) return {replacement.status, replacement.message};
            text = *replacement;
            if (auto connected = text.connectSource({[&] { return replacementValue; }, subscribe}); !connected)
                return connected;
            if (auto connected = text.connectImmediate([&](const auto& change) {
                replacementValue = change.value;
                return Result{};
            }); !connected) return connected;
            return text.setValue(u"New!");
        }
        if (mode == "queued-text") {
            const auto before = ui.semantics().text(id("text"));
            auto changed = text.setValue(u"A\U0001F642\u754c");
            return changed ? text.setValue(*before) : changed;
        }
        if (mode == "queued-menu") {
            for (int round = 0; round < 2; ++round) {
                if (auto opened = ui.openContextMenu(text); !opened) return opened;
                if (auto closed = detail::access(ui).keyDown({.key = Key::escape}); !closed) return closed;
            }
        }
        return {};
    };
    std::unique_ptr<GSource, decltype(&g_source_unref)> queuedSource(nullptr, g_source_unref);
    button.bind([&] {
        if (mode == "queued-text" || mode == "queued-menu" || mode == "queued-replacement") {
            if (queuedSource) return Result{Status::busy, {}};
            // Run separate UI operations in one GLib dispatch, outside the
            // button action's outer operation, before the host delivers events.
            queuedSource.reset(g_idle_source_new());
            g_source_set_callback(queuedSource.get(), [](gpointer data) -> gboolean {
                if (!(*static_cast<decltype(queuedChanges)*>(data))())
                    std::cerr << "Queued fixture operation failed\n";
                return G_SOURCE_REMOVE;
            }, &queuedChanges, nullptr);
            g_source_attach(queuedSource.get(), detail::LinuxPreferenceMonitor::context());
            return Result{};
        }
        if (injectFailure) {
            hooks.contextLostAtFrame = 1;
            auto settings = ui.presentation();
            settings.textSize = 18;
            return ui.setPresentation(settings);
        }
        const auto status = host->setTitle(u"\u7121\u969c\u7919");
        std::osyncstream(std::cout) << "Invoked " << bool(status) << std::endl;
        return status;
    });
    close.bind([&] {
        if (enabled) {
            std::osyncstream(std::cout) << "Model " << checked << ' ' << volume << ' '
                << (choice ? choice->id : "") << ' ' << (saved == u"Updated \u4e2d") << std::endl;
        }
        return host->close();
    });
    replaced.bind([] { return Result{}; });
    remove.bind([&] {
        auto result = replaced.remove();
        if (!result) return result;
        return Result{};
    });
    // Replacement follows the safe removal boundary in the next host dispatch.
    bool focusEvent = false, propertyEvent = false, structureEvent = false;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& event) {
        if (event.kind == SemanticEventKind::structureChanged && !replaced.describe()) {
            auto next = ui.addButton({.id = id("replaced"), .label = u"Replacement"});
            if (next) {
                replaced = *next;
                replaced.bind([] { return Result{}; });
            }
        }
        if (enabled) return;
        focusEvent |= event.kind == SemanticEventKind::focusChanged && event.ref.id == id("text");
        propertyEvent |= event.kind == SemanticEventKind::textChanged && event.ref.id == id("text");
        structureEvent |= event.kind == SemanticEventKind::structureChanged;
        if (focusEvent && propertyEvent && structureEvent) {
            std::osyncstream(std::cout) << "Disabled host dispatched local focus, text and structure" << std::endl;
            focusEvent = propertyEvent = structureEvent = false;
            button.focus();
        }
    });
    DesktopConfig config;
    config.title = u"Isoduo Accessibility ";
    config.title.append(mode.begin(), mode.end());
    if (mode != "default") config.platformAccessibilityEnabled = enabled;
    if (injectFailure) config.onRendererFailed = [](Desktop&, const RendererFailure& failure) {
        if (failure.kind == RendererFailureKind::deviceReset) std::cout << "Renderer failed: device reset" << std::endl;
    };
    config.onCloseRequested = [&](Desktop&) {
        std::osyncstream(std::cout) << "Native close " << mode << ' '
            << (saved.find(u'z') != std::u16string::npos) << std::endl;
        return CloseDecision::close;
    };
    auto created = detail::LinuxDesktopTest::create(ui, webAppearance(), [&] {
        return layout::column({layout::component(button), layout::component(label), layout::component(text),
            layout::component(check), layout::component(slider), layout::component(progress),
            layout::component(dropdown), layout::component(remove), layout::component(replaced),
            layout::component(close)});
    }, scroll, config, injectFailure ? &hooks : nullptr);
    if (!created.desktop) return 2;
    host = created.desktop.get();
    if (host->setTitle(u"Invalid\nTitle").status != Status::invalidValue) return 3;
    Status status{};
    std::thread worker([&] { status = host->setTitle(u"Wrong thread").status; });
    worker.join();
    if (status != Status::wrongThread) return 4;
    if (!enabled) {
        if (!text.setValue(u"Local") || !ui.semantics().perform({id("text")}, Verb::focus) ||
            !ui.addLabel({.id = id("dynamic"), .label = u"Dynamic"}) ||
            ui.semantics().text(id("text")).value != u"Local") return 5;
    }
    std::osyncstream(std::cout) << "Ready " << mode << std::endl;
    auto result = host->run();
    if (queuedSource) g_source_destroy(queuedSource.get());
    return result.status == Status::ok ? 0 : 6;
}
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "default";
    if (mode == "text-boundaries" || mode == "text-boundaries-headless")
        return runTextFixture(mode == "text-boundaries-headless");
    if (mode != "dual") return runFixture(mode);
    // Each host runs on its own UI thread, with both windows alive in one process.
    std::jthread disabled([] {
        if (const int status = runFixture("disabled")) std::exit(status);
    });
    return runFixture("enabled");
}
