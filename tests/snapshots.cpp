#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "lcars/layout.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <tuple>
#include <type_traits>
using namespace lcars;
using test::check;

namespace {
static_assert(std::is_same_v<decltype(ComponentSnapshot::writingMode), std::optional<WritingMode>>);
static_assert(std::is_same_v<decltype(ComponentSnapshot::verticalDigits), std::optional<int>>);
static_assert(std::is_same_v<decltype(ComponentSnapshot::range), std::optional<SliderRange>>);
static_assert(std::is_same_v<decltype(ComponentSnapshot::scroll), std::optional<TextFieldScroll>>);

bool same_common_fields(const ComponentSnapshot& a, const NodeSnapshot& b) {
    return static_cast<const CommonState&>(a) == static_cast<const CommonState&>(b);
}

Query<ComponentSnapshot> describe(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Query<ComponentSnapshot> {
        if constexpr (requires { h.describe(); }) return h.describe();
        else return {Status::notFound, {}, {}};
    }, handle);
}
template <class T> Source<T> source(T value) {
    return {[value] { return value; }, [](std::function<void()>) { return [] {}; }};
}
struct Subject { std::string id; ComponentHandle handle; };
// One public, assembled inventory, including single/multi-line and rich text and attached labels.
// Drivers change a state once, then run the same checks over every applicable component kind.
struct World {
    Ui ui;
    test::FakeMeasure measure;
    ScrollState page_scroll;
    std::vector<Subject> subjects;
    TextFieldHandle multi;
    ButtonHandle button;
    template <class H> H keep(H h) { subjects.push_back({h.describe()->id, h}); return h; }
    explicit World(bool enabled = true, bool readOnly = false) {
        auto config = [&](std::string id) { return ComponentConfig{.id = std::move(id), .label = u"Fallback", .enabled = enabled}; };
        auto text_config = config("text"); text_config.readOnly = readOnly;
        auto text = keep(ui.addTextField(text_config).value.value());
        text.connectSource(source(std::u16string(u"Single")));
        text.connectConfirmed([](const auto&) { return Result{}; });
        auto multi_config = config("multi"); multi_config.multiline = true; multi_config.visibleLines = 2;
        multi_config.rich = true; multi_config.readOnly = readOnly;
        multi = keep(ui.addTextField(multi_config).value.value());
        multi.connectRichSource(source(RichText{u"a\nb\nc\nd\ne\nf", {{0, 1, true}}}));
        multi.connectRichConfirmed([](const auto&) { return Result{}; });
        button = keep(ui.addButton(config("button")).value.value());
        button.bind([] { return Result{}; });
        auto label_config = config("label"); label_config.value = u"Displayed"; label_config.verticalDigits = 2;
        label_config.writingMode = WritingMode::vertical; label_config.textAlign = TextAlign::right;
        label_config.justifyMethod = JustifyMethod::spaces; label_config.lineSpacing = 1.5; label_config.paragraphSpacing = 4;
        keep(ui.addLabel(label_config).value.value());
        auto box = keep(ui.addCheckbox(config("checkbox")).value.value());
        box.connectSource(source(true)); box.connectConfirmed([](const auto&) { return Result{}; });
        const std::vector<OptionItem> options = {{{"a"}, u"Alpha"}, {{"b"}, u"Disabled", false}};
        auto radio = keep(ui.addRadioGroup(config("radio"), options).value.value());
        radio.connectSource(source(std::optional{OptionValue{"a"}})); radio.connectConfirmed([](const auto&) { return Result{}; });
        auto slider_config = config("slider"); slider_config.readOnly = readOnly;
        auto slider = keep(ui.addSlider(slider_config, {-5, 15, 5}).value.value());
        slider.connectSource(source(5)); slider.connectConfirmed([](const auto&) { return Result{}; });
        auto dropdown = keep(ui.addDropdown(config("dropdown"), options).value.value());
        dropdown.connectSource(source(std::optional{OptionValue{"a"}})); dropdown.connectConfirmed([](const auto&) { return Result{}; });
        auto list_config = config("list"); list_config.readOnly = readOnly;
        auto list = keep(ui.addList(list_config, options).value.value());
        list.connectSource(source(std::optional{OptionValue{"a"}})); list.connectImmediate([](const auto&) { return Result{}; });
        const std::vector<TabPage> pages = {{{"a"}, u"Alpha", true, {}}, {{"b"}, u"Disabled", false, {}}};
        keep(ui.addTabs(config("tabs"), pages).value.value());
        auto progress = keep(ui.addProgress(config("progress")).value.value()); progress.connectSource(source(42));
        keep(ui.addNavigationList(config("navigation"), pages, {"a"}).value.value());
        for (auto role : {LabelKind::name, LabelKind::description, LabelKind::error}) {
            auto label = config("text.label." + std::to_string(static_cast<int>(role)));
            label.target = "text"; label.labelKind = role; label.value = u"Attached";
            keep(ui.addLabel(label).value.value());
        }
    }
    layout::Node root() const {
        std::vector<layout::Node> nodes;
        for (const auto& s : subjects) if (describe(s.handle)->target.empty()) nodes.push_back(layout::component(s.handle));
        return layout::scroll(layout::column(std::move(nodes)));
    }
    Frame arrange_page(int height = 2400) {
        return arrange(root(), ui, webAppearance(), measure, 400, height, 1.f, page_scroll);
    }
};
void compare_all(World& w, const char* state) {
    for (const auto& subject : w.subjects) {
        const auto context = std::string(state) + "/" + subject.id;
        const auto component = describe(subject.handle);
        const auto node = w.ui.semantics().get({subject.id});
        check(component && node && same_common_fields(*component, *node), (context + ": common fields agree").c_str());
        for (const auto& option : component->items) {
            const auto item = w.ui.semantics().get({subject.id, option.value});
            check(item && item->enabled == option.enabled && item->selected == option.selected && item->invalid == option.invalid,
                  (context + "/" + option.value.id + ": enabled, selected and invalid agree").c_str());
        }
    }
}

void normal() {
    World world; compare_all(world, "before_layout"); world.arrange_page(); compare_all(world, "normal");
    Ui ui;
    auto label = ui.addLabel({.id = "label", .label = u"Fallback", .value = u"Displayed"}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    arrange(layout::component(label), ui, webAppearance(), measure, 300, 200, 1.f, scroll);
    const auto component = label.describe();
    const auto node = ui.semantics().get({"label"});
    check(component && node && component->name == u"Displayed" && component->value == u"Displayed" &&
          component->name == node->name, "normal/label: both snapshots name a label by its displayed text");
    check(same_common_fields(*component, *node), "normal/label: all common fields agree");
    auto field = ui.addTextField({.id = "multi", .value = u"a\nb\nc\nd", .multiline = true, .visibleLines = 2}).value.value();
    arrange(layout::component(field), ui, webAppearance(), measure, 300, 200, 1.f, scroll);
    check(same_common_fields(*field.describe(), *ui.semantics().get({"multi"})), "normal/multi: all common fields agree");
    check(!field.describe()->writingMode && !field.describe()->verticalDigits &&
          field.describe()->scroll == std::optional{TextFieldScroll{0, 2, 4}}, "normal/multi: only labels have writing settings, scrolling is one value");
    auto name = ui.addLabel({.id = "label.name", .value = u"Other", .target = "label", .labelKind = LabelKind::name}).value.value();
    check(label.describe()->name == u"Displayed" && ui.semantics().get({"label"})->name == u"Displayed",
          "normal/label: an association does not replace the label's own displayed name");
}
void disabled() {
    World w(false); w.arrange_page(); compare_all(w, "disabled");
    for (const auto& subject : w.subjects) {
        const auto snapshot = describe(subject.handle);
        check(!snapshot->enabled && snapshot->verbs.empty(), ("disabled/" + subject.id + ": no component actions").c_str());
        for (const auto& option : snapshot->items)
            check(w.ui.semantics().get({subject.id, option.value})->verbs.empty(), "disabled: item keeps its setting but offers no action");
    }
}
void readOnly() {
    World w(true, true); w.arrange_page(); compare_all(w, "read_only");
    for (const auto& subject : w.subjects) {
        const auto snapshot = describe(subject.handle);
        // Applicability follows the typed handle/configuration capabilities, including read-only progress.
        const bool applicable = std::holds_alternative<TextFieldHandle>(subject.handle) ||
                                std::holds_alternative<SliderHandle>(subject.handle) ||
                                std::holds_alternative<ListHandle>(subject.handle) ||
                                std::holds_alternative<ProgressHandle>(subject.handle);
        if (applicable) check(snapshot->readOnly, ("read_only/" + subject.id + ": state reached").c_str());
    }
}
void contextMenuOpen() {
    World w; w.arrange_page();
    for (const auto& subject : w.subjects) {
        const auto menu = w.ui.addContextMenu(subject.handle, {{.id = "inspect", .label = u"Inspect", .action = [](const auto&) { return Result{}; }}});
        check(bool(menu) && bool(w.ui.openContextMenu(subject.handle)), ("context_menu/" + subject.id + ": opened").c_str());
        compare_all(w, "context_menu_open");
        for (const auto& other : w.subjects) check(!describe(other.handle)->focused, "context_menu: components do not own keyboard input");
        w.ui.closeContextMenu();
    }
}
void attached_error_visible() {
    Ui ui;
    auto box = ui.addCheckbox({.id = "box", .label = u"Check"}).value.value();
    box.connectSource(source(false));
    box.connectConfirmed([](const auto&) { return Result{Status::invalidValue, u"Error"}; });
    auto error = ui.addLabel({.id = "box.error", .target = "box", .labelKind = LabelKind::error}).value.value();
    check(bool(box.toggle()) && box.confirm().status == Status::invalidValue, "attached error: rejected confirmation creates an error");
    test::FakeMeasure measure; ScrollState scroll;
    arrange(layout::component(box), ui, webAppearance(), measure, 300, 200, 1.f, scroll);
    const auto node = ui.semantics().get({"box.error"});
    check(node && node->bounds && node->visible && error.describe()->visible && same_common_fields(*error.describe(), *node),
          "attached error: the visible error line has the same visibility in both snapshots");
}
void hidden() {
    World w; w.arrange_page();
    std::vector<ComponentHandle> members;
    for (const auto& s : w.subjects) members.push_back(s.handle);
    const auto gate = w.ui.addNavigationList({.id = "gate", .label = u"Gate"},
        {{{"shown"}, u"Shown", true, members}, {{"hidden"}, u"Hidden", true, {}}}, {"shown"}).value.value();
    check(bool(gate.select({"hidden"})), "hidden: select the other page");
    w.arrange_page(); compare_all(w, "hidden");
    for (const auto& s : w.subjects) {
        const auto node = w.ui.semantics().get({s.id});
        check(node->hidden && !node->visible && node->verbs.empty(), ("hidden/" + s.id + ": state reached").c_str());
    }
}
void blockedByModal() {
    World w;
    const auto inside = w.ui.addButton({.id = "dialog.close", .label = u"Close"}).value.value();
    const auto dialog = w.ui.addDialog("dialog", {inside}, inside, w.button, [](bool) {}).value.value();
    check(bool(dialog.open()), "modal: open dialog");
    arrange(layout::modal(w.root(), layout::component(inside), dialog), w.ui, webAppearance(), w.measure, 400, 2400, 1.f, w.page_scroll);
    compare_all(w, "blocked_by_modal");
    for (const auto& s : w.subjects) {
        const auto node = w.ui.semantics().get({s.id});
        check(node->blockedByModal && node->verbs.empty(), ("modal/" + s.id + ": state reached").c_str());
    }
}
void multiline_scrolled() {
    World w; w.arrange_page();
    for (const auto& subject : w.subjects) {
        if (const auto* field = std::get_if<TextFieldHandle>(&subject.handle); field && field->describe()->multiline)
            check(bool(field->setScrollLine(3)), "multiline_scrolled: set first display line");
    }
    w.arrange_page(); compare_all(w, "multiline_scrolled");
    check(w.multi.describe()->scroll == std::optional{TextFieldScroll{3, 2, 6}}, "multiline_scrolled: scrolled view reached");
}
void scrolled_offscreen() {
    World w;
    const auto filler = w.ui.addLabel({.id = "filler", .value = std::u16string(80, u'\n')}).value.value();
    for (const auto& subject : w.subjects) {
        ScrollState scroll;
        const auto root = layout::scroll(layout::column({layout::component(subject.handle), layout::component(filler)}));
        auto frame = arrange(root, w.ui, webAppearance(), w.measure, 400, 100, 1.f, scroll);
        check(frame.maximum > 0 && frame.scrollTo(w.ui, scroll, frame.maximum), "scrolled_offscreen: scroll past subject");
        arrange(root, w.ui, webAppearance(), w.measure, 400, 100, 1.f, scroll);
        compare_all(w, "scrolled_offscreen");
        check(!describe(subject.handle)->visible && !w.ui.semantics().get({subject.id})->bounds,
              ("scrolled_offscreen/" + subject.id + ": no visible area").c_str());
    }
}
void errors() {
    World w;
    for (const auto& subject : w.subjects) std::visit([](const auto& handle) {
        if constexpr (requires { handle.setError(u"Error"); }) check(bool(handle.setError(u"Error")), "errors: set field error");
    }, subject.handle);
    for (const auto& subject : w.subjects) {
        if (const auto* tabs = std::get_if<TabsHandle>(&subject.handle))
            check(bool(tabs->addMember({"a"}, w.subjects.front().handle)), "errors: place text on tab page");
        if (const auto* nav = std::get_if<NavigationListHandle>(&subject.handle))
            check(bool(nav->addMember({"a"}, w.multi)), "errors: place multi on navigation page");
    }
    w.arrange_page(); compare_all(w, "errors");
    check(w.ui.semantics().get({"tabs", OptionValue{"a"}})->invalid &&
          w.ui.semantics().get({"navigation", OptionValue{"a"}})->invalid, "errors: page items carry the member's error mark");
}
void dropdown_open() {
    World w; w.arrange_page();
    for (const auto& subject : w.subjects) if (const auto* dropdown = std::get_if<DropdownHandle>(&subject.handle)) {
        check(bool(dropdown->open()), "dropdown_open: open popup");
        w.arrange_page(); compare_all(w, "dropdown_open");
        check(dropdown->describe()->expanded, "dropdown_open: expanded state reached");
    }
}
void component_equality() {
    Ui ui;
    const auto list = ui.addList({.id = "list", .label = u"List"}, {{{"a"}, u"Alpha"}}).value.value();
    const auto original = *list.describe();
    auto changed = original;
    check(original == changed, "component equality: a copied snapshot is equal");
    changed.items.front().label = u"Changed";
    check(!(original == changed), "component equality: items are part of the whole snapshot");
}
void node_equality() {
    Ui ui;
    const auto label = ui.addLabel({.id = "label", .value = u"Displayed"}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    arrange(layout::component(label), ui, webAppearance(), measure, 300, 200, 1.f, scroll);
    const auto original = *ui.semantics().get({"label"});
    auto changed = original;
    check(original == changed, "node equality: a copied snapshot is equal");
    check(changed.bounds.has_value(), "node equality: the arranged label has bounds");
    ++changed.bounds->x;
    check(!(original == changed), "node equality: bounds are part of the whole snapshot");
}
}
int main() {
    const test::Case cases[] = {{"normal", normal}, {"disabled", disabled}, {"read_only", readOnly}, {"context_menu_open", contextMenuOpen},
                              {"attached_error_visible", attached_error_visible}, {"hidden", hidden}, {"blocked_by_modal", blockedByModal},
                              {"multiline_scrolled", multiline_scrolled}, {"scrolled_offscreen", scrolled_offscreen},
                              {"errors", errors}, {"dropdown_open", dropdown_open}, {"component_equality", component_equality},
                              {"node_equality", node_equality}};
    return test::run("snapshots", cases);
}
