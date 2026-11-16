#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "check.hpp"
using namespace lcars;
using test::check;
void exercise() {
    Ui ui;
    auto nav = ui.addNavigationList({.id="nav"},
        {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}, {{"three"}, u"Three", false, {}}}, {"one"});
    check(bool(nav), "navigation list is created");
    auto semantic = ui.semantics().get({"nav"});
    auto option = ui.semantics().get({"nav", OptionValue{"one"}});
    check(semantic && semantic->role == Role::list && semantic->selectionRequired && option && option->role == Role::listItem && option->selected,
          "navigation list exposes list and list-item semantics");
    check(option && std::find(option->verbs.begin(), option->verbs.end(), Verb::select) != option->verbs.end(), "navigation item advertises select");
    auto field = ui.addTextField({.id="field", .value=u"draft"}).value.value();
    check(bool(nav->addMember({"one"}, field)), "attach member");
    check(nav->selected()->id == "one", "initial page is selected");
    check(bool(ui.semantics().perform({"nav", OptionValue{"two"}}, Verb::select)) && nav->selected()->id == "two", "semantic navigation selects page");
    check(bool(ui.semantics().perform({"nav", OptionValue{"one"}}, Verb::select)), "semantic navigation restores page");
    int invalidations = 0; detail::access(ui).onInvalidate([&] { ++invalidations; });
    int changes = 0; nav->onPageChange([&](auto, auto) { ++changes; });
    check(bool(nav->select({"one"})) && changes == 0 && invalidations == 0, "reselect does not notify or redraw");
    check(bool(nav->focus()), "focus navigation");
    check(bool(detail::access(ui).keyDown({.key=Key::down})) && nav->selected()->id == "two", "down selects next page");
    check(field.setValue(u"other").status == Status::hidden, "inactive page refuses edits");
    detail::access(ui).keyDown({.key=Key::down});
    check(nav->selected()->id == "two", "disabled page skipped at end");
    detail::access(ui).keyDown({.key=Key::home});
    check(nav->selected()->id == "one", "home selects first page");
    check(nav->enablePage({"one"}, false).status == Status::invalidValue, "cannot disable selected page");
    check(ui.addNavigationList({.id="bad"}, {}, {"one"}).status == Status::invalidValue, "empty rejected");
    check(ui.addNavigationList({.id="bad"}, {{{"x"},u"X",true,{}},{{"x"},u"Y",true,{}}}, {"x"}).status == Status::invalidValue, "duplicate rejected");
    check(ui.addNavigationList({.id="bad"}, {{{"x"},u"X",false,{}}}, {"x"}).status == Status::invalidValue, "disabled initial rejected");
    auto lockedList = ui.addList({.id="locked", .readOnly=true}, {{{"x"}, u"X", false}}).value.value();
    check(!ui.semantics().get({"locked"})->selectionRequired, "ordinary list permits no selection");
    check(lockedList.select({"x"}).status == Status::disabled &&
          ui.semantics().perform({"locked", OptionValue{"x"}}, Verb::select).status == Status::disabled,
          "disabled item precedes read-only list");
    detail::access(ui).rendererFailed(true);
    check(lockedList.focus({"missing"}).status == Status::notFound &&
          ui.semantics().perform({"locked", OptionValue{"missing"}}, Verb::focus).status == Status::notFound,
          "missing item precedes renderer failure");
    detail::access(ui).rendererFailed(false);
    check(bool(field.focus()), "focus page member");
    bool blurred = false;
    field.onBlur([&] { blurred = true; });
    check(bool(nav->select({"two"})) && blurred && detail::access(ui).focused() == "nav", "switching a focused member blurs and focuses navigation");
    check(bool(nav->select({"one"})) && bool(field.focus()), "return to page member");
    detail::access(ui).pointerDown("nav", 2);
    detail::access(ui).pointerUp("nav", 2);
    check(detail::access(ui).focused() == "field" && nav->selected()->id == "one", "disabled pointer row does nothing");
    check(ui.addNavigationList({.id="bad"}, {{{"   "},u"X",true,{}}}, {"   "}).status == Status::invalidValue, "blank page identity rejected");
    detail::access(ui).pointerDown("nav", 1);
    detail::access(ui).pointerUp("nav", 1);
    check(nav->selected()->id == "two" && detail::access(ui).focused() == "nav", "enabled pointer row selects and focuses navigation");
    detail::access(ui).keyDown({.key=Key::end});
    check(nav->selected()->id == "two", "end selects last enabled page");
    for (Key key : {Key::left, Key::right, Key::space, Key::enter}) {
        detail::access(ui).keyDown({.key=key});
        detail::access(ui).keyUp(key);
        check(nav->selected()->id == "two", "non-navigation key does not select a different page");
    }
    detail::access(ui).keyDown({.key=Key::up});
    check(nav->selected()->id == "one", "up selects preceding page");
    std::u16string value; std::function<void()> notify;
    Ui shortcuts;
    auto text = shortcuts.addTextField({.id="text"}).value.value();
    auto save = shortcuts.addButton({.id="save"}).value.value();
    int saves = 0;
    save.bind([&] { ++saves; return Result{}; });
    check(bool(shortcuts.addShortcut({.letter=u'S',.ctrl=true}, save)), "register shortcut");
    check(shortcuts.semantics().get({"save"})->acceleratorKey == u"Ctrl+S", "semantic shortcut for Save");
    check(bool(text.focus()), "focus text for shortcut");
    check(bool(detail::access(shortcuts).keyDown({.ctrl=true,.letter=u'S'})) && saves == 1, "unhandled text chord reaches shortcut");
    detail::access(shortcuts).keyDown({.ctrl=true,.repeat=true,.letter=u'S'});
    check(saves == 1, "shortcut does not repeat");
    check(shortcuts.addShortcut({.key=Key::tab}, save).status == Status::invalidValue, "Tab cannot be registered");
    check(shortcuts.addShortcut({.key=Key::escape}, save).status == Status::invalidValue, "Escape cannot be registered");
    check(shortcuts.addShortcut({}, save).status == Status::invalidValue, "empty chord cannot be registered");
    check(shortcuts.addShortcut({.letter=u'S',.ctrl=true}, save).status == Status::invalidValue, "duplicate chord rejected");
    auto list = shortcuts.addList({.id="list"}, {}).value.value();
    auto scoped = shortcuts.addButton({.id="scoped"}).value.value();
    int edits = 0; scoped.bind([&] { ++edits; return Result{}; });
    check(bool(shortcuts.addShortcut({.key=Key::del}, scoped, list)), "register scoped Delete");
    check(shortcuts.semantics().get({"scoped"})->acceleratorKey == u"Delete", "semantic scoped shortcut");
    list.focus(); detail::access(shortcuts).keyDown({.key=Key::del});
    check(edits==1, "focused list routes Delete to scoped target");
    text.focus(); detail::access(shortcuts).keyDown({.key=Key::del});
    check(edits==1, "text field retains Delete");
    save.enable(false);
    check(detail::access(shortcuts).keyDown({.ctrl=true,.letter=u'S'}).status == Status::disabled && saves==1, "disabled shortcut target rejected");
    save.enable(true);
    text.connectSource({[&] { return value; }, [&](auto callback) { notify=std::move(callback); return [&] { notify={}; }; }});
    text.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    check(bool(detail::access(shortcuts).compositionUpdate(u"x",1)), "composition starts");
    detail::access(shortcuts).keyDown({.ctrl=true,.letter=u'S'});
    check(saves==1, "composition blocks shortcuts");
    detail::access(shortcuts).compositionCancel();
    Ui dialogUi;
    auto opener = dialogUi.addButton({.id="opener"}).value.value();
    auto backup = dialogUi.addButton({.id="backup"}).value.value();
    auto input = dialogUi.addTextField({.id="input"}).value.value();
    auto cancel = dialogUi.addButton({.id="cancel"}).value.value();
    auto textDialog = dialogUi.addDialog("text-dialog", {input,cancel}, input, backup, [](bool) {}).value.value();
    for (int i=0; i<4; ++i) {
        auto id="other"+std::to_string(i);
        auto button=dialogUi.addButton({.id=id}).value.value();
        check(bool(dialogUi.addDialog("dialog"+std::to_string(i), {button}, button, backup, [](bool) {})), "five dialogs register");
    }
    opener.focus();
    check(bool(textDialog.open()) && detail::access(dialogUi).focused()=="input", "dialog opens with text initial focus");
    detail::access(dialogUi).keyDown({.key=Key::tab});
    check(detail::access(dialogUi).focused()=="cancel", "Tab remains in dialog");
    detail::access(dialogUi).keyDown({.key=Key::tab,.shift=true});
    check(detail::access(dialogUi).focused()=="input", "Shift-Tab remains in dialog");
    check(bool(opener.remove()), "remove original opener");
    check(bool(textDialog.settle(false)) && detail::access(dialogUi).focused()=="backup", "removed opener returns focus to fallback");
    auto pages = dialogUi.addNavigationList({.id="pages"}, {{{"a"},u"A",true,{}}}, {"a"}).value.value();
    check(pages.addMember({"a"},input).status == Status::invalidValue, "dialog member cannot also belong to page");
    Ui reentry;
    auto source = reentry.addTextField({.id="source"}).value.value();
    auto fallback2 = reentry.addButton({.id="backup"}).value.value();
    auto a = reentry.addButton({.id="a"}).value.value();
    auto b = reentry.addButton({.id="b"}).value.value();
    auto firstDialog = reentry.addDialog("a-dialog", {a}, a, fallback2, [](bool) {}).value.value();
    auto secondDialog = reentry.addDialog("b-dialog", {b}, b, fallback2, [](bool) {}).value.value();
    Result nested, hiddenDuringTransition;
    source.onBlur([&] { nested=secondDialog.open(); hiddenDuringTransition=secondDialog.settle(false); });
    source.focus();
    check(bool(firstDialog.open()) && nested.status==Status::busy && !*secondDialog.isOpen(), "opening cannot reenter another dialog during blur");
    check(hiddenDuringTransition.status==Status::hidden, "hidden dialog takes precedence over transition busy");
    Ui rapidUi;
    auto rapidBackground = rapidUi.addButton({.id="background"}).value.value();
    auto rapidButton = rapidUi.addButton({.id="member"}).value.value();
    auto rapidDialog = rapidUi.addDialog("rapid", {rapidButton}, rapidButton, rapidBackground, [](bool) {}).value.value();
    std::vector<bool> visibilityChanges;
    auto observer = rapidUi.semantics().subscribe([&](const SemanticEvent& event) {
        if (event.ref.id == "rapid" && event.property == "hidden" && event.hidden) visibilityChanges.push_back(*event.hidden);
    });
    check(bool(rapidDialog.open()) && bool(rapidDialog.settle(false)), "rapid dialog transitions succeed");
    rapidUi.semantics().dispatchEvents();
    check(visibilityChanges == std::vector<bool>{false, true}, "queued dialog events retain each visibility transition");
    Ui modalUi;
    auto first = modalUi.addButton({.id="first"}).value.value();
    auto fallback = modalUi.addButton({.id="fallback"}).value.value();
    auto dialog = modalUi.addDialog("dialog", {first}, first, fallback, [](bool) {}).value.value();
    auto nav2 = modalUi.addNavigationList({.id="nav2"}, {{{"a"},u"A",true,{}},{{"b"},u"B",true,{}}}, {"a"}).value.value();
    auto second = modalUi.addButton({.id="second"}).value.value();
    auto dialog2 = modalUi.addDialog("dialog2", {second}, second, fallback, [](bool) {});
    check(bool(dialog2), "multiple dialogs can be registered");
    auto firstChildren = modalUi.semantics().children({"dialog"});
    auto secondChildren = modalUi.semantics().children({"dialog2"});
    check(firstChildren && firstChildren->size() == 1 && (*firstChildren)[0].id == "first" &&
          secondChildren && secondChildren->size() == 1 && (*secondChildren)[0].id == "second",
          "each semantic dialog owns only its own members");
    check(bool(dialog.open(fallback)), "open modal");
    check(dialog2->open(fallback).status == Status::busy, "second dialog cannot open concurrently");
    check(!modalUi.semantics().get({"dialog"})->hidden && modalUi.semantics().get({"dialog2"})->hidden &&
          !modalUi.semantics().get({"first"})->hidden && modalUi.semantics().get({"second"})->hidden,
          "only the active semantic dialog and its members are visible");
    check(bool(dialog.settle(false)) && bool(dialog2->open()), "second dialog opens after first settles");
    check(*dialog2->isOpen() && !*dialog.isOpen(), "only active dialog reports open");
    check(modalUi.semantics().get({"dialog"})->hidden && !modalUi.semantics().get({"dialog2"})->hidden &&
          modalUi.semantics().get({"first"})->hidden && !modalUi.semantics().get({"second"})->hidden,
          "semantic dialog visibility follows the active dialog");
    check(bool(dialog2->settle(false)) && bool(dialog.open(fallback)), "first dialog can reopen");
    check(nav2.select({"b"}).status == Status::blockedByModal && nav2.selected()->id == "a", "modal blocks navigation selection");
    check(modalUi.addNavigationList({.id="nav"}, {{{"x"},u"X",true,{first}}}, {"x"}).status == Status::invalidValue, "dialog member rejected");
}

int main() { const test::Case cases[] = {{"navigation", exercise}}; return test::run("navigation", cases); }
