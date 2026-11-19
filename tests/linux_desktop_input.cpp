#include "isoduo/desktop.hpp"
#include "isoduo/semantics.hpp"
#include "utf.hpp"
#include <iostream>

using namespace isoduo;

// Drive this window with native input. Ctrl+G samples application-visible state
// on the UI thread, and the window manager's close command ends the trial.
int main(int argc, char** argv) {
    const bool material = argc == 2 && std::string_view(argv[1]) == "material";
    const auto& appearance = material ? materialAppearance() : webAppearance();
    Ui ui;
    ScrollState scroll, innerScroll;
    if (argc == 2 && std::string_view(argv[1]) == "peer") {
        DesktopConfig config;
        config.title = u"Input focus peer";
        config.width = 320;
        config.height = 200;
        return runDesktop(ui, appearance, [] { return layout::column({}); }, scroll, config).status == Status::ok ? 0 : 1;
    }
    auto action = *ui.addButton({.id = "action", .label = u"Action"}).value;
    auto disabled = *ui.addButton({.id = "disabled", .label = u"Disabled"}).value;
    disabled.enable(false);
    auto field = *ui.addTextField({.id = "field", .label = u"Text"}).value;
    if (!field.connectSource({[] { return std::u16string{}; }, [](auto) { return [] {}; }}) ||
        !field.connectConfirmed([](const auto&) { return Result{}; })) return 1;
    auto report = *ui.addButton({.id = "report", .label = u"Report (Ctrl+G)"}).value;
    auto tip = *ui.addTooltip("tip", action, u"Action tooltip", 350).value;
    auto slider = *ui.addSlider({.id = "slider", .label = u"Level"}, {0, 10, 3}).value;
    std::vector<OptionItem> items;
    for (int i = 0; i < 25; ++i) {
        auto id = std::to_string(i);
        items.push_back({{id}, u"Option " + detail::utf16FromUtf8(id), i != 1});
    }
    auto list = *ui.addList({.id = "list", .label = u"List"}, items).value;
    auto dropdown = *ui.addDropdown({.id = "dropdown", .label = u"Dropdown"}, items).value;
    auto subscribe = [](auto) { return [] {}; };
    std::optional<OptionValue> selected;
    int saved = 0, writes = 0;
    if (!slider.connectSource({[&] { return saved; }, subscribe}) ||
        !list.connectSource({[&] { return selected; }, subscribe}) ||
        !list.connectImmediate([&](const auto& change) { selected = change.value; return Result{}; }) ||
        !dropdown.connectSource({[] { return std::optional<OptionValue>{{"0"}}; }, subscribe}) ||
        !dropdown.connectConfirmed([](const auto&) { return Result{}; })) return 1;
    auto notes = *ui.addTextField({.id = "notes", .label = u"Notes", .multiline = true, .visibleLines = 3}).value;
    if (!notes.connectSource({[] { return u"alpha beta\nsecond line\nthird line\nfourth line\nfifth line\nsixth line"; }, subscribe}) ||
        !notes.connectConfirmed([](const auto&) { return Result{}; })) return 1;
    auto tail = *ui.addLabel({.id = "tail", .value = u"Additional content below the nested viewport"}).value;
    int activations = 0;
    action.bind([&] { ++activations; return Result{}; });
    report.bind([&] {
        auto tree = ui.semantics();
        auto hover = *tree.hover().value;
        std::cout << "State " << activations << ' ' << tree.get({"action"}).value->focused
                  << ' ' << tree.get({"field"}).value->focused << ' ' << *tip.visible()
                  << ' ' << (hover ? hover->id : "-") << ' ' << tree.text("field").value->size()
                  << ' ' << ui.focusIndicatorVisible() << '\n';
        const auto snapshot = [&](std::string_view id, const NodeRef& ref) {
            auto node = tree.get(ref);
            if (node && node.value->bounds) {
                const auto& bounds = *node.value->bounds;
                std::cout << "Bounds " << id << ' ' << bounds.x << ' ' << bounds.y << ' ' << bounds.w << ' ' << bounds.h << '\n';
            }
        };
        for (const auto* id : {"action", "disabled", "field", "report", "slider", "list", "dropdown", "notes", "field/menu"})
            snapshot(id, {id});
        for (const auto* id : {"list", "dropdown"}) for (const auto& item : items)
            snapshot(std::string(id) + '/' + item.value.id, {id, item.value});
        const auto note = *tree.get({"notes"}).value;
        const auto menu = tree.get({"field/menu"});
        auto draft = *dropdown.draft().value;
        std::cout << "Components " << *slider.draft().value << ' ' << saved << ' ' << writes
                  << ' ' << (selected ? selected->id : "-") << ' ' << (draft ? draft->id : "-")
                  << ' ' << dropdown.describe().value->expanded << ' ' << innerScroll.offset << ' ' << scroll.offset
                  << ' ' << note.textSelection->anchor << ' ' << note.textSelection->caret << ' ' << note.scroll->firstLine
                  << ' ' << (menu && menu.value->visible) << ' ' << *ui.animating().value << '\n';
        std::cout << "Ready" << std::endl;
        return Result{};
    });
    ui.addShortcut({.letter = u'G', .ctrl = true}, report);
    auto apply = *ui.addButton({.id = "apply", .label = u"Apply Level"}).value;
    if (!ui.connectConfirmation(apply, {slider}, [&](const auto& changes) {
        saved = std::get<Change<SliderHandle>>(changes.front()).value;
        ++writes;
        return ConfirmationResult{};
    }) || !ui.addShortcut({.letter = u'S', .ctrl = true}, apply)) return 1;
    auto events = ui.semantics().subscribe([&](const SemanticEvent& event) {
        if (event.kind == SemanticEventKind::menuOpened) std::cout << "Menu opened " << event.ref.id << std::endl;
        if (event.kind == SemanticEventKind::menuClosed) std::cout << "Menu closed " << event.ref.id << std::endl;
        if (event.ref.id == "slider" && event.kind == SemanticEventKind::propertyChanged && event.property == "numericValue")
            std::cout << "Slider transition " << *slider.draft().value << ' ' << *ui.animating().value << std::endl;
    });
    auto page = [&] {
        using namespace layout;
        return layout::scroll(column({component(action), component(disabled), component(field),
            row({component(report), component(apply)}), component(slider), component(dropdown),
            layout::scroll(component(list), innerScroll, 135), component(notes), component(tail)}, 12, 20));
    };
    DesktopConfig config;
    config.title = u"Desktop input fixture";
    config.width = 640;
    config.height = 650;
    auto result = Desktop::create(ui, appearance, page, scroll, config);
    if (!result.desktop) {
        if (result.failure) std::cerr << detail::utf8FromUtf16(result.failure->message) << '\n';
        return 1;
    }
    const auto& caps = result.desktop->capabilities();
    std::cout << "Backend " << static_cast<int>(result.desktop->nativeWindow()->backend)
              << ' ' << detail::utf8FromUtf16(caps.version) << ' ' << detail::utf8FromUtf16(caps.renderer) << std::endl;
    return result.desktop->run().status == Status::ok ? 0 : 1;
}
