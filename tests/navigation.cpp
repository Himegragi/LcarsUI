#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "check.hpp"
using namespace lcars;
using test::check;
void exercise() {
    Ui ui;
    auto nav = ui.add_navigation_list({.id="nav"},
        {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}, {{"three"}, u"Three", false, {}}}, {"one"});
    check(bool(nav), "navigation list is created");
    auto semantic = ui.semantics().get({"nav"});
    auto option = ui.semantics().get({"nav", OptionValue{"one"}});
    check(semantic && semantic->role == Role::list && semantic->selection_required && option && option->role == Role::list_item && option->selected,
          "navigation list exposes list and list-item semantics");
    check(option && std::find(option->verbs.begin(), option->verbs.end(), Verb::select) != option->verbs.end(), "navigation item advertises select");
    auto field = ui.add_text({.id="field", .value=u"draft"}).value.value();
    check(bool(nav->add_member({"one"}, field)), "attach member");
    check(nav->selected()->id == "one", "initial page is selected");
    check(bool(ui.semantics().perform({"nav", OptionValue{"two"}}, Verb::select)) && nav->selected()->id == "two", "semantic navigation selects page");
    check(bool(ui.semantics().perform({"nav", OptionValue{"one"}}, Verb::select)), "semantic navigation restores page");
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
    check(ui.add_navigation_list({.id="bad"}, {}, {"one"}).status == Status::invalid_value, "empty rejected");
    check(ui.add_navigation_list({.id="bad"}, {{{"x"},u"X",true,{}},{{"x"},u"Y",true,{}}}, {"x"}).status == Status::invalid_value, "duplicate rejected");
    check(ui.add_navigation_list({.id="bad"}, {{{"x"},u"X",false,{}}}, {"x"}).status == Status::invalid_value, "disabled initial rejected");
    auto locked_list = ui.add_list({.id="locked", .read_only=true}, {{{"x"}, u"X", false}}).value.value();
    check(!ui.semantics().get({"locked"})->selection_required, "ordinary list permits no selection");
    check(locked_list.select({"x"}).status == Status::disabled &&
          ui.semantics().perform({"locked", OptionValue{"x"}}, Verb::select).status == Status::disabled,
          "disabled item precedes read-only list");
    detail::access(ui).renderer_failed(true);
    check(locked_list.focus({"missing"}).status == Status::not_found &&
          ui.semantics().perform({"locked", OptionValue{"missing"}}, Verb::focus).status == Status::not_found,
          "missing item precedes renderer failure");
    detail::access(ui).renderer_failed(false);
    check(bool(field.focus()), "focus page member");
    bool blurred = false;
    field.on_blur([&] { blurred = true; });
    check(bool(nav->select({"two"})) && blurred && detail::access(ui).focused() == "nav", "switching a focused member blurs and focuses navigation");
    check(bool(nav->select({"one"})) && bool(field.focus()), "return to page member");
    detail::access(ui).pointer_down("nav", 2);
    detail::access(ui).pointer_up("nav", 2);
    check(detail::access(ui).focused() == "field" && nav->selected()->id == "one", "disabled pointer row does nothing");
    check(ui.add_navigation_list({.id="bad"}, {{{"   "},u"X",true,{}}}, {"   "}).status == Status::invalid_value, "blank page identity rejected");
    detail::access(ui).pointer_down("nav", 1);
    detail::access(ui).pointer_up("nav", 1);
    check(nav->selected()->id == "two" && detail::access(ui).focused() == "nav", "enabled pointer row selects and focuses navigation");
    detail::access(ui).key_down({.key=Key::end});
    check(nav->selected()->id == "two", "end selects last enabled page");
    for (Key key : {Key::left, Key::right, Key::space, Key::enter}) {
        detail::access(ui).key_down({.key=key});
        detail::access(ui).key_up(key);
        check(nav->selected()->id == "two", "non-navigation key does not select a different page");
    }
    detail::access(ui).key_down({.key=Key::up});
    check(nav->selected()->id == "one", "up selects preceding page");
    std::u16string value; std::function<void()> notify;
    Ui shortcuts;
    auto text = shortcuts.add_text({.id="text"}).value.value();
    auto save = shortcuts.add_button({.id="save"}).value.value();
    int saves = 0;
    save.bind([&] { ++saves; return Result{}; });
    check(bool(shortcuts.add_shortcut({.letter=u'S',.ctrl=true}, save)), "register shortcut");
    check(shortcuts.semantics().get({"save"})->accelerator_key == u"Ctrl+S", "semantic shortcut for Save");
    check(bool(text.focus()), "focus text for shortcut");
    check(bool(detail::access(shortcuts).key_down({.ctrl=true,.letter=u'S'})) && saves == 1, "unhandled text chord reaches shortcut");
    detail::access(shortcuts).key_down({.ctrl=true,.repeat=true,.letter=u'S'});
    check(saves == 1, "shortcut does not repeat");
    check(shortcuts.add_shortcut({.key=Key::tab}, save).status == Status::invalid_value, "Tab cannot be registered");
    check(shortcuts.add_shortcut({.key=Key::escape}, save).status == Status::invalid_value, "Escape cannot be registered");
    check(shortcuts.add_shortcut({}, save).status == Status::invalid_value, "empty chord cannot be registered");
    check(shortcuts.add_shortcut({.letter=u'S',.ctrl=true}, save).status == Status::invalid_value, "duplicate chord rejected");
    auto list = shortcuts.add_list({.id="list"}, {}).value.value();
    auto scoped = shortcuts.add_button({.id="scoped"}).value.value();
    int edits = 0; scoped.bind([&] { ++edits; return Result{}; });
    check(bool(shortcuts.add_shortcut({.key=Key::del}, scoped, list)), "register scoped Delete");
    check(shortcuts.semantics().get({"scoped"})->accelerator_key == u"Delete", "semantic scoped shortcut");
    list.focus(); detail::access(shortcuts).key_down({.key=Key::del});
    check(edits==1, "focused list routes Delete to scoped target");
    text.focus(); detail::access(shortcuts).key_down({.key=Key::del});
    check(edits==1, "text field retains Delete");
    save.enable(false);
    check(detail::access(shortcuts).key_down({.ctrl=true,.letter=u'S'}).status == Status::disabled && saves==1, "disabled shortcut target rejected");
    save.enable(true);
    text.connect_source({[&] { return value; }, [&](auto callback) { notify=std::move(callback); return [&] { notify={}; }; }});
    text.connect_confirmed([](const Change<TextHandle>&) { return Result{}; });
    check(bool(detail::access(shortcuts).composition_update(u"x",1)), "composition starts");
    detail::access(shortcuts).key_down({.ctrl=true,.letter=u'S'});
    check(saves==1, "composition blocks shortcuts");
    detail::access(shortcuts).composition_cancel();
    Ui dialog_ui;
    auto opener = dialog_ui.add_button({.id="opener"}).value.value();
    auto backup = dialog_ui.add_button({.id="backup"}).value.value();
    auto input = dialog_ui.add_text({.id="input"}).value.value();
    auto cancel = dialog_ui.add_button({.id="cancel"}).value.value();
    auto text_dialog = dialog_ui.add_dialog("text-dialog", {input,cancel}, input, backup, [](bool) {}).value.value();
    for (int i=0; i<4; ++i) {
        auto id="other"+std::to_string(i);
        auto button=dialog_ui.add_button({.id=id}).value.value();
        check(bool(dialog_ui.add_dialog("dialog"+std::to_string(i), {button}, button, backup, [](bool) {})), "five dialogs register");
    }
    opener.focus();
    check(bool(text_dialog.open()) && detail::access(dialog_ui).focused()=="input", "dialog opens with text initial focus");
    detail::access(dialog_ui).key_down({.key=Key::tab});
    check(detail::access(dialog_ui).focused()=="cancel", "Tab remains in dialog");
    detail::access(dialog_ui).key_down({.key=Key::tab,.shift=true});
    check(detail::access(dialog_ui).focused()=="input", "Shift-Tab remains in dialog");
    check(bool(opener.remove()), "remove original opener");
    check(bool(text_dialog.settle(false)) && detail::access(dialog_ui).focused()=="backup", "removed opener returns focus to fallback");
    auto pages = dialog_ui.add_navigation_list({.id="pages"}, {{{"a"},u"A",true,{}}}, {"a"}).value.value();
    check(pages.add_member({"a"},input).status == Status::invalid_value, "dialog member cannot also belong to page");
    Ui reentry;
    auto source = reentry.add_text({.id="source"}).value.value();
    auto fallback2 = reentry.add_button({.id="backup"}).value.value();
    auto a = reentry.add_button({.id="a"}).value.value();
    auto b = reentry.add_button({.id="b"}).value.value();
    auto first_dialog = reentry.add_dialog("a-dialog", {a}, a, fallback2, [](bool) {}).value.value();
    auto second_dialog = reentry.add_dialog("b-dialog", {b}, b, fallback2, [](bool) {}).value.value();
    Result nested, hidden_during_transition;
    source.on_blur([&] { nested=second_dialog.open(); hidden_during_transition=second_dialog.settle(false); });
    source.focus();
    check(bool(first_dialog.open()) && nested.status==Status::busy && !*second_dialog.is_open(), "opening cannot reenter another dialog during blur");
    check(hidden_during_transition.status==Status::hidden, "hidden dialog takes precedence over transition busy");
    Ui rapid_ui;
    auto rapid_background = rapid_ui.add_button({.id="background"}).value.value();
    auto rapid_button = rapid_ui.add_button({.id="member"}).value.value();
    auto rapid_dialog = rapid_ui.add_dialog("rapid", {rapid_button}, rapid_button, rapid_background, [](bool) {}).value.value();
    std::vector<bool> visibility_changes;
    auto observer = rapid_ui.semantics().subscribe([&](const SemanticEvent& event) {
        if (event.ref.id == "rapid" && event.property == "hidden" && event.hidden) visibility_changes.push_back(*event.hidden);
    });
    check(bool(rapid_dialog.open()) && bool(rapid_dialog.settle(false)), "rapid dialog transitions succeed");
    rapid_ui.semantics().dispatch_events();
    check(visibility_changes == std::vector<bool>{false, true}, "queued dialog events retain each visibility transition");
    Ui modal_ui;
    auto first = modal_ui.add_button({.id="first"}).value.value();
    auto fallback = modal_ui.add_button({.id="fallback"}).value.value();
    auto dialog = modal_ui.add_dialog("dialog", {first}, first, fallback, [](bool) {}).value.value();
    auto nav2 = modal_ui.add_navigation_list({.id="nav2"}, {{{"a"},u"A",true,{}},{{"b"},u"B",true,{}}}, {"a"}).value.value();
    auto second = modal_ui.add_button({.id="second"}).value.value();
    auto dialog2 = modal_ui.add_dialog("dialog2", {second}, second, fallback, [](bool) {});
    check(bool(dialog2), "multiple dialogs can be registered");
    auto first_children = modal_ui.semantics().children({"dialog"});
    auto second_children = modal_ui.semantics().children({"dialog2"});
    check(first_children && first_children->size() == 1 && (*first_children)[0].id == "first" &&
          second_children && second_children->size() == 1 && (*second_children)[0].id == "second",
          "each semantic dialog owns only its own members");
    check(bool(dialog.open(fallback)), "open modal");
    check(dialog2->open(fallback).status == Status::busy, "second dialog cannot open concurrently");
    check(!modal_ui.semantics().get({"dialog"})->hidden && modal_ui.semantics().get({"dialog2"})->hidden &&
          !modal_ui.semantics().get({"first"})->hidden && modal_ui.semantics().get({"second"})->hidden,
          "only the active semantic dialog and its members are visible");
    check(bool(dialog.settle(false)) && bool(dialog2->open()), "second dialog opens after first settles");
    check(*dialog2->is_open() && !*dialog.is_open(), "only active dialog reports open");
    check(modal_ui.semantics().get({"dialog"})->hidden && !modal_ui.semantics().get({"dialog2"})->hidden &&
          modal_ui.semantics().get({"first"})->hidden && !modal_ui.semantics().get({"second"})->hidden,
          "semantic dialog visibility follows the active dialog");
    check(bool(dialog2->settle(false)) && bool(dialog.open(fallback)), "first dialog can reopen");
    check(nav2.select({"b"}).status == Status::blocked_by_modal && nav2.selected()->id == "a", "modal blocks navigation selection");
    check(modal_ui.add_navigation_list({.id="nav"}, {{{"x"},u"X",true,{first}}}, {"x"}).status == Status::invalid_value, "dialog member rejected");
}

int main() { const test::Case cases[] = {{"navigation", exercise}}; return test::run("navigation", cases); }
