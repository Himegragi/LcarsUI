#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "lcars/layout.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
using namespace lcars;
using test::check;

namespace {
void normal() {
    Ui ui;
    auto label = ui.add_label({.id = "label", .label = u"Fallback", .value = u"Displayed"}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    arrange(layout::component(label), ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    const auto component = label.describe();
    const auto node = ui.semantics().get({"label"});
    check(component && node && component->name == u"Displayed" && component->value == u"Displayed" &&
          component->name == node->name, "normal/label: both snapshots name a label by its displayed text");
}
}
int main() {
    const test::Case cases[] = {{"normal", normal}};
    return test::run("snapshots", cases);
}
