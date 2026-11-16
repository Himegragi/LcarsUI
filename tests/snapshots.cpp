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
static_assert(std::is_same_v<decltype(ComponentSnapshot::writing_mode), std::optional<WritingMode>>);
static_assert(std::is_same_v<decltype(ComponentSnapshot::vertical_digits), std::optional<int>>);
static_assert(std::is_same_v<decltype(ComponentSnapshot::range), std::optional<SliderRange>>);
static_assert(std::is_same_v<decltype(ComponentSnapshot::scroll), std::optional<TextScroll>>);

// #102 can replace this predicate with CommonState equality without changing any state driver.
bool same_common_fields(const ComponentSnapshot& a, const NodeSnapshot& b) {
    auto fields = [](const auto& s) {
        return std::tie(s.name, s.value, s.description, s.error, s.enabled, s.read_only, s.focusable,
                        s.focused, s.invalid, s.executing, s.visible, s.expanded, s.checked,
                        s.numeric_value, s.writing_mode, s.vertical_digits, s.scroll, s.text_align,
                        s.justify_method, s.line_spacing, s.paragraph_spacing, s.rich);
    };
    const auto range = [](const auto& s) {
        return s.range ? std::optional{std::tuple{s.range->minimum, s.range->maximum, s.range->step}} : std::nullopt;
    };
    return fields(a) == fields(b) && range(a) == range(b);
}

void normal() {
    Ui ui;
    auto label = ui.add_label({.id = "label", .label = u"Fallback", .value = u"Displayed"}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    arrange(layout::component(label), ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    const auto component = label.describe();
    const auto node = ui.semantics().get({"label"});
    check(component && node && component->name == u"Displayed" && component->value == u"Displayed" &&
          component->name == node->name, "normal/label: both snapshots name a label by its displayed text");
    check(same_common_fields(*component, *node), "normal/label: all common fields agree");
    auto field = ui.add_text({.id = "multi", .value = u"a\nb\nc\nd", .multiline = true, .visible_lines = 2}).value.value();
    arrange(layout::component(field), ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    check(same_common_fields(*field.describe(), *ui.semantics().get({"multi"})), "normal/multi: all common fields agree");
    check(!field.describe()->writing_mode && !field.describe()->vertical_digits &&
          field.describe()->scroll == std::optional{TextScroll{0, 2, 4}}, "normal/multi: only labels have writing settings, scrolling is one value");
}
}
int main() {
    const test::Case cases[] = {{"normal", normal}};
    return test::run("snapshots", cases);
}
