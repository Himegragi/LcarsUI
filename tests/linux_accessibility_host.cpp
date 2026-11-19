#include "isoduo/desktop.hpp"
#include "isoduo/semantics.hpp"
#include <iostream>
#include <syncstream>
#include <thread>

using namespace isoduo;

namespace {
int runFixture(std::string mode) {
    const bool enabled = mode != "disabled";
    const std::string prefix = enabled ? "fixture." : "private.";
    const auto id = [&](std::string_view suffix) { return prefix + std::string(suffix); };
    Ui ui;
    ScrollState scroll;
    Desktop* host = nullptr;
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
    std::u16string saved = u"A\U0001F642\u4e2d";
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
    button.bind([&] {
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
    config.onCloseRequested = [&](Desktop&) {
        std::osyncstream(std::cout) << "Native close " << mode << ' '
            << (saved.find(u'z') != std::u16string::npos) << std::endl;
        return CloseDecision::close;
    };
    auto created = Desktop::create(ui, webAppearance(), [&] {
        return layout::column({layout::component(button), layout::component(label), layout::component(text),
            layout::component(check), layout::component(slider), layout::component(progress),
            layout::component(dropdown), layout::component(remove), layout::component(replaced),
            layout::component(close)});
    }, scroll, config);
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
    return host->run().status == Status::ok ? 0 : 6;
}
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "default";
    if (mode != "dual") return runFixture(mode);
    // Each host runs on its own UI thread, with both windows alive in one process.
    std::jthread disabled([] {
        if (const int status = runFixture("disabled")) std::exit(status);
    });
    return runFixture("enabled");
}
