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
        auto unnamed = probe.add_button({.id = "probe.unnamed", .kind = Kind::button}).value.value();
        test::FakeMeasure measure; ScrollState scroll;
        arrange(layout::control(unnamed), probe, web_appearance(), measure, 300, 200, 1.f, scroll);
        arrange(layout::control(unnamed), probe, web_appearance(), measure, 300, 200, 1.f, scroll);
        check(probe.semantics().get({"probe.unnamed"})->name.empty());
        auto node = probe.semantics().get({"probe.unnamed"});
        check(std::find(node->verbs.begin(), node->verbs.end(), Verb::invoke) == node->verbs.end());
        check(probe.semantics().perform({"probe.unnamed"}, Verb::invoke).status == Status::not_supported);
        auto field = probe.add_text({.id = "unconnected", .kind = Kind::text}).value.value();
        check(bool(field.read_only(true)));
        check(field.set_value(u"x").status == Status::read_only);
#ifndef NDEBUG
        check(probe.diagnostics().size() == 1);
#endif
    }
    {
        Ui order;
        auto second = order.add_button({.id = "second", .kind = Kind::button, .label = u"Second"}).value.value();
        auto first = order.add_button({.id = "first", .kind = Kind::button, .label = u"First"}).value.value();
        test::FakeMeasure measure; ScrollState scroll;
        arrange(layout::column({layout::control(first), layout::control(second)}), order, web_appearance(), measure, 300, 0, 1.f, scroll);
        auto children = order.semantics().children({});
        check(children && children->size() == 2 && (*children)[0].id == "first" && (*children)[1].id == "second");
    }
    Ui ui;
    auto first = ui.add_text({.id = "form.name", .kind = Kind::text});
    check(bool(first));
    auto duplicate = ui.add_button({.id = "form.name", .kind = Kind::button});
    check(duplicate.status == Status::invalid_value && !duplicate.value && bool(first.value->describe()));
    auto empty = ui.add_list({.id = "", .kind = Kind::list}, {});
    check(empty.status == Status::invalid_value && !empty.value);
#ifndef NDEBUG
    check(ui.diagnostics().size() == 2);
    check(ui.diagnostics()[0].find("form.name") != std::string::npos && ui.diagnostics()[0].find("text") != std::string::npos);
#endif
    auto source = ui.add_button({.id = "source", .kind = Kind::button}).value.value();
    auto tip = ui.add_tooltip("source.tip", source, u"Help");
    check(bool(tip));
    check(ui.add_button({.id = "source.tip", .kind = Kind::button}).status == Status::invalid_value);
    check(ui.add_tooltip("form.name", source, u"Help").status == Status::invalid_value);
    check(bool(source.remove()));
    check(bool(ui.add_button({.id = "source.tip", .kind = Kind::button})));
    auto button = ui.add_button({.id = "form.title.pages.v03.temporary", .kind = Kind::button}).value.value();
    Status pending = Status::ok;
    button.bind([&] {
        check(bool(button.remove()));
        pending = ui.add_button({.id = "form.title.pages.v03.temporary", .kind = Kind::button}).status;
        return Result{};
    });
    check(bool(button.invoke()) && pending == Status::invalid_value);
    auto replacement = ui.add_button({.id = "form.title.pages.v03.temporary", .kind = Kind::button});
    check(bool(replacement) && button.describe().status == Status::not_found);
}
