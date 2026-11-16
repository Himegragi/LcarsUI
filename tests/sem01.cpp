#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "lcars/layout.hpp"
#include "fake_measure.hpp"
#include <stdexcept>
#include <algorithm>
#include <string>
using namespace lcars;
static void check(bool ok) { if (!ok) throw std::runtime_error("SEM01 failed"); }
int main() {
    {
        Ui probe;
        auto unnamed = probe.addButton({.id = "probe.unnamed"}).value.value();
        test::FakeMeasure measure; ScrollState scroll;
        arrange(layout::component(unnamed), probe, webAppearance(), measure, 300, 200, 1.f, scroll);
        arrange(layout::component(unnamed), probe, webAppearance(), measure, 300, 200, 1.f, scroll);
        check(probe.semantics().get({"probe.unnamed"})->name.empty());
        auto node = probe.semantics().get({"probe.unnamed"});
        check(std::find(node->verbs.begin(), node->verbs.end(), Verb::invoke) == node->verbs.end());
        check(probe.semantics().perform({"probe.unnamed"}, Verb::invoke).status == Status::notSupported);
        auto field = probe.addTextField({.id = "unconnected"}).value.value();
        check(bool(field.readOnly(true)));
        check(field.setValue(u"x").status == Status::readOnly);
        auto error = probe.addLabel({.id = "unconnected.error", .target = "unconnected", .labelKind = LabelKind::error}).value.value();
        auto described = probe.semantics().get({"unconnected"});
        check(described->describedBy.size() == 1 && described->describedBy[0].id == "unconnected.error");
#ifndef NDEBUG
        check(probe.diagnostics().size() == 1);
#endif
    }
    {
        Ui order;
        auto second = order.addButton({.id = "second", .label = u"Second"}).value.value();
        auto first = order.addButton({.id = "first", .label = u"First"}).value.value();
        test::FakeMeasure measure; ScrollState scroll;
        arrange(layout::column({layout::component(first), layout::component(second)}), order, webAppearance(), measure, 300, 0, 1.f, scroll);
        auto children = order.semantics().children({});
        check(children && children->size() == 2 && (*children)[0].id == "first" && (*children)[1].id == "second");
    }
    Ui ui;
    auto first = ui.addTextField({.id = "form.name"});
    check(bool(first));
    auto duplicate = ui.addButton({.id = "form.name"});
    check(duplicate.status == Status::invalidValue && !duplicate.value && bool(first.value->describe()));
    auto empty = ui.addList({.id = ""}, {});
    check(empty.status == Status::invalidValue && !empty.value);
#ifndef NDEBUG
    check(ui.diagnostics().size() == 2);
    check(ui.diagnostics()[0].find("form.name") != std::string::npos && ui.diagnostics()[0].find("text") != std::string::npos);
#endif
    auto source = ui.addButton({.id = "source"}).value.value();
    auto tip = ui.addTooltip("source.tip", source, u"Help");
    check(bool(tip));
    check(ui.addButton({.id = "source.tip"}).status == Status::invalidValue);
    check(ui.addTooltip("form.name", source, u"Help").status == Status::invalidValue);
    check(bool(source.remove()));
    check(bool(ui.addButton({.id = "source.tip"})));
    auto button = ui.addButton({.id = "form.title.pages.v03.temporary"}).value.value();
    Status pending = Status::ok;
    button.bind([&] {
        check(bool(button.remove()));
        pending = ui.addButton({.id = "form.title.pages.v03.temporary"}).status;
        return Result{};
    });
    check(bool(button.invoke()) && pending == Status::invalidValue);
    auto replacement = ui.addButton({.id = "form.title.pages.v03.temporary"});
    check(bool(replacement) && button.describe().status == Status::notFound);
}
