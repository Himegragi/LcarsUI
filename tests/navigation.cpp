#include "lcars/core.hpp"
#include "core_internal.hpp"
#include "check.hpp"
using namespace lcars;
using test::check;
void exercise() {
    Ui ui;
    auto nav = ui.add_navigation_list({.id="nav", .kind=Kind::navigation_list},
        {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}, {{"three"}, u"Three", false, {}}}, {"one"});
    check(bool(nav), "navigation list is created");
    auto field = ui.add_text({.id="field", .kind=Kind::text, .value=u"draft"}).value.value();
    check(bool(nav->add_member({"one"}, field)), "attach member");
    check(nav->selected()->id == "one", "initial page is selected");
    int invalidations = 0; detail::access(ui).on_invalidate([&] { ++invalidations; });
    int changes = 0; nav->on_page_change([&](auto, auto) { ++changes; });
    check(bool(nav->select({"one"})) && changes == 0 && invalidations == 0, "reselect does not notify or redraw");
    check(bool(nav->focus()), "focus navigation");
    check(bool(detail::access(ui).key_down({.key=Key::down})) && nav->selected()->id == "two", "down selects next page");
    check(field.set_value(u"other").status == Status::hidden, "inactive page refuses edits");
    detail::access(ui).key_down({.key=Key::down});
    check(nav->selected()->id == "two", "disabled page skipped at end");
    detail::access(ui).key_down({.key=Key::home});
    check(nav->selected()->id == "one", "home selects first page");
    check(nav->enable_page({"one"}, false).status == Status::invalid_value, "cannot disable selected page");
    check(ui.add_navigation_list({.id="bad", .kind=Kind::navigation_list}, {}, {"one"}).status == Status::invalid_value, "empty rejected");
    check(ui.add_navigation_list({.id="bad", .kind=Kind::navigation_list}, {{{"x"},u"X",true,{}},{{"x"},u"Y",true,{}}}, {"x"}).status == Status::invalid_value, "duplicate rejected");
    check(ui.add_navigation_list({.id="bad", .kind=Kind::navigation_list}, {{{"x"},u"X",false,{}}}, {"x"}).status == Status::invalid_value, "disabled initial rejected");
    check(bool(field.focus()), "focus page member");
    bool blurred = false;
    field.on_blur([&] { blurred = true; });
    check(bool(nav->select({"two"})) && blurred && detail::access(ui).focused() == "nav", "switching a focused member blurs and focuses navigation");
    check(bool(nav->select({"one"})) && bool(field.focus()), "return to page member");
    detail::access(ui).pointer_down("nav", 2);
    detail::access(ui).pointer_up("nav", 2);
    check(detail::access(ui).focused() == "field" && nav->selected()->id == "one", "disabled pointer row does nothing");
    check(ui.add_navigation_list({.id="bad", .kind=Kind::navigation_list}, {{{"   "},u"X",true,{}}}, {"   "}).status == Status::invalid_value, "blank page identity rejected");
    detail::access(ui).pointer_down("nav", 1);
    detail::access(ui).pointer_up("nav", 1);
    check(nav->selected()->id == "two" && detail::access(ui).focused() == "nav", "enabled pointer row selects and focuses navigation");
    Ui modal_ui;
    auto first = modal_ui.add_button({.id="first",.kind=Kind::button}).value.value();
    auto fallback = modal_ui.add_button({.id="fallback",.kind=Kind::button}).value.value();
    auto dialog = modal_ui.add_dialog("dialog", {first}, first, fallback, [](bool) {}).value.value();
    auto nav2 = modal_ui.add_navigation_list({.id="nav2",.kind=Kind::navigation_list}, {{{"a"},u"A",true,{}},{{"b"},u"B",true,{}}}, {"a"}).value.value();
    check(bool(dialog.open(fallback)), "open modal");
    check(nav2.select({"b"}).status == Status::blocked_by_modal && nav2.selected()->id == "a", "modal blocks navigation selection");
    check(modal_ui.add_navigation_list({.id="nav",.kind=Kind::navigation_list}, {{{"x"},u"X",true,{first}}}, {"x"}).status == Status::invalid_value, "dialog member rejected");
}

int main() { const test::Case cases[] = {{"navigation", exercise}}; return test::run("navigation", cases); }
