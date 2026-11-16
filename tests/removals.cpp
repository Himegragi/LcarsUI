#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include <array>
#include <functional>
#include <iostream>

using namespace lcars;
using test::check;
namespace {
const std::vector<OptionItem> choices{{{"a"}, u"Alpha"}, {{"b"}, u"Beta"}};
ComponentHandle make(Ui& ui, int kind, std::string id = "removed") {
    ComponentConfig c{.id = std::move(id), .label = u"Fallback"};
    switch (kind) {
    case 0: return ui.add_label(c).value.value();
    case 1: return ui.add_text_field(c).value.value();
    case 2: return ui.add_checkbox(c).value.value();
    case 3: return ui.add_radio_group(c, choices).value.value();
    case 4: return ui.add_slider(c, {0, 100, 5}).value.value();
    case 5: return ui.add_progress(c).value.value();
    case 6: return ui.add_dropdown(c, choices).value.value();
    case 7: return ui.add_list(c, choices).value.value();
    default: return ui.add_button(c).value.value();
    }
}
Result remove(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Result {
        if constexpr (requires { h.remove(); }) return h.remove();
        else return {Status::not_supported, {}};
    }, handle);
}
Query<ComponentSnapshot> describe(const ComponentHandle& handle) {
    return std::visit([](const auto& h) -> Query<ComponentSnapshot> {
        if constexpr (requires { h.describe(); }) return h.describe();
        else return {Status::not_supported, {}, {}};
    }, handle);
}
void all_eight_kinds_can_be_removed() {
    for (int kind = 0; kind < 8; ++kind) {
        std::cout << "kind " << kind << '\n';
        Ui ui;
        auto handle = make(ui, kind);
        check(bool(describe(handle)), "The component starts alive");
        check(bool(remove(handle)), "Owner removal is accepted");
        check(describe(handle).status == Status::not_found, "A removed handle is invalid");
        check(remove(handle).status == Status::not_found, "Removal cannot be repeated");
        auto replacement = make(ui, kind);
        check(handle != replacement && describe(handle).status == Status::not_found, "Reusing an id never revives the old handle");
    }
}
}
int main() {
    const test::Case cases[] = {{"#104 eight removable kinds", all_eight_kinds_can_be_removed}};
    return test::run("removals", cases);
}
