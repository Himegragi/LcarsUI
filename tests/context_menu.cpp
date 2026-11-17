#include "core_internal.hpp"
#include "component_type.hpp"
// Context menu (#85) through the public core interface: attach, open, query, keyboard, execute,
// semantics and geometry. No window, GPU or font backend; the real-window twin is in host_verify.
#include "check.hpp"
#include "fake_measure.hpp"
#include "geometry.hpp"
#include "isoduo/core.hpp"
#include "isoduo/semantics.hpp"
#include <algorithm>
using namespace isoduo;
using test::check;
using test::inside;

namespace {
struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override {
        if (!content) return {Status::notSupported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, *content};
    }
};
// An editable, immediately written text field over an application-owned model.
struct Field {
    TextFieldHandle handle;
    std::shared_ptr<std::u16string> model;
};
Field addField(Ui& ui, std::string id, std::u16string value, bool readOnly = false) {
    auto model = std::make_shared<std::u16string>(value);
    auto handle = ui.addTextField({.id = id, .label = u"欄位", .value = value, .readOnly = readOnly}).value.value();
    check(bool(handle.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "connect field source");
    check(bool(handle.connectImmediate([model](const Change<TextFieldHandle>& change) { *model = change.value; return Result{}; })), "connect field write");
    return {handle, model};
}
detail::UiState& core(Ui& ui) { return detail::access(ui); }
Result press(Ui& ui, Key key, bool shift = false) { return core(ui).keyDown({.key = key, .shift = shift}); }
Result type(Ui& ui, char16_t ch) { return core(ui).textInput(std::u16string(1, ch)); }
std::vector<std::string> idsOf(const MenuSnapshot& menu) {
    std::vector<std::string> ids;
    for (const auto& item : menu.items) ids.push_back(item.separator ? "-" : item.id);
    return ids;
}
const MenuItemSnapshot* findItem(const MenuSnapshot& menu, std::string_view id) {
    auto it = std::find_if(menu.items.begin(), menu.items.end(), [&](const auto& item) { return item.id == id; });
    return it == menu.items.end() ? nullptr : &*it;
}
bool enabled(Ui& ui, std::string_view id) {
    auto menu = ui.contextMenu();
    auto item = menu ? findItem(*menu, id) : nullptr;
    return item && item->enabled;
}
bool hasVerb(Ui& ui, std::string_view id, Verb verb) {
    auto node = ui.semantics().get({std::string(id)});
    return node && std::find(node->verbs.begin(), node->verbs.end(), verb) != node->verbs.end();
}
std::string highlighted(Ui& ui) {
    auto menu = ui.contextMenu();
    if (!menu) return "<closed>";
    for (const auto& item : menu->items) if (item.highlighted) return item.id;
    return "";
}
const std::vector<std::string> builtinLayout{"lcars.undo", "lcars.redo", "-", "lcars.cut", "lcars.copy", "lcars.paste", "lcars.delete", "-", "lcars.select_all"};

void textFieldMenuHasTheWindowsEditItems() {
    Ui ui; auto field = addField(ui, "note", u"hello world");
    check(!*ui.contextMenuOpen(), "no menu is open at the start");
    check(ui.contextMenu().status == Status::hidden, "querying a closed menu reports hidden");
    check(bool(ui.openContextMenu(field.handle)), "a text field opens its built-in menu");
    check(*ui.contextMenuOpen() && std::holds_alternative<TextFieldHandle>(ui.contextMenu()->target) &&
          std::get<TextFieldHandle>(ui.contextMenu()->target) == field.handle, "the menu is open for the field");
    auto menu = ui.contextMenu();
    check(idsOf(*menu) == builtinLayout, "built-in items: undo, redo, line, cut, copy, paste, delete, line, select all");
    check(findItem(*menu, "lcars.undo")->label == u"復原(U)" && findItem(*menu, "lcars.redo")->label == u"重做(R)" &&
          findItem(*menu, "lcars.cut")->label == u"剪下(T)" && findItem(*menu, "lcars.copy")->label == u"複製(C)" &&
          findItem(*menu, "lcars.paste")->label == u"貼上(P)" && findItem(*menu, "lcars.delete")->label == u"刪除(D)" &&
          findItem(*menu, "lcars.select_all")->label == u"全選(A)", "labels follow the Windows edit menu");
    check(findItem(*menu, "lcars.undo")->accessKey == u'U' && findItem(*menu, "lcars.redo")->accessKey == u'R' &&
          findItem(*menu, "lcars.cut")->accessKey == u'T' && findItem(*menu, "lcars.copy")->accessKey == u'C' &&
          findItem(*menu, "lcars.paste")->accessKey == u'P' && findItem(*menu, "lcars.delete")->accessKey == u'D' &&
          findItem(*menu, "lcars.select_all")->accessKey == u'A', "quick keys match the letters in the labels");
    check(findItem(*menu, "lcars.cut")->shortcutHint == u"Ctrl+X" && findItem(*menu, "lcars.paste")->shortcutHint == u"Ctrl+V" &&
          findItem(*menu, "lcars.delete")->shortcutHint == u"Del", "shortcut hints are shown");
}

void itemsAreUnavailableWhenTheyCannotAct() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard);
    auto field = addField(ui, "note", u"hello world");
    ui.openContextMenu(field.handle);
    check(!enabled(ui, "lcars.cut") && !enabled(ui, "lcars.copy") && !enabled(ui, "lcars.delete"), "no selection: cut, copy and delete are off");
    check(!enabled(ui, "lcars.paste"), "empty clipboard: paste is off");
    check(!enabled(ui, "lcars.undo") && !enabled(ui, "lcars.redo"), "no history: undo and redo are off");
    check(enabled(ui, "lcars.select_all"), "select all is on");
    ui.closeContextMenu();
    clipboard.content = u"x";
    check(bool(field.handle.select(0, 5)), "select a range");
    ui.openContextMenu(field.handle);
    check(enabled(ui, "lcars.cut") && enabled(ui, "lcars.copy") && enabled(ui, "lcars.delete") && enabled(ui, "lcars.paste"),
          "a selection and clipboard text turn cut, copy, delete and paste on");
    check(bool(ui.invokeMenuItem("lcars.delete")), "delete the selection through the menu");
    ui.openContextMenu(field.handle);
    check(enabled(ui, "lcars.undo") && !enabled(ui, "lcars.redo"), "an edit turns undo on");
    ui.invokeMenuItem("lcars.undo");
    ui.openContextMenu(field.handle);
    check(!enabled(ui, "lcars.undo") && enabled(ui, "lcars.redo"), "after undo, redo is on");
}

void readOnlyFieldOnlyCopiesAndSelects() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard); clipboard.content = u"x";
    auto field = addField(ui, "note", u"hello", true);
    field.handle.select(0, 5);
    check(bool(ui.openContextMenu(field.handle)), "a read-only field still has its menu");
    for (const char* id : {"lcars.undo", "lcars.redo", "lcars.cut", "lcars.paste", "lcars.delete"})
        check(!enabled(ui, id), "a read-only field refuses every editing item");
    check(enabled(ui, "lcars.copy") && enabled(ui, "lcars.select_all"), "copy and select all stay on");
}

// Every built-in item is the same operation as its shortcut: value, clipboard and history agree.
void builtInItemsMatchTheirShortcuts() {
    // Besides menu == shortcut, each case states what the operation must do on "hello world" with "hello" selected
    // (code review 2026-11-12, item 16): the value after it, the clipboard after it, and the final selection.
    struct Case { const char* item; KeyPress key; const char16_t* clipboard; const char16_t* value; std::optional<std::u16string> clipboardAfter; unsigned anchor, caret; };
    const Case cases[] = {
        {"lcars.cut", {.ctrl = true, .letter = u'X'}, nullptr, u" world", u"hello", 0, 0},
        {"lcars.copy", {.ctrl = true, .letter = u'C'}, nullptr, u"hello world", u"hello", 0, 5},
        {"lcars.paste", {.ctrl = true, .letter = u'V'}, u"PASTED", u"PASTED world", u"PASTED", 6, 6},
        {"lcars.delete", {.key = Key::del}, nullptr, u" world", std::nullopt, 0, 0},
        {"lcars.select_all", {.ctrl = true, .letter = u'A'}, nullptr, u"hello world", std::nullopt, 0, 11},
    };
    for (const auto& c : cases) {
        Ui byMenu, byKey; MemoryClipboard menuClipboard, keyClipboard;
        core(byMenu).setClipboard(&menuClipboard); core(byKey).setClipboard(&keyClipboard);
        if (c.clipboard) { menuClipboard.content = c.clipboard; keyClipboard.content = c.clipboard; }
        auto a = addField(byMenu, "f", u"hello world"), b = addField(byKey, "f", u"hello world");
        a.handle.select(0, 5); b.handle.select(0, 5);
        b.handle.focus();
        check(bool(byMenu.openContextMenu(a.handle)), "open for the menu twin");
        check(bool(byMenu.invokeMenuItem(c.item)), "run the menu item");
        check(bool(core(byKey).keyDown(c.key)), "run the shortcut");
        check(*a.model == c.value && *b.model == c.value, (std::string(c.item) + ": the value after the operation").c_str());
        check(menuClipboard.content == c.clipboardAfter && keyClipboard.content == c.clipboardAfter, (std::string(c.item) + ": the clipboard after the operation").c_str());
        check(*a.model == *b.model, "the menu item and its shortcut leave the same value");
        check(a.handle.draft().value.value() == b.handle.draft().value.value(), "same field text");
        check(menuClipboard.content == keyClipboard.content, "the same clipboard content");
        auto sa = *core(byMenu).describe("f"), sb = *core(byKey).describe("f");
        check(sa.verbs == sb.verbs, "the same undo history (undo and redo operations)");
        auto selectionA = byMenu.semantics().get({"f"})->textSelection, selectionB = byKey.semantics().get({"f"})->textSelection;
        check(selectionA && selectionB && selectionA->anchor == selectionB->anchor && selectionA->caret == selectionB->caret, "the same selection");
        check(selectionA && selectionA->anchor == c.anchor && selectionA->caret == c.caret, (std::string(c.item) + ": the selection after the operation").c_str());
        check(a.handle.undo().status == b.handle.undo().status && *a.model == *b.model, "undo behaves the same afterwards");
    }
}

// A rich (multi-line, styled) field over an application-owned model, written at once.
struct RichField {
    TextFieldHandle handle;
    std::shared_ptr<RichText> model;
};
RichField addRichField(Ui& ui, std::string id, std::u16string value, bool readOnly = false) {
    auto model = std::make_shared<RichText>(RichText{value, {}});
    auto handle = ui.addTextField({.id = id, .label = u"內容", .value = value, .readOnly = readOnly, .multiline = true, .rich = true}).value.value();
    check(bool(handle.connectRichSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "connect rich source");
    check(bool(handle.connectRichImmediate([model](const Change<TextFieldHandle, RichText>& change) { *model = change.value; return Result{}; })), "connect rich write");
    return {handle, model};
}
const std::vector<std::string> richLayout{"lcars.undo", "lcars.redo", "-", "lcars.cut", "lcars.copy", "lcars.paste", "lcars.delete", "-", "lcars.select_all",
                                           "-", "lcars.bold", "lcars.italic", "lcars.underline"};

void richFieldMenuAddsBoldItalicUnderline() {
    Ui ui; auto field = addRichField(ui, "note", u"hello world");
    check(bool(ui.openContextMenu(field.handle)), "a rich field opens its built-in menu");
    auto menu = ui.contextMenu();
    check(idsOf(*menu) == richLayout, "the built-in items, a line, then bold, italic and underline");
    check(findItem(*menu, "lcars.bold")->label == u"粗體(B)" && findItem(*menu, "lcars.italic")->label == u"斜體(I)" &&
          findItem(*menu, "lcars.underline")->label == u"底線(U)", "labels follow the Windows rich-edit menu");
    check(findItem(*menu, "lcars.bold")->accessKey == u'B' && findItem(*menu, "lcars.italic")->accessKey == u'I' &&
          findItem(*menu, "lcars.underline")->accessKey == u'U', "quick keys match the letters in the labels");
    check(findItem(*menu, "lcars.bold")->shortcutHint == u"Ctrl+B" && findItem(*menu, "lcars.italic")->shortcutHint == u"Ctrl+I" &&
          findItem(*menu, "lcars.underline")->shortcutHint == u"Ctrl+U", "shortcut hints are shown");
    check(enabled(ui, "lcars.bold") && enabled(ui, "lcars.italic") && enabled(ui, "lcars.underline"), "on in an editable rich field, with or without a selection");
    ui.closeContextMenu();
    // Only rich fields have them; a plain field keeps the old layout, and a read-only rich field disables them.
    auto plain = addField(ui, "plain", u"hello");
    ui.openContextMenu(plain.handle);
    check(idsOf(*ui.contextMenu()) == builtinLayout, "a plain field has no style items");
    ui.closeContextMenu();
    auto locked = addRichField(ui, "locked", u"hello", true);
    ui.openContextMenu(locked.handle);
    check(idsOf(*ui.contextMenu()) == richLayout && !enabled(ui, "lcars.bold") && !enabled(ui, "lcars.italic") && !enabled(ui, "lcars.underline"),
          "a read-only rich field shows them but refuses every one");
    ui.closeContextMenu();
    // The application's own switches still reach it: replace drops the built-ins, style items included.
    auto handle = ui.addContextMenu(field.handle, {MenuItem{.id = "mine", .label = u"我的(M)", .accessKey = u'M'}}, MenuMode::replace).value.value();
    ui.openContextMenu(field.handle);
    check(idsOf(*ui.contextMenu()) == std::vector<std::string>{"mine"}, "MenuMode::replace leaves no style items");
    (void)handle;
}

// Bold, italic and underline run the very function Ctrl+B / Ctrl+I / Ctrl+U run, on a selection and at a caret.
void styleItemsMatchTheirShortcuts() {
    struct Case { const char* item; char16_t letter; bool selection; };
    const Case cases[] = {
        {"lcars.bold", u'B', true}, {"lcars.italic", u'I', true}, {"lcars.underline", u'U', true},
        {"lcars.bold", u'B', false}, {"lcars.italic", u'I', false}, {"lcars.underline", u'U', false},
    };
    for (const auto& c : cases) {
        Ui byMenu, byKey;
        auto a = addRichField(byMenu, "f", u"hello world"), b = addRichField(byKey, "f", u"hello world");
        if (c.selection) { a.handle.select(0, 5); b.handle.select(0, 5); } else { a.handle.select(3, 3); b.handle.select(3, 3); }
        b.handle.focus();
        check(bool(byMenu.openContextMenu(a.handle)), "open for the menu twin");
        check(bool(byMenu.invokeMenuItem(c.item)), "run the menu item");
        check(bool(core(byKey).keyDown({.ctrl = true, .letter = c.letter})), "run the shortcut");
        check(*a.model == *b.model, "the menu item and its shortcut leave the same rich value");
        check(a.handle.richValue().value.value() == b.handle.richValue().value.value(), "same spans");
        check(a.handle.typingStyle().value.value() == b.handle.typingStyle().value.value(), "the same typing style at a caret");
        if (c.selection) {
            const auto spans = a.handle.richValue().value.value().spans;
            check(spans.size() == 1 && spans[0].start == 0 && spans[0].length == 5 &&
                  (c.letter == u'B' ? spans[0].bold : c.letter == u'I' ? spans[0].italic : spans[0].underline), "the selection takes the style");
        } else {
            const auto typing = a.handle.typingStyle().value.value();
            check(c.letter == u'B' ? typing.bold : c.letter == u'I' ? typing.italic : typing.underline, "a caret starts the style for the next typed text");
        }
        check(a.handle.undo().status == b.handle.undo().status && *a.model == *b.model, "undo behaves the same afterwards");
        // Asking again takes the style away again, through the menu as through the key.
        byMenu.openContextMenu(a.handle);
        byMenu.invokeMenuItem(c.item); core(byKey).keyDown({.ctrl = true, .letter = c.letter});
        check(a.handle.richValue().value.value() == b.handle.richValue().value.value(), "a second toggle agrees too");
    }
}

// U is both 復原(U) and 底線(U): the quick key cycles between them without running either, and Enter runs the highlighted one.
void underlineSharesItsQuickKeyWithUndo() {
    Ui ui; auto field = addRichField(ui, "note", u"hello world");
    field.handle.select(0, 5); field.handle.focus();
    check(bool(type(ui, u'x')), "type over the selection, so that undo has something to undo");
    field.handle.select(0, 1);
    press(ui, Key::menu);
    check(enabled(ui, "lcars.undo") && enabled(ui, "lcars.underline"), "both items that share U are on");
    type(ui, u'U');
    check(*ui.contextMenuOpen() && highlighted(ui) == "lcars.undo", "U jumps to the first match, 復原(U), without running it");
    check(field.handle.richValue().value.value().spans.empty(), "nothing was run by the quick key");
    type(ui, u'u');
    check(highlighted(ui) == "lcars.underline" && *ui.contextMenuOpen(), "pressed again it goes to 底線(U), still without running");
    type(ui, u'U');
    check(highlighted(ui) == "lcars.undo", "and wraps around");
    type(ui, u'u');
    check(bool(press(ui, Key::enter)) && !*ui.contextMenuOpen(), "Enter runs the highlighted 底線(U)");
    const auto spans = field.handle.richValue().value.value().spans;
    check(spans.size() == 1 && spans[0].start == 0 && spans[0].length == 1 && spans[0].underline, "the selected character is underlined");
    // B and I are unique letters: they run at once.
    press(ui, Key::menu);
    check(bool(type(ui, u'b')) && !*ui.contextMenuOpen(), "B runs 粗體(B) at once");
    check(field.handle.richValue().value.value().spans.size() == 1 && field.handle.richValue().value.value().spans[0].bold, "the character is now bold too");
    press(ui, Key::menu);
    check(bool(type(ui, u'I')) && !*ui.contextMenuOpen() && field.handle.richValue().value.value().spans[0].italic, "I runs 斜體(I) at once");
}

void keyboardMovesSkipsLinesAndWraps() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard); clipboard.content = u"x";
    auto field = addField(ui, "note", u"hello");
    field.handle.focus();
    check(bool(press(ui, Key::menu)) && *ui.contextMenuOpen(), "the menu key opens the menu");
    check(highlighted(ui).empty(), "nothing is highlighted at first");
    press(ui, Key::down); check(highlighted(ui) == "lcars.undo", "Down enters at the first item");
    press(ui, Key::down); press(ui, Key::down);
    check(highlighted(ui) == "lcars.cut", "Down skips the separator");
    press(ui, Key::up); check(highlighted(ui) == "lcars.redo", "Up skips the separator");
    press(ui, Key::up); press(ui, Key::up);
    check(highlighted(ui) == "lcars.select_all", "Up from the first item wraps to the last");
    press(ui, Key::down); check(highlighted(ui) == "lcars.undo", "Down from the last item wraps to the first");
    press(ui, Key::end); check(highlighted(ui) == "lcars.select_all", "End goes to the last item");
    press(ui, Key::home); check(highlighted(ui) == "lcars.undo", "Home goes to the first item");
    check(press(ui, Key::enter).status == Status::disabled && *ui.contextMenuOpen(), "a disabled item can be reached but not run, and the menu stays");
    press(ui, Key::end);
    check(bool(press(ui, Key::space)) && !*ui.contextMenuOpen(), "Space runs the highlighted item and closes the menu");
    check(core(ui).describe("note")->value == u"hello" && field.handle.describe()->focused, "select all ran; the field kept focus");
    check(core(ui).focused() == "note", "focus is on the field again");
    press(ui, Key::menu);
    press(ui, Key::end); press(ui, Key::up);
    check(highlighted(ui) == "lcars.delete", "Up from the last item lands before the separator");
    // Code review 2026-11-12 item 13: a standard Win32 popup menu ignores the Apps key while open (probed with TrackPopupMenu: F10 and
    // Alt close it, VK_APPS does not), and the spec's close list is Esc / Alt / F10 / click outside / deactivation.
    press(ui, Key::menu);
    check(*ui.contextMenuOpen() && highlighted(ui) == "lcars.delete", "the menu key again leaves the open menu and its highlight alone");
    press(ui, Key::escape);
    check(!*ui.contextMenuOpen(), "Escape still closes it");
    core(ui).keyDown({.key = Key::f10, .shift = true});
    check(*ui.contextMenuOpen(), "Shift+F10 opens the menu");
    core(ui).keyDown({.key = Key::enter});
    check(*ui.contextMenuOpen(), "Enter with nothing highlighted does nothing");
}

void quickKeysRunUniqueAndCycleSharedLetters() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard);
    auto field = addField(ui, "note", u"hello world");
    field.handle.select(0, 5); field.handle.focus();
    press(ui, Key::menu);
    check(bool(type(ui, u'c')) && !*ui.contextMenuOpen() && clipboard.content == std::u16string(u"hello"), "a unique quick key runs its item at once (lower case)");
    press(ui, Key::menu);
    check(bool(type(ui, u'A')) && !*ui.contextMenuOpen(), "upper case works the same");
    auto button = ui.addButton({.id = "b", .label = u"B"}).value.value();
    int runs = 0;
    auto menu = ui.addContextMenu(button, {
        {.id = "underline", .label = u"底線(U)", .accessKey = u'U', .action = [&](const MenuContext&) { ++runs; return Result{}; }},
        {.id = "uppercase", .label = u"大寫(U)", .accessKey = u'U', .action = [&](const MenuContext&) { runs += 10; return Result{}; }},
        {.id = "other", .label = u"其他(O)", .accessKey = u'O'},
    }).value.value();
    button.focus(); press(ui, Key::menu);
    type(ui, u'U');
    check(*ui.contextMenuOpen() && highlighted(ui) == "underline" && runs == 0, "a shared letter jumps to the first match without running");
    type(ui, u'u');
    check(highlighted(ui) == "uppercase" && runs == 0, "pressed again it goes to the next match");
    type(ui, u'U');
    check(highlighted(ui) == "underline", "and wraps around");
    type(ui, u'z');
    check(highlighted(ui) == "underline" && *ui.contextMenuOpen(), "an unused letter does nothing");
    type(ui, u'O');
    check(!*ui.contextMenuOpen() && runs == 0, "a unique letter runs its item");
    // Letters never reach the field while a menu is open.
    field.handle.focus(); press(ui, Key::menu); type(ui, u'q');
    check(core(ui).describe("note")->value == u"hello world", "a letter is a quick key, not text");
    (void)menu;
}

MenuItem command(std::string id, std::u16string label, char16_t key, int* runs = nullptr) {
    return {.id = std::move(id), .label = std::move(label), .accessKey = key,
            .action = [runs](const MenuContext&) { if (runs) ++*runs; return Result{}; }};
}

void attachModesAppendReplaceAndOff() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard);
    auto field = addField(ui, "note", u"hello");
    auto menu = ui.addContextMenu(field.handle, {command("app.one", u"一(1)", u'1'), MenuItem::divider(), command("app.two", u"二(2)", u'2')}).value.value();
    ui.openContextMenu(field.handle);
    auto appended = builtinLayout; appended.insert(appended.end(), {"-", "app.one", "-", "app.two"});
    check(idsOf(*ui.contextMenu()) == appended, "append (the default): built-in items, a separator, then the application's");
    ui.closeContextMenu();
    check(bool(menu.setMode(MenuMode::replace)), "switch to replace");
    ui.openContextMenu(field.handle);
    check(idsOf(*ui.contextMenu()) == std::vector<std::string>({"app.one", "-", "app.two"}), "replace: only the application's items");
    ui.closeContextMenu();
    check(bool(menu.setMode(MenuMode::off)), "switch to off");
    check(ui.openContextMenu(field.handle).status == Status::notSupported, "off: this field has no menu");
    check(!hasVerb(ui, "note", Verb::contextMenu) && hasVerb(ui, "note", Verb::focus), "off: the semantic layer does not offer the menu either");
    check(bool(menu.setMode(MenuMode::append)), "back to append");
    check(bool(menu.setItems({})), "no application items");
    ui.openContextMenu(field.handle);
    check(idsOf(*ui.contextMenu()) == builtinLayout, "append with nothing to add shows only the built-in items, with no stray separator");
    ui.closeContextMenu();
    check(bool(menu.setItems({command("only", u"唯一(O)", u'O')})) && bool(menu.setMode(MenuMode::replace)), "replace with one item");
    ui.openContextMenu(field.handle);
    check(idsOf(*ui.contextMenu()) == std::vector<std::string>({"only"}), "replace with one item");
    ui.closeContextMenu();
    // Separators at the ends or doubled are not shown.
    menu.setItems({MenuItem::divider(), command("a", u"甲", 0), MenuItem::divider(), MenuItem::divider(), command("b", u"乙", 0), MenuItem::divider()});
    ui.openContextMenu(field.handle);
    check(idsOf(*ui.contextMenu()) == std::vector<std::string>({"a", "-", "b"}), "leading, trailing and repeated separators collapse");
}

void otherComponentsHaveNoMenuUntilOneIsAttached() {
    Ui ui;
    auto button = ui.addButton({.id = "b", .label = u"按鈕"}).value.value();
    check(ui.openContextMenu(button).status == Status::notSupported, "a button has no menu by default");
    check(!hasVerb(ui, "b", Verb::contextMenu), "and the semantic layer does not offer one");
    check(core(ui).menuPointerOpen("b", std::nullopt, std::nullopt, {5, 5}).status == Status::notSupported && !*ui.contextMenuOpen(), "right button on it does nothing");
    int runs = 0;
    auto menu = ui.addContextMenu(button, {command("go", u"執行(G)", u'G', &runs)});
    check(bool(menu), "attach a menu to a button");
    check(ui.addContextMenu(button, {}).status == Status::invalidValue, "a component carries one attachment");
    check(bool(ui.openContextMenu(button)) && idsOf(*ui.contextMenu()) == std::vector<std::string>({"go"}), "the button opens it");
    check(bool(type(ui, u'g')) && runs == 1 && !*ui.contextMenuOpen(), "its quick key runs the application's code");
    check(core(ui).focused() == "b", "the button was focused by opening its menu");
    auto bad = ui.addButton({.id = "c"}).value.value();
    check(ui.addContextMenu(bad, {command("x", u"x", 0), command("x", u"y", 0)}).status == Status::invalidValue, "duplicate ids are refused");
    check(ui.addContextMenu(bad, {command("", u"x", 0)}).status == Status::invalidValue, "an item needs an id");
    check(ui.addContextMenu(bad, {command("lcars.cut", u"x", 0)}).status == Status::invalidValue, "the isoduo. prefix is reserved");
    check(bool(ui.addContextMenu(bad, {})) , "an empty attachment is allowed (it is not a menu yet)");
    check(ui.openContextMenu(bad).status == Status::notSupported, "and opens nothing");
}

void menuClosesByEscapeAltF10AndDeactivation() {
    Ui ui; auto field = addField(ui, "note", u"hello");
    core(ui).windowActive(true);
    struct Close { const char* name; std::function<void()> close; };
    const Close closers[] = {
        {"Escape", [&] { press(ui, Key::escape); }},
        {"Alt", [&] { press(ui, Key::alt); }},
        {"F10", [&] { press(ui, Key::f10); }},
        {"the window losing activation", [&] { core(ui).windowActive(false); core(ui).windowActive(true); }},
        {"close_context_menu", [&] { ui.closeContextMenu(); }},
    };
    for (const auto& c : closers) {
        field.handle.focus();
        check(bool(ui.openContextMenu(field.handle)) && *ui.contextMenuOpen(), "open before closing");
        c.close();
        check(!*ui.contextMenuOpen(), c.name);
        check(core(ui).focused() == "note" && field.handle.describe()->focused, "focus is on the field after closing");
    }
    check(*field.model == u"hello", "closing changed nothing");
    // The open menu swallows keys meant for the field.
    ui.openContextMenu(field.handle);
    core(ui).keyDown({.ctrl = true, .letter = u'A'});
    core(ui).keyDown({.key = Key::backspace});
    core(ui).keyDown({.key = Key::left});
    check(*field.model == u"hello" && *ui.contextMenuOpen(), "other keys do nothing while the menu is open");
}

// Code review 2026-11-12 item 16: every way of closing a menu is announced (MenuClosed in UIA is built on this event), and
// every opening is announced, not only the first one.
void everyWayOfClosingAnnouncesMenuClosed() {
    std::vector<std::pair<std::string, std::function<void(Ui&, Field&)>>> closers = {
        {"Escape", [](Ui& ui, Field&) { press(ui, Key::escape); }},
        {"Alt", [](Ui& ui, Field&) { press(ui, Key::alt); }},
        {"F10", [](Ui& ui, Field&) { press(ui, Key::f10); }},
        {"the window losing activation", [](Ui& ui, Field&) { core(ui).windowActive(false); core(ui).windowActive(true); }},
        {"close_context_menu", [](Ui& ui, Field&) { ui.closeContextMenu(); }},
        {"the semantic cancel verb", [](Ui& ui, Field&) { ui.semantics().perform({menuNodeId("note")}, Verb::cancel); }},
    };
    for (const auto& [name, close] : closers) {
        Ui ui; auto field = addField(ui, "note", u"hello");
        core(ui).windowActive(true);
        std::vector<SemanticEventKind> kinds;
        auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { kinds.push_back(e.kind); });
        auto count = [&](SemanticEventKind kind) { return std::count(kinds.begin(), kinds.end(), kind); };
        for (int round = 1; round <= 2; ++round) {  // the second opening is announced like the first
            field.handle.focus();
            check(bool(ui.openContextMenu(field.handle)) && *ui.contextMenuOpen(), "open");
            ui.semantics().dispatchEvents();
            check(count(SemanticEventKind::menuOpened) == round, (name + ": opening number " + std::to_string(round) + " is announced").c_str());
            if (round == 1) {
                close(ui, field);
                ui.semantics().dispatchEvents();
                check(!*ui.contextMenuOpen() && count(SemanticEventKind::menuClosed) == 1, (name + ": closing is announced once").c_str());
            } else {
                press(ui, Key::escape);
                ui.semantics().dispatchEvents();
                check(count(SemanticEventKind::menuClosed) == 2, (name + ": the second menu closes with its own announcement").c_str());
            }
        }
    }
}

// Removing the component a menu hangs from closes the menu, and that is announced too.
void removingTheComponentAnnouncesMenuClosed() {
    Ui ui;
    auto button = ui.addButton({.id = "b"}).value.value();
    ui.addContextMenu(button, {command("go", u"執行(G)", u'G')});
    std::vector<SemanticEventKind> kinds;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { kinds.push_back(e.kind); });
    check(bool(ui.openContextMenu(button)), "open the button's menu");
    ui.semantics().dispatchEvents();
    check(std::count(kinds.begin(), kinds.end(), SemanticEventKind::menuOpened) == 1, "opening is announced");
    check(bool(button.remove()) && !*ui.contextMenuOpen(), "removing the component closes the menu");
    ui.semantics().dispatchEvents();
    check(std::count(kinds.begin(), kinds.end(), SemanticEventKind::menuClosed) == 1, "and the closing is announced once");
}

void menuCannotOpenOrSurviveWhenTheComponentCannotAct() {
    Ui ui; auto field = addField(ui, "note", u"hello");
    field.handle.enable(false);
    check(ui.openContextMenu(field.handle).status == Status::disabled, "a disabled component opens no menu");
    check(core(ui).menuPointerOpen("note", std::nullopt, 0, {1, 1}).status == Status::disabled && !*ui.contextMenuOpen(), "nor does the right button on it");
    field.handle.enable(true);
    ui.openContextMenu(field.handle);
    field.handle.enable(false);
    check(!*ui.contextMenuOpen(), "disabling the component closes its open menu at once");
    field.handle.enable(true);
    auto button = ui.addButton({.id = "b"}).value.value();
    ui.addContextMenu(button, {command("go", u"執行(G)", u'G')});
    ui.openContextMenu(button);
    check(bool(button.remove()) && !*ui.contextMenuOpen(), "removing the component closes its open menu");
    check(ui.openContextMenu(button).status == Status::notFound, "a removed component cannot open one");
    // A modal dialog blocks the background.
    auto cancel = ui.addButton({.id = "cancel"}).value.value();
    auto fallback = ui.addButton({.id = "fallback"}).value.value();
    auto inner = addField(ui, "inner", u"x");
    auto dialog = ui.addDialog("d", {inner.handle, cancel}, inner.handle, fallback, [](bool) {}).value.value();
    ui.openContextMenu(field.handle);
    check(bool(dialog.open()) && !*ui.contextMenuOpen(), "opening a dialog closes an open menu");
    check(ui.openContextMenu(field.handle).status == Status::blockedByModal, "a background component refuses while a dialog is open");
    check(core(ui).menuPointerOpen("note", std::nullopt, 0, {1, 1}).status == Status::blockedByModal && !*ui.contextMenuOpen(), "right button on the background does nothing");
    check(ui.semantics().perform({"note"}, Verb::contextMenu).status == Status::blockedByModal, "the semantic layer gives the same status");
    check(bool(ui.openContextMenu(inner.handle)) && *ui.contextMenuOpen(), "a field inside the dialog opens its menu");
    check(bool(ui.closeContextMenu()) && bool(dialog.settle(false)), "close");
    core(ui).rendererFailed(true);
    check(ui.openContextMenu(field.handle).status == Status::rendererFailed, "a failed renderer opens no menu");
}

void listMenuSelectsTheRowAndHandsItToTheApplication() {
    Ui ui;
    auto selected = std::make_shared<std::optional<OptionValue>>();
    auto list = ui.addList({.id = "items", .label = u"項目"}, {{{"a"}, u"甲"}, {{"b"}, u"乙"}, {{"c"}, u"丙", false}}).value.value();
    check(bool(list.connectSource({[selected] { return *selected; }, [](std::function<void()>) { return [] {}; }})), "connect list source");
    check(bool(list.connectImmediate([selected](const Change<ListHandle>& change) { *selected = change.value; return Result{}; })), "connect list write");
    std::optional<OptionValue> got; int runs = 0;
    auto menu = ui.addContextMenu(list, {
        {.id = "edit", .label = u"編輯(E)", .accessKey = u'E', .shortcutHint = u"Enter",
         .action = [&](const MenuContext& c) { ++runs; got = c.item; return Result{}; }}}).value.value();
    check(bool(core(ui).menuPointerOpen("items", 1, std::nullopt, {10, 10})) && *ui.contextMenuOpen(), "right button on a row opens the menu");
    check(selected->has_value() && **selected == OptionValue{"b"}, "the row was selected first");
    check(ui.contextMenu()->item && *ui.contextMenu()->item == OptionValue{"b"}, "the menu knows its row");
    check(bool(ui.invokeMenuItem("edit")) && runs == 1 && got && *got == OptionValue{"b"}, "the application's code receives that row");
    check(core(ui).menuPointerOpen("items", 2, std::nullopt, {10, 10}).status == Status::disabled && !*ui.contextMenuOpen() && **selected == OptionValue{"b"},
          "a disabled row opens nothing and keeps the selection");
    check(bool(core(ui).menuPointerOpen("items", std::nullopt, std::nullopt, {10, 10})) && !ui.contextMenu()->item && **selected == OptionValue{"b"},
          "empty space opens the menu with no row and keeps the selection");
    ui.closeContextMenu();
    *selected = std::nullopt; got.reset();
    list.focus();
    check(bool(press(ui, Key::menu)) && ui.contextMenu()->item && *ui.contextMenu()->item == OptionValue{"b"} && !selected->has_value(),
          "the menu key hands over the list's current row without selecting it");
    ui.invokeMenuItem("edit");
    check(got && *got == OptionValue{"b"}, "the application's code received the current row");
    check(bool(ui.openContextMenu(list, MenuPosition{3, 4}, OptionValue{"b"})) && **selected == OptionValue{"b"}, "the typed call can name the row");
    check(ui.openContextMenu(list, MenuPosition{3, 4}, OptionValue{"zz"}).status == Status::notFound, "an unknown row is refused");
    check(ui.openContextMenu(list, MenuPosition{3, 4}, OptionValue{"c"}).status == Status::disabled, "a disabled row is refused");
    (void)menu;
}

void itemsCanChangeWhileTheMenuIsOpen() {
    Ui ui; auto field = addField(ui, "note", u"hello");
    int runs = 0;
    auto menu = ui.addContextMenu(field.handle, {command("one", u"一", 0, &runs), command("two", u"二", 0, &runs)}).value.value();
    ui.openContextMenu(field.handle);
    core(ui).menuHighlight("two");
    check(bool(menu.enableItem("two", false)) && !enabled(ui, "two") && highlighted(ui) == "two", "enabling and disabling updates the open menu");
    check(ui.invokeMenuItem("two").status == Status::disabled && *ui.contextMenuOpen(), "a disabled item does not run and the menu stays open");
    check(bool(menu.addItem(command("zero", u"零", 0), "one")), "add before an item");
    check(idsOf(*ui.contextMenu()).end()[-3] == "zero" && idsOf(*ui.contextMenu()).end()[-2] == "one", "the new item appears in place");
    check(menu.addItem(command("one", u"重複", 0)).status == Status::invalidValue, "a duplicate id is refused");
    check(bool(menu.updateItem({.id = "one", .label = u"新的一", .action = [&](const MenuContext&) { runs += 100; return Result{}; }})) &&
          findItem(*ui.contextMenu(), "one")->label == u"新的一", "update replaces an item");
    check(bool(menu.removeItem("two")) && highlighted(ui).empty(), "removing the highlighted item clears the highlight");
    check(menu.removeItem("two").status == Status::notFound, "removing it twice says so");
    check(bool(ui.invokeMenuItem("one")) && runs == 100, "the updated action runs");
    check(menu.items()->size() == 2 && menu.items()->front().id == "zero", "items() lists the application's own items");
    ui.openContextMenu(field.handle);
    check(bool(menu.setMode(MenuMode::off)) && !*ui.contextMenuOpen(), "turning the menu off closes it");
    menu.setMode(MenuMode::append);
    ui.openContextMenu(field.handle);
    check(ui.invokeMenuItem("separator:2").status == Status::invalidValue, "a separator cannot run");
    check(ui.invokeMenuItem("nothing").status == Status::notFound, "an unknown id is not found");
    // An action may rewrite the menu; the next opening sees it.
    menu.updateItem({.id = "one", .label = u"一", .action = [&](const MenuContext&) { return menu.removeItem("zero"); }});
    check(bool(ui.invokeMenuItem("one")), "an action can edit the menu");
    ui.openContextMenu(field.handle);
    check(findItem(*ui.contextMenu(), "zero") == nullptr, "and the edit is in effect");
    ui.closeContextMenu();
    // Failure from the application's code is the item's result, and the menu is closed regardless.
    menu.updateItem({.id = "one", .label = u"一", .action = [](const MenuContext&) { return Result{Status::invalidValue, u"nope"}; }});
    ui.openContextMenu(field.handle);
    check(ui.invokeMenuItem("one").status == Status::invalidValue && !*ui.contextMenuOpen(), "the application's result is returned and the menu closes");
}

Frame layoutOf(Ui& ui, int width, int height, ScrollState& scroll, float scale = 1.f) {
    test::FakeMeasure measure;
    std::vector<layout::Node> nodes;
    for (const auto& control : core(ui).controls())
        if (control.kind != ComponentKind::label && control.page != "dialog") nodes.push_back(layout::component(core(ui).layoutHandle(control.id)));
    return arrange(layout::column(std::move(nodes), 8), ui, webAppearance(), measure, width, height, scale, scroll);
}

void menuPanelSitsBesideThePointerFlipsAndScrolls() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard);
    auto field = addField(ui, "note", u"hello world");
    ScrollState scroll;
    auto frame = layoutOf(ui, 600, 400, scroll);
    check(frame.menuPanel() == nullptr, "no menu, no panel");
    check(bool(ui.openContextMenu(field.handle, MenuPosition{100, 60})), "open at a point");
    frame = layoutOf(ui, 600, 400, scroll);
    const auto* panel = frame.menuPanel();
    check(panel && panel->bounds.x == 100 && panel->bounds.y == 60, "the panel's corner is at the pointer");
    check(panel->menuRows.size() == 9 && panel->menuRows[2].separator && !panel->menuRows[0].separator, "one row per item, lines included");
    check(panel->menuMaximum == 0 && inside(panel->bounds, {0, 0, 600, 400}), "it fits, so nothing scrolls");
    check(frame.hit(110, 70) != frame.menuPanel() && frame.menuContains(110, 70) && frame.menuItemAt(110, 70) == "lcars.undo",
          "a point on the first row hits the first item (and the page's hit test never returns the menu)");
    check(!frame.menuContains(99, 59) && !frame.menuItemAt(99, 59), "just outside is outside");
    const auto size = panel->bounds;
    ui.closeContextMenu();
    ui.openContextMenu(field.handle, MenuPosition{590, 390});
    frame = layoutOf(ui, 600, 400, scroll);
    panel = frame.menuPanel();
    check(panel && panel->bounds.x + panel->bounds.w == 590 && panel->bounds.y + panel->bounds.h == 390 && panel->bounds.w == size.w && panel->bounds.h == size.h,
          "at the bottom right corner it opens up and to the left, the same size");
    ui.closeContextMenu();
    ui.openContextMenu(field.handle, MenuPosition{590, 50});
    frame = layoutOf(ui, 600, 400, scroll);
    panel = frame.menuPanel();
    check(panel && panel->bounds.x + panel->bounds.w == 590 && panel->bounds.y == 50, "near the right edge only: left, still downwards");
    ui.closeContextMenu();
    ui.openContextMenu(field.handle, MenuPosition{20, 395});
    frame = layoutOf(ui, 600, 400, scroll);
    panel = frame.menuPanel();
    check(panel && panel->bounds.x == 20 && panel->bounds.y + panel->bounds.h == 395, "near the bottom only: up, still rightwards");
    // A window too small for the whole menu: the menu scrolls inside the window.
    ui.closeContextMenu();
    ui.openContextMenu(field.handle, MenuPosition{30, 40});
    frame = layoutOf(ui, 300, 120, scroll);
    panel = frame.menuPanel();
    check(panel && inside(panel->bounds, {0, 0, 300, 120}) && panel->menuMaximum > 0 && panel->menuThumb.h > 0, "a small window gets a scrolling menu inside it");
    const int firstRowY = panel->menuRows.front().bounds.y;
    check(frame.wheel(ui, panel->bounds.x + 5, panel->bounds.y + 5, -40), "the wheel scrolls the menu");
    frame = layoutOf(ui, 300, 120, scroll);
    check(frame.menuPanel()->menuRows.front().bounds.y < firstRowY, "the rows moved up");
    press(ui, Key::end);
    frame = layoutOf(ui, 300, 120, scroll);
    panel = frame.menuPanel();
    const auto& last = panel->menuRows.back();
    check(last.highlighted && inside(last.bounds, panel->menuInner), "keyboard to the end reveals the last item");
    press(ui, Key::home);
    frame = layoutOf(ui, 300, 120, scroll);
    check(frame.menuPanel()->menuRows.front().highlighted && inside(frame.menuPanel()->menuRows.front().bounds, frame.menuPanel()->menuInner), "and Home brings back the first");
}

void keyboardPositionFollowsTheCaretAndTheCurrentRow() {
    Ui ui;
    auto field = addField(ui, "note", u"hello world");
    auto list = ui.addList({.id = "items", .label = u"項目"}, {{{"a"}, u"甲"}, {{"b"}, u"乙"}, {{"c"}, u"丙"}}).value.value();
    list.connectSource({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    ScrollState scroll;
    field.handle.select(5, 5); field.handle.focus();
    auto frame = layoutOf(ui, 600, 400, scroll);
    const auto* item = frame.find(field.handle);
    check(bool(press(ui, Key::menu)), "open from the keyboard in a text field");
    frame = layoutOf(ui, 600, 400, scroll);
    const auto* panel = frame.menuPanel();
    item = frame.find(field.handle);
    const int caret = item->caretX(5), lineBottom = item->bounds.y + item->textField.editor.y + item->textField.editor.h;
    check(panel && panel->bounds.x == caret && panel->bounds.y == lineBottom, "the menu hangs from the caret, under its line");
    ui.closeContextMenu();
    field.handle.select(0, 0);
    ui.openContextMenu(field.handle);
    frame = layoutOf(ui, 600, 400, scroll);
    check(frame.menuPanel()->bounds.x == frame.find(field.handle)->caretX(0), "and follows a moved caret");
    ui.closeContextMenu();
    list.focus(); press(ui, Key::down);  // the cursor moves to the second row
    check(press(ui, Key::menu).status == Status::notSupported, "a list with no attachment has no menu to open");
    frame = layoutOf(ui, 600, 400, scroll);
    const auto* rows = frame.find(list);
    panel = frame.menuPanel();
    check(panel == nullptr, "and no panel appears");
    check(ui.addContextMenu(list, {command("x", u"x(X)", u'X')}).status == Status::ok, "attach");
    check(bool(press(ui, Key::menu)), "open again");
    frame = layoutOf(ui, 600, 400, scroll);
    rows = frame.find(list); panel = frame.menuPanel();
    check(panel && rows && ui.contextMenu()->item && panel->bounds.y == rows->options[1].y && panel->bounds.x >= rows->options[1].x,
          "the menu hangs from the list's current row");
}

// A multi-line field (#79) has the same built-in menu, and a keyboard-opened one hangs from the caret's own line.
void multiLineFieldMenuHangsFromTheCaretLine() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard); clipboard.content = u"x\ny";
    auto model = std::make_shared<std::u16string>(u"first\nsecond\nthird");
    auto note = ui.addTextField({.id = "note", .label = u"說明", .value = *model, .multiline = true, .visibleLines = 3}).value.value();
    check(bool(note.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})) &&
          bool(note.connectImmediate([model](const Change<TextFieldHandle>& change) { *model = change.value; return Result{}; })), "connect the multi-line field");
    ScrollState scroll;
    note.select(8, 8); note.focus();
    auto frame = layoutOf(ui, 600, 400, scroll);
    check(bool(press(ui, Key::menu)), "the menu key opens the menu in a multi-line field");
    check(idsOf(*ui.contextMenu()) == builtinLayout && enabled(ui, "lcars.paste"), "with the same built-in items");
    frame = layoutOf(ui, 600, 400, scroll);
    const auto* field = frame.find(note);
    const auto caret = field->caretRect(8);
    const auto* panel = frame.menuPanel();
    check(panel && caret.y > field->view.text.y && panel->bounds.x == caret.x && panel->bounds.y == caret.y + caret.h, "it hangs from the second line's caret, under that line");
    ui.closeContextMenu();
    check(bool(core(ui).menuPointerOpen("note", std::nullopt, 2, {30, 30})) && core(ui).edit("note")->selection.caret == 2, "right button moves the caret to the point");
    ui.invokeMenuItem("lcars.paste");
    check(*model == u"fix\nyrst\nsecond\nthird", "paste inserts the clipboard text with its line break at the caret");
    // Copy and cut over a selection that spans lines: the same functions as the shortcuts, so the clipboard gets CRLF (#79).
    core(ui).select("note", 0, 5);   // "fix", the line break and "y"
    check(bool(core(ui).menuPointerOpen("note", std::nullopt, std::nullopt, {30, 30})) && enabled(ui, "lcars.copy") && enabled(ui, "lcars.cut"), "copy and cut are on with a multi-line selection");
    check(bool(ui.invokeMenuItem("lcars.copy")) && clipboard.content == u"fix\r\ny" && *model == u"fix\nyrst\nsecond\nthird", "copy writes CRLF and leaves the text");
    core(ui).select("note", 0, 5);
    check(bool(core(ui).menuPointerOpen("note", std::nullopt, std::nullopt, {30, 30})) && bool(ui.invokeMenuItem("lcars.cut")) && clipboard.content == u"fix\r\ny" && *model == u"rst\nsecond\nthird",
          "cut writes CRLF and removes the text across the line break");
    check(bool(core(ui).menuPointerOpen("note", std::nullopt, std::nullopt, {30, 30})) && bool(ui.invokeMenuItem("lcars.paste")) && *model == u"fix\nyrst\nsecond\nthird",
          "pasting the CRLF clipboard back gives LF line breaks again");
}

void semanticTreeShowsMenuAndItemsWhileOpen() {
    Ui ui; MemoryClipboard clipboard; core(ui).setClipboard(&clipboard);
    auto field = addField(ui, "note", u"hello");
    auto& tree = ui.semantics();
    const auto menuId = menuNodeId("note");
    {
        auto before = tree.children({"note"});
        check(!tree.get({menuId}) && std::find(before->begin(), before->end(), NodeRef{menuId}) == before->end(), "no menu node while closed");
    }
    check(hasVerb(ui, "note", Verb::contextMenu), "a text field offers the context_menu verb");
    check(bool(tree.perform({"note"}, Verb::contextMenu)) && *ui.contextMenuOpen(), "perform(context_menu) opens the menu");
    auto menu = tree.get({menuId});
    check(menu && menu->role == Role::menu && !menu->hidden && menu->enabled, "a menu node appears");
    auto children = tree.children({"note"});
    check(std::find(children->begin(), children->end(), NodeRef{menuId}) != children->end(), "as a child of the component");
    auto items = tree.children({menuId});
    check(items && items->size() == 9 && items->front() == NodeRef{menuId, OptionValue{"lcars.undo"}}, "with one child per item");
    auto copy = tree.get({menuId, OptionValue{"lcars.copy"}});
    check(copy && copy->role == Role::menuItem && copy->name == u"複製(C)" && copy->accessKey == u"C" && copy->acceleratorKey == u"Ctrl+C" && !copy->enabled,
          "a disabled item: role, name with its letter, AccessKey, accelerator hint, not enabled");
    check(std::find(copy->verbs.begin(), copy->verbs.end(), Verb::invoke) == copy->verbs.end() && tree.perform(copy->ref, Verb::invoke).status == Status::disabled,
          "it offers no invoke and refuses one");
    auto line = tree.get({menuId, OptionValue{"separator:2"}});
    check(line && line->role == Role::menuSeparator && !line->focusable, "a separator node");
    check(tree.perform({menuId, OptionValue{"lcars.select_all"}}, Verb::focus).status == Status::ok && highlighted(ui) == "lcars.select_all", "focus on an item highlights it");
    check(tree.get({menuId, OptionValue{"lcars.select_all"}})->focused && !tree.get({"note"})->focused, "keyboard focus belongs to the item, not the field");
    check(bool(tree.perform({menuId, OptionValue{"lcars.select_all"}}, Verb::invoke)) && !*ui.contextMenuOpen() && !tree.get({menuId}), "invoke runs the item and the nodes vanish");
    check(tree.get({"note"})->focused && tree.get({"note"})->textSelection->caret == 5 && tree.get({"note"})->textSelection->anchor == 0, "focus is back on the field, with everything selected");
    tree.perform({"note"}, Verb::contextMenu);
    check(bool(tree.perform({menuId}, Verb::cancel)) && !*ui.contextMenuOpen(), "cancel on the menu closes it");
    check(bool(tree.perform({"note"}, Verb::contextMenu, MenuPosition{7, 9})) && *ui.contextMenuOpen(), "a position can be passed");
    check(tree.perform({"note"}, Verb::contextMenu, 5).status == Status::invalidValue, "a wrong argument is refused");
    ui.closeContextMenu();
    check(hasVerb(ui, "note", Verb::contextMenu), "the verb is still there after closing");
    // The typed call and the semantic call agree on the result vocabulary.
    field.handle.enable(false);
    check(tree.perform({"note"}, Verb::contextMenu).status == ui.openContextMenu(field.handle).status, "disabled: the same status both ways");
}

void semanticEventsAnnounceOpeningFocusAndClosing() {
    Ui ui; auto field = addField(ui, "note", u"hello");
    field.handle.focus();
    std::vector<std::pair<SemanticEventKind, NodeRef>> events;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { events.push_back({e.kind, e.ref}); });
    auto has = [&](SemanticEventKind kind, const NodeRef& ref) {
        return std::any_of(events.begin(), events.end(), [&](const auto& e) { return e.first == kind && e.second == ref; });
    };
    const auto menuId = menuNodeId("note");
    ui.openContextMenu(field.handle);
    ui.semantics().dispatchEvents();
    check(has(SemanticEventKind::menuOpened, {menuId}), "opening is announced");
    events.clear();
    press(ui, Key::down);
    ui.semantics().dispatchEvents();
    check(has(SemanticEventKind::focusChanged, {menuId, OptionValue{"lcars.undo"}}), "highlighting the first item moves focus to it");
    events.clear();
    press(ui, Key::down);
    ui.semantics().dispatchEvents();
    check(has(SemanticEventKind::focusChanged, {menuId, OptionValue{"lcars.redo"}}), "and then to the next");
    events.clear();
    press(ui, Key::escape);
    ui.semantics().dispatchEvents();
    check(has(SemanticEventKind::menuClosed, {menuId}), "closing is announced");
    check(has(SemanticEventKind::focusChanged, {"note"}), "and focus returns to the field");
}
}

int main() {
    std::cout << std::unitbuf;
    static const test::Case cases[] = {
        {"attach modes: append, replace, off", attachModesAppendReplaceAndOff},
        {"other components have no menu until one is attached", otherComponentsHaveNoMenuUntilOneIsAttached},
        {"menu closes by Escape, Alt, F10 and deactivation", menuClosesByEscapeAltF10AndDeactivation},
        {"every way of closing a menu announces menu_closed", everyWayOfClosingAnnouncesMenuClosed},
        {"removing the component announces menu_closed", removingTheComponentAnnouncesMenuClosed},
        {"menu cannot open or survive when the component cannot act", menuCannotOpenOrSurviveWhenTheComponentCannotAct},
        {"list menu selects the row and hands it to the application", listMenuSelectsTheRowAndHandsItToTheApplication},
        {"items can change while the menu is open", itemsCanChangeWhileTheMenuIsOpen},
        {"menu panel sits beside the pointer, flips and scrolls", menuPanelSitsBesideThePointerFlipsAndScrolls},
        {"keyboard position follows the caret and the current row", keyboardPositionFollowsTheCaretAndTheCurrentRow},
        {"multi-line field menu hangs from the caret line", multiLineFieldMenuHangsFromTheCaretLine},
        {"semantic tree shows menu and items while open", semanticTreeShowsMenuAndItemsWhileOpen},
        {"semantic events announce opening, focus and closing", semanticEventsAnnounceOpeningFocusAndClosing},
        {"text field menu has the Windows edit items", textFieldMenuHasTheWindowsEditItems},
        {"items are unavailable when they cannot act", itemsAreUnavailableWhenTheyCannotAct},
        {"read-only field only copies and selects", readOnlyFieldOnlyCopiesAndSelects},
        {"built-in items match their shortcuts", builtInItemsMatchTheirShortcuts},
        {"rich field menu adds bold italic underline", richFieldMenuAddsBoldItalicUnderline},
        {"style items match their shortcuts", styleItemsMatchTheirShortcuts},
        {"underline shares its quick key with undo", underlineSharesItsQuickKeyWithUndo},
        {"keyboard moves, skips lines and wraps", keyboardMovesSkipsLinesAndWraps},
        {"quick keys run unique and cycle shared letters", quickKeysRunUniqueAndCycleSharedLetters},
    };
    return test::run("context_menu", cases);
}
