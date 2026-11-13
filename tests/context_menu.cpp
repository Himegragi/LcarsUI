#include "core_internal.hpp"
#include "component_type.hpp"
// Context menu (#85) through the public core interface: attach, open, query, keyboard, execute,
// semantics and geometry. No window, GPU or font backend; the real-window twin is in host_verify.
#include "check.hpp"
#include "fake_measure.hpp"
#include "geometry.hpp"
#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
#include <algorithm>
using namespace lcars;
using test::check;
using test::inside;

namespace {
struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override {
        if (!content) return {Status::not_supported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, *content};
    }
};
// An editable, immediately written text field over an application-owned model.
struct Field {
    TextHandle handle;
    std::shared_ptr<std::u16string> model;
};
Field add_field(Ui& ui, std::string id, std::u16string value, bool read_only = false) {
    auto model = std::make_shared<std::u16string>(value);
    auto handle = ui.add_text({.id = id, .kind = ComponentKind::text, .label = u"欄位", .value = value, .read_only = read_only}).value.value();
    check(bool(handle.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "connect field source");
    check(bool(handle.connect_immediate([model](const Change<TextHandle>& change) { *model = change.value; return Result{}; })), "connect field write");
    return {handle, model};
}
detail::UiState& core(Ui& ui) { return detail::access(ui); }
Result press(Ui& ui, Key key, bool shift = false) { return core(ui).key_down({.key = key, .shift = shift}); }
Result type(Ui& ui, char16_t ch) { return core(ui).text_input(std::u16string(1, ch)); }
std::vector<std::string> ids_of(const MenuInfo& menu) {
    std::vector<std::string> ids;
    for (const auto& item : menu.items) ids.push_back(item.separator ? "-" : item.id);
    return ids;
}
const MenuItemInfo* find_item(const MenuInfo& menu, std::string_view id) {
    auto it = std::find_if(menu.items.begin(), menu.items.end(), [&](const auto& item) { return item.id == id; });
    return it == menu.items.end() ? nullptr : &*it;
}
bool enabled(Ui& ui, std::string_view id) {
    auto menu = ui.context_menu();
    auto item = menu ? find_item(*menu, id) : nullptr;
    return item && item->enabled;
}
bool has_verb(Ui& ui, std::string_view id, Verb verb) {
    auto node = ui.semantics().get({std::string(id)});
    return node && std::find(node->verbs.begin(), node->verbs.end(), verb) != node->verbs.end();
}
std::string highlighted(Ui& ui) {
    auto menu = ui.context_menu();
    if (!menu) return "<closed>";
    for (const auto& item : menu->items) if (item.highlighted) return item.id;
    return "";
}
const std::vector<std::string> builtin_layout{"lcars.undo", "lcars.redo", "-", "lcars.cut", "lcars.copy", "lcars.paste", "lcars.delete", "-", "lcars.select_all"};

void text_field_menu_has_the_windows_edit_items() {
    Ui ui; auto field = add_field(ui, "note", u"hello world");
    check(!*ui.context_menu_open(), "no menu is open at the start");
    check(ui.context_menu().status == Status::hidden, "querying a closed menu reports hidden");
    check(bool(ui.open_context_menu(field.handle)), "a text field opens its built-in menu");
    check(*ui.context_menu_open() && std::holds_alternative<TextHandle>(ui.context_menu()->target) &&
          std::get<TextHandle>(ui.context_menu()->target) == field.handle, "the menu is open for the field");
    auto menu = ui.context_menu();
    check(ids_of(*menu) == builtin_layout, "built-in items: undo, redo, line, cut, copy, paste, delete, line, select all");
    check(find_item(*menu, "lcars.undo")->label == u"復原(U)" && find_item(*menu, "lcars.redo")->label == u"重做(R)" &&
          find_item(*menu, "lcars.cut")->label == u"剪下(T)" && find_item(*menu, "lcars.copy")->label == u"複製(C)" &&
          find_item(*menu, "lcars.paste")->label == u"貼上(P)" && find_item(*menu, "lcars.delete")->label == u"刪除(D)" &&
          find_item(*menu, "lcars.select_all")->label == u"全選(A)", "labels follow the Windows edit menu");
    check(find_item(*menu, "lcars.undo")->access_key == u'U' && find_item(*menu, "lcars.redo")->access_key == u'R' &&
          find_item(*menu, "lcars.cut")->access_key == u'T' && find_item(*menu, "lcars.copy")->access_key == u'C' &&
          find_item(*menu, "lcars.paste")->access_key == u'P' && find_item(*menu, "lcars.delete")->access_key == u'D' &&
          find_item(*menu, "lcars.select_all")->access_key == u'A', "quick keys match the letters in the labels");
    check(find_item(*menu, "lcars.cut")->shortcut_hint == u"Ctrl+X" && find_item(*menu, "lcars.paste")->shortcut_hint == u"Ctrl+V" &&
          find_item(*menu, "lcars.delete")->shortcut_hint == u"Del", "shortcut hints are shown");
}

void items_are_unavailable_when_they_cannot_act() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard);
    auto field = add_field(ui, "note", u"hello world");
    ui.open_context_menu(field.handle);
    check(!enabled(ui, "lcars.cut") && !enabled(ui, "lcars.copy") && !enabled(ui, "lcars.delete"), "no selection: cut, copy and delete are off");
    check(!enabled(ui, "lcars.paste"), "empty clipboard: paste is off");
    check(!enabled(ui, "lcars.undo") && !enabled(ui, "lcars.redo"), "no history: undo and redo are off");
    check(enabled(ui, "lcars.select_all"), "select all is on");
    ui.close_context_menu();
    clipboard.content = u"x";
    check(bool(field.handle.select(0, 5)), "select a range");
    ui.open_context_menu(field.handle);
    check(enabled(ui, "lcars.cut") && enabled(ui, "lcars.copy") && enabled(ui, "lcars.delete") && enabled(ui, "lcars.paste"),
          "a selection and clipboard text turn cut, copy, delete and paste on");
    check(bool(ui.invoke_menu_item("lcars.delete")), "delete the selection through the menu");
    ui.open_context_menu(field.handle);
    check(enabled(ui, "lcars.undo") && !enabled(ui, "lcars.redo"), "an edit turns undo on");
    ui.invoke_menu_item("lcars.undo");
    ui.open_context_menu(field.handle);
    check(!enabled(ui, "lcars.undo") && enabled(ui, "lcars.redo"), "after undo, redo is on");
}

void read_only_field_only_copies_and_selects() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard); clipboard.content = u"x";
    auto field = add_field(ui, "note", u"hello", true);
    field.handle.select(0, 5);
    check(bool(ui.open_context_menu(field.handle)), "a read-only field still has its menu");
    for (const char* id : {"lcars.undo", "lcars.redo", "lcars.cut", "lcars.paste", "lcars.delete"})
        check(!enabled(ui, id), "a read-only field refuses every editing item");
    check(enabled(ui, "lcars.copy") && enabled(ui, "lcars.select_all"), "copy and select all stay on");
}

// Every built-in item is the same operation as its shortcut: value, clipboard and history agree.
void built_in_items_match_their_shortcuts() {
    // Besides menu == shortcut, each case states what the operation must do on "hello world" with "hello" selected
    // (code review 2026-11-12, item 16): the value after it, the clipboard after it, and the final selection.
    struct Case { const char* item; KeyPress key; const char16_t* clipboard; const char16_t* value; std::optional<std::u16string> clipboard_after; unsigned anchor, caret; };
    const Case cases[] = {
        {"lcars.cut", {.ctrl = true, .letter = u'X'}, nullptr, u" world", u"hello", 0, 0},
        {"lcars.copy", {.ctrl = true, .letter = u'C'}, nullptr, u"hello world", u"hello", 0, 5},
        {"lcars.paste", {.ctrl = true, .letter = u'V'}, u"PASTED", u"PASTED world", u"PASTED", 6, 6},
        {"lcars.delete", {.key = Key::del}, nullptr, u" world", std::nullopt, 0, 0},
        {"lcars.select_all", {.ctrl = true, .letter = u'A'}, nullptr, u"hello world", std::nullopt, 0, 11},
    };
    for (const auto& c : cases) {
        Ui by_menu, by_key; MemoryClipboard menu_clipboard, key_clipboard;
        core(by_menu).set_clipboard(&menu_clipboard); core(by_key).set_clipboard(&key_clipboard);
        if (c.clipboard) { menu_clipboard.content = c.clipboard; key_clipboard.content = c.clipboard; }
        auto a = add_field(by_menu, "f", u"hello world"), b = add_field(by_key, "f", u"hello world");
        a.handle.select(0, 5); b.handle.select(0, 5);
        b.handle.focus();
        check(bool(by_menu.open_context_menu(a.handle)), "open for the menu twin");
        check(bool(by_menu.invoke_menu_item(c.item)), "run the menu item");
        check(bool(core(by_key).key_down(c.key)), "run the shortcut");
        check(*a.model == c.value && *b.model == c.value, (std::string(c.item) + ": the value after the operation").c_str());
        check(menu_clipboard.content == c.clipboard_after && key_clipboard.content == c.clipboard_after, (std::string(c.item) + ": the clipboard after the operation").c_str());
        check(*a.model == *b.model, "the menu item and its shortcut leave the same value");
        check(a.handle.draft().value.value() == b.handle.draft().value.value(), "same field text");
        check(menu_clipboard.content == key_clipboard.content, "the same clipboard content");
        auto sa = *core(by_menu).describe("f"), sb = *core(by_key).describe("f");
        check(sa.operations == sb.operations, "the same undo history (undo and redo operations)");
        auto selection_a = by_menu.semantics().get({"f"})->text_selection, selection_b = by_key.semantics().get({"f"})->text_selection;
        check(selection_a && selection_b && selection_a->anchor == selection_b->anchor && selection_a->caret == selection_b->caret, "the same selection");
        check(selection_a && selection_a->anchor == c.anchor && selection_a->caret == c.caret, (std::string(c.item) + ": the selection after the operation").c_str());
        check(a.handle.undo().status == b.handle.undo().status && *a.model == *b.model, "undo behaves the same afterwards");
    }
}

// A rich (multi-line, styled) field over an application-owned model, written at once.
struct RichField {
    TextHandle handle;
    std::shared_ptr<RichText> model;
};
RichField add_rich_field(Ui& ui, std::string id, std::u16string value, bool read_only = false) {
    auto model = std::make_shared<RichText>(RichText{value, {}});
    auto handle = ui.add_text({.id = id, .kind = ComponentKind::text, .label = u"內容", .value = value, .read_only = read_only, .multiline = true, .rich = true}).value.value();
    check(bool(handle.connect_rich_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "connect rich source");
    check(bool(handle.connect_rich_immediate([model](const Change<TextHandle, RichText>& change) { *model = change.value; return Result{}; })), "connect rich write");
    return {handle, model};
}
const std::vector<std::string> rich_layout{"lcars.undo", "lcars.redo", "-", "lcars.cut", "lcars.copy", "lcars.paste", "lcars.delete", "-", "lcars.select_all",
                                           "-", "lcars.bold", "lcars.italic", "lcars.underline"};

void rich_field_menu_adds_bold_italic_underline() {
    Ui ui; auto field = add_rich_field(ui, "note", u"hello world");
    check(bool(ui.open_context_menu(field.handle)), "a rich field opens its built-in menu");
    auto menu = ui.context_menu();
    check(ids_of(*menu) == rich_layout, "the built-in items, a line, then bold, italic and underline");
    check(find_item(*menu, "lcars.bold")->label == u"粗體(B)" && find_item(*menu, "lcars.italic")->label == u"斜體(I)" &&
          find_item(*menu, "lcars.underline")->label == u"底線(U)", "labels follow the Windows rich-edit menu");
    check(find_item(*menu, "lcars.bold")->access_key == u'B' && find_item(*menu, "lcars.italic")->access_key == u'I' &&
          find_item(*menu, "lcars.underline")->access_key == u'U', "quick keys match the letters in the labels");
    check(find_item(*menu, "lcars.bold")->shortcut_hint == u"Ctrl+B" && find_item(*menu, "lcars.italic")->shortcut_hint == u"Ctrl+I" &&
          find_item(*menu, "lcars.underline")->shortcut_hint == u"Ctrl+U", "shortcut hints are shown");
    check(enabled(ui, "lcars.bold") && enabled(ui, "lcars.italic") && enabled(ui, "lcars.underline"), "on in an editable rich field, with or without a selection");
    ui.close_context_menu();
    // Only rich fields have them; a plain field keeps the old layout, and a read-only rich field disables them.
    auto plain = add_field(ui, "plain", u"hello");
    ui.open_context_menu(plain.handle);
    check(ids_of(*ui.context_menu()) == builtin_layout, "a plain field has no style items");
    ui.close_context_menu();
    auto locked = add_rich_field(ui, "locked", u"hello", true);
    ui.open_context_menu(locked.handle);
    check(ids_of(*ui.context_menu()) == rich_layout && !enabled(ui, "lcars.bold") && !enabled(ui, "lcars.italic") && !enabled(ui, "lcars.underline"),
          "a read-only rich field shows them but refuses every one");
    ui.close_context_menu();
    // The application's own switches still reach it: replace drops the built-ins, style items included.
    auto handle = ui.add_context_menu(field.handle, {MenuItem{.id = "mine", .label = u"我的(M)", .access_key = u'M'}}, MenuMode::replace).value.value();
    ui.open_context_menu(field.handle);
    check(ids_of(*ui.context_menu()) == std::vector<std::string>{"mine"}, "MenuMode::replace leaves no style items");
    (void)handle;
}

// Bold, italic and underline run the very function Ctrl+B / Ctrl+I / Ctrl+U run, on a selection and at a caret.
void style_items_match_their_shortcuts() {
    struct Case { const char* item; char16_t letter; bool selection; };
    const Case cases[] = {
        {"lcars.bold", u'B', true}, {"lcars.italic", u'I', true}, {"lcars.underline", u'U', true},
        {"lcars.bold", u'B', false}, {"lcars.italic", u'I', false}, {"lcars.underline", u'U', false},
    };
    for (const auto& c : cases) {
        Ui by_menu, by_key;
        auto a = add_rich_field(by_menu, "f", u"hello world"), b = add_rich_field(by_key, "f", u"hello world");
        if (c.selection) { a.handle.select(0, 5); b.handle.select(0, 5); } else { a.handle.select(3, 3); b.handle.select(3, 3); }
        b.handle.focus();
        check(bool(by_menu.open_context_menu(a.handle)), "open for the menu twin");
        check(bool(by_menu.invoke_menu_item(c.item)), "run the menu item");
        check(bool(core(by_key).key_down({.ctrl = true, .letter = c.letter})), "run the shortcut");
        check(*a.model == *b.model, "the menu item and its shortcut leave the same rich value");
        check(a.handle.rich_value().value.value() == b.handle.rich_value().value.value(), "same spans");
        check(a.handle.typing_style().value.value() == b.handle.typing_style().value.value(), "the same typing style at a caret");
        if (c.selection) {
            const auto spans = a.handle.rich_value().value.value().spans;
            check(spans.size() == 1 && spans[0].start == 0 && spans[0].length == 5 &&
                  (c.letter == u'B' ? spans[0].bold : c.letter == u'I' ? spans[0].italic : spans[0].underline), "the selection takes the style");
        } else {
            const auto typing = a.handle.typing_style().value.value();
            check(c.letter == u'B' ? typing.bold : c.letter == u'I' ? typing.italic : typing.underline, "a caret starts the style for the next typed text");
        }
        check(a.handle.undo().status == b.handle.undo().status && *a.model == *b.model, "undo behaves the same afterwards");
        // Asking again takes the style away again, through the menu as through the key.
        by_menu.open_context_menu(a.handle);
        by_menu.invoke_menu_item(c.item); core(by_key).key_down({.ctrl = true, .letter = c.letter});
        check(a.handle.rich_value().value.value() == b.handle.rich_value().value.value(), "a second toggle agrees too");
    }
}

// U is both 復原(U) and 底線(U): the quick key cycles between them without running either, and Enter runs the highlighted one.
void underline_shares_its_quick_key_with_undo() {
    Ui ui; auto field = add_rich_field(ui, "note", u"hello world");
    field.handle.select(0, 5); field.handle.focus();
    check(bool(type(ui, u'x')), "type over the selection, so that undo has something to undo");
    field.handle.select(0, 1);
    press(ui, Key::menu);
    check(enabled(ui, "lcars.undo") && enabled(ui, "lcars.underline"), "both items that share U are on");
    type(ui, u'U');
    check(*ui.context_menu_open() && highlighted(ui) == "lcars.undo", "U jumps to the first match, 復原(U), without running it");
    check(field.handle.rich_value().value.value().spans.empty(), "nothing was run by the quick key");
    type(ui, u'u');
    check(highlighted(ui) == "lcars.underline" && *ui.context_menu_open(), "pressed again it goes to 底線(U), still without running");
    type(ui, u'U');
    check(highlighted(ui) == "lcars.undo", "and wraps around");
    type(ui, u'u');
    check(bool(press(ui, Key::enter)) && !*ui.context_menu_open(), "Enter runs the highlighted 底線(U)");
    const auto spans = field.handle.rich_value().value.value().spans;
    check(spans.size() == 1 && spans[0].start == 0 && spans[0].length == 1 && spans[0].underline, "the selected character is underlined");
    // B and I are unique letters: they run at once.
    press(ui, Key::menu);
    check(bool(type(ui, u'b')) && !*ui.context_menu_open(), "B runs 粗體(B) at once");
    check(field.handle.rich_value().value.value().spans.size() == 1 && field.handle.rich_value().value.value().spans[0].bold, "the character is now bold too");
    press(ui, Key::menu);
    check(bool(type(ui, u'I')) && !*ui.context_menu_open() && field.handle.rich_value().value.value().spans[0].italic, "I runs 斜體(I) at once");
}

void keyboard_moves_skips_lines_and_wraps() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard); clipboard.content = u"x";
    auto field = add_field(ui, "note", u"hello");
    field.handle.focus();
    check(bool(press(ui, Key::menu)) && *ui.context_menu_open(), "the menu key opens the menu");
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
    check(press(ui, Key::enter).status == Status::disabled && *ui.context_menu_open(), "a disabled item can be reached but not run, and the menu stays");
    press(ui, Key::end);
    check(bool(press(ui, Key::space)) && !*ui.context_menu_open(), "Space runs the highlighted item and closes the menu");
    check(core(ui).describe("note")->value == u"hello" && field.handle.describe()->focused, "select all ran; the field kept focus");
    check(core(ui).focused() == "note", "focus is on the field again");
    press(ui, Key::menu);
    press(ui, Key::end); press(ui, Key::up);
    check(highlighted(ui) == "lcars.delete", "Up from the last item lands before the separator");
    // Code review 2026-11-12 item 13: a standard Win32 popup menu ignores the Apps key while open (probed with TrackPopupMenu: F10 and
    // Alt close it, VK_APPS does not), and the spec's close list is Esc / Alt / F10 / click outside / deactivation.
    press(ui, Key::menu);
    check(*ui.context_menu_open() && highlighted(ui) == "lcars.delete", "the menu key again leaves the open menu and its highlight alone");
    press(ui, Key::escape);
    check(!*ui.context_menu_open(), "Escape still closes it");
    core(ui).key_down({.key = Key::f10, .shift = true});
    check(*ui.context_menu_open(), "Shift+F10 opens the menu");
    core(ui).key_down({.key = Key::enter});
    check(*ui.context_menu_open(), "Enter with nothing highlighted does nothing");
}

void quick_keys_run_unique_and_cycle_shared_letters() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard);
    auto field = add_field(ui, "note", u"hello world");
    field.handle.select(0, 5); field.handle.focus();
    press(ui, Key::menu);
    check(bool(type(ui, u'c')) && !*ui.context_menu_open() && clipboard.content == std::u16string(u"hello"), "a unique quick key runs its item at once (lower case)");
    press(ui, Key::menu);
    check(bool(type(ui, u'A')) && !*ui.context_menu_open(), "upper case works the same");
    auto button = ui.add_button({.id = "b", .kind = ComponentKind::button, .label = u"B"}).value.value();
    int runs = 0;
    auto menu = ui.add_context_menu(button, {
        {.id = "underline", .label = u"底線(U)", .access_key = u'U', .action = [&](const MenuContext&) { ++runs; return Result{}; }},
        {.id = "uppercase", .label = u"大寫(U)", .access_key = u'U', .action = [&](const MenuContext&) { runs += 10; return Result{}; }},
        {.id = "other", .label = u"其他(O)", .access_key = u'O'},
    }).value.value();
    button.focus(); press(ui, Key::menu);
    type(ui, u'U');
    check(*ui.context_menu_open() && highlighted(ui) == "underline" && runs == 0, "a shared letter jumps to the first match without running");
    type(ui, u'u');
    check(highlighted(ui) == "uppercase" && runs == 0, "pressed again it goes to the next match");
    type(ui, u'U');
    check(highlighted(ui) == "underline", "and wraps around");
    type(ui, u'z');
    check(highlighted(ui) == "underline" && *ui.context_menu_open(), "an unused letter does nothing");
    type(ui, u'O');
    check(!*ui.context_menu_open() && runs == 0, "a unique letter runs its item");
    // Letters never reach the field while a menu is open.
    field.handle.focus(); press(ui, Key::menu); type(ui, u'q');
    check(core(ui).describe("note")->value == u"hello world", "a letter is a quick key, not text");
    (void)menu;
}

MenuItem command(std::string id, std::u16string label, char16_t key, int* runs = nullptr) {
    return {.id = std::move(id), .label = std::move(label), .access_key = key,
            .action = [runs](const MenuContext&) { if (runs) ++*runs; return Result{}; }};
}

void attach_modes_append_replace_and_off() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard);
    auto field = add_field(ui, "note", u"hello");
    auto menu = ui.add_context_menu(field.handle, {command("app.one", u"一(1)", u'1'), MenuItem::divider(), command("app.two", u"二(2)", u'2')}).value.value();
    ui.open_context_menu(field.handle);
    auto appended = builtin_layout; appended.insert(appended.end(), {"-", "app.one", "-", "app.two"});
    check(ids_of(*ui.context_menu()) == appended, "append (the default): built-in items, a separator, then the application's");
    ui.close_context_menu();
    check(bool(menu.set_mode(MenuMode::replace)), "switch to replace");
    ui.open_context_menu(field.handle);
    check(ids_of(*ui.context_menu()) == std::vector<std::string>({"app.one", "-", "app.two"}), "replace: only the application's items");
    ui.close_context_menu();
    check(bool(menu.set_mode(MenuMode::off)), "switch to off");
    check(ui.open_context_menu(field.handle).status == Status::not_supported, "off: this field has no menu");
    check(!has_verb(ui, "note", Verb::context_menu) && has_verb(ui, "note", Verb::focus), "off: the semantic layer does not offer the menu either");
    check(bool(menu.set_mode(MenuMode::append)), "back to append");
    check(bool(menu.set_items({})), "no application items");
    ui.open_context_menu(field.handle);
    check(ids_of(*ui.context_menu()) == builtin_layout, "append with nothing to add shows only the built-in items, with no stray separator");
    ui.close_context_menu();
    check(bool(menu.set_items({command("only", u"唯一(O)", u'O')})) && bool(menu.set_mode(MenuMode::replace)), "replace with one item");
    ui.open_context_menu(field.handle);
    check(ids_of(*ui.context_menu()) == std::vector<std::string>({"only"}), "replace with one item");
    ui.close_context_menu();
    // Separators at the ends or doubled are not shown.
    menu.set_items({MenuItem::divider(), command("a", u"甲", 0), MenuItem::divider(), MenuItem::divider(), command("b", u"乙", 0), MenuItem::divider()});
    ui.open_context_menu(field.handle);
    check(ids_of(*ui.context_menu()) == std::vector<std::string>({"a", "-", "b"}), "leading, trailing and repeated separators collapse");
}

void other_components_have_no_menu_until_one_is_attached() {
    Ui ui;
    auto button = ui.add_button({.id = "b", .kind = ComponentKind::button, .label = u"按鈕"}).value.value();
    check(ui.open_context_menu(button).status == Status::not_supported, "a button has no menu by default");
    check(!has_verb(ui, "b", Verb::context_menu), "and the semantic layer does not offer one");
    check(core(ui).menu_pointer_open("b", std::nullopt, std::nullopt, {5, 5}).status == Status::not_supported && !*ui.context_menu_open(), "right button on it does nothing");
    int runs = 0;
    auto menu = ui.add_context_menu(button, {command("go", u"執行(G)", u'G', &runs)});
    check(bool(menu), "attach a menu to a button");
    check(ui.add_context_menu(button, {}).status == Status::invalid_value, "a component carries one attachment");
    check(bool(ui.open_context_menu(button)) && ids_of(*ui.context_menu()) == std::vector<std::string>({"go"}), "the button opens it");
    check(bool(type(ui, u'g')) && runs == 1 && !*ui.context_menu_open(), "its quick key runs the application's code");
    check(core(ui).focused() == "b", "the button was focused by opening its menu");
    auto bad = ui.add_button({.id = "c", .kind = ComponentKind::button}).value.value();
    check(ui.add_context_menu(bad, {command("x", u"x", 0), command("x", u"y", 0)}).status == Status::invalid_value, "duplicate ids are refused");
    check(ui.add_context_menu(bad, {command("", u"x", 0)}).status == Status::invalid_value, "an item needs an id");
    check(ui.add_context_menu(bad, {command("lcars.cut", u"x", 0)}).status == Status::invalid_value, "the lcars. prefix is reserved");
    check(bool(ui.add_context_menu(bad, {})) , "an empty attachment is allowed (it is not a menu yet)");
    check(ui.open_context_menu(bad).status == Status::not_supported, "and opens nothing");
}

void menu_closes_by_escape_alt_f10_and_deactivation() {
    Ui ui; auto field = add_field(ui, "note", u"hello");
    core(ui).window_active(true);
    struct Close { const char* name; std::function<void()> close; };
    const Close closers[] = {
        {"Escape", [&] { press(ui, Key::escape); }},
        {"Alt", [&] { press(ui, Key::alt); }},
        {"F10", [&] { press(ui, Key::f10); }},
        {"the window losing activation", [&] { core(ui).window_active(false); core(ui).window_active(true); }},
        {"close_context_menu", [&] { ui.close_context_menu(); }},
    };
    for (const auto& c : closers) {
        field.handle.focus();
        check(bool(ui.open_context_menu(field.handle)) && *ui.context_menu_open(), "open before closing");
        c.close();
        check(!*ui.context_menu_open(), c.name);
        check(core(ui).focused() == "note" && field.handle.describe()->focused, "focus is on the field after closing");
    }
    check(*field.model == u"hello", "closing changed nothing");
    // The open menu swallows keys meant for the field.
    ui.open_context_menu(field.handle);
    core(ui).key_down({.ctrl = true, .letter = u'A'});
    core(ui).key_down({.key = Key::backspace});
    core(ui).key_down({.key = Key::left});
    check(*field.model == u"hello" && *ui.context_menu_open(), "other keys do nothing while the menu is open");
}

// Code review 2026-11-12 item 16: every way of closing a menu is announced (MenuClosed in UIA is built on this event), and
// every opening is announced, not only the first one.
void every_way_of_closing_announces_menu_closed() {
    std::vector<std::pair<std::string, std::function<void(Ui&, Field&)>>> closers = {
        {"Escape", [](Ui& ui, Field&) { press(ui, Key::escape); }},
        {"Alt", [](Ui& ui, Field&) { press(ui, Key::alt); }},
        {"F10", [](Ui& ui, Field&) { press(ui, Key::f10); }},
        {"the window losing activation", [](Ui& ui, Field&) { core(ui).window_active(false); core(ui).window_active(true); }},
        {"close_context_menu", [](Ui& ui, Field&) { ui.close_context_menu(); }},
        {"the semantic cancel verb", [](Ui& ui, Field&) { ui.semantics().perform({menu_node_id("note")}, Verb::cancel); }},
    };
    for (const auto& [name, close] : closers) {
        Ui ui; auto field = add_field(ui, "note", u"hello");
        core(ui).window_active(true);
        std::vector<SemanticEventKind> kinds;
        auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { kinds.push_back(e.kind); });
        auto count = [&](SemanticEventKind kind) { return std::count(kinds.begin(), kinds.end(), kind); };
        for (int round = 1; round <= 2; ++round) {  // the second opening is announced like the first
            field.handle.focus();
            check(bool(ui.open_context_menu(field.handle)) && *ui.context_menu_open(), "open");
            ui.semantics().dispatch_events();
            check(count(SemanticEventKind::menu_opened) == round, (name + ": opening number " + std::to_string(round) + " is announced").c_str());
            if (round == 1) {
                close(ui, field);
                ui.semantics().dispatch_events();
                check(!*ui.context_menu_open() && count(SemanticEventKind::menu_closed) == 1, (name + ": closing is announced once").c_str());
            } else {
                press(ui, Key::escape);
                ui.semantics().dispatch_events();
                check(count(SemanticEventKind::menu_closed) == 2, (name + ": the second menu closes with its own announcement").c_str());
            }
        }
    }
}

// Removing the component a menu hangs from closes the menu, and that is announced too.
void removing_the_component_announces_menu_closed() {
    Ui ui;
    auto button = ui.add_button({.id = "b", .kind = ComponentKind::button}).value.value();
    ui.add_context_menu(button, {command("go", u"執行(G)", u'G')});
    std::vector<SemanticEventKind> kinds;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { kinds.push_back(e.kind); });
    check(bool(ui.open_context_menu(button)), "open the button's menu");
    ui.semantics().dispatch_events();
    check(std::count(kinds.begin(), kinds.end(), SemanticEventKind::menu_opened) == 1, "opening is announced");
    check(bool(button.remove()) && !*ui.context_menu_open(), "removing the component closes the menu");
    ui.semantics().dispatch_events();
    check(std::count(kinds.begin(), kinds.end(), SemanticEventKind::menu_closed) == 1, "and the closing is announced once");
}

void menu_cannot_open_or_survive_when_the_component_cannot_act() {
    Ui ui; auto field = add_field(ui, "note", u"hello");
    field.handle.enable(false);
    check(ui.open_context_menu(field.handle).status == Status::disabled, "a disabled component opens no menu");
    check(core(ui).menu_pointer_open("note", std::nullopt, 0, {1, 1}).status == Status::disabled && !*ui.context_menu_open(), "nor does the right button on it");
    field.handle.enable(true);
    ui.open_context_menu(field.handle);
    field.handle.enable(false);
    check(!*ui.context_menu_open(), "disabling the component closes its open menu at once");
    field.handle.enable(true);
    auto button = ui.add_button({.id = "b", .kind = ComponentKind::button}).value.value();
    ui.add_context_menu(button, {command("go", u"執行(G)", u'G')});
    ui.open_context_menu(button);
    check(bool(button.remove()) && !*ui.context_menu_open(), "removing the component closes its open menu");
    check(ui.open_context_menu(button).status == Status::not_found, "a removed component cannot open one");
    // A modal dialog blocks the background.
    auto cancel = ui.add_button({.id = "cancel", .kind = ComponentKind::button}).value.value();
    auto fallback = ui.add_button({.id = "fallback", .kind = ComponentKind::button}).value.value();
    auto inner = add_field(ui, "inner", u"x");
    auto dialog = ui.add_dialog("d", {inner.handle, cancel}, inner.handle, fallback, [](bool) {}).value.value();
    ui.open_context_menu(field.handle);
    check(bool(dialog.open()) && !*ui.context_menu_open(), "opening a dialog closes an open menu");
    check(ui.open_context_menu(field.handle).status == Status::blocked_by_modal, "a background component refuses while a dialog is open");
    check(core(ui).menu_pointer_open("note", std::nullopt, 0, {1, 1}).status == Status::blocked_by_modal && !*ui.context_menu_open(), "right button on the background does nothing");
    check(ui.semantics().perform({"note"}, Verb::context_menu).status == Status::blocked_by_modal, "the semantic layer gives the same status");
    check(bool(ui.open_context_menu(inner.handle)) && *ui.context_menu_open(), "a field inside the dialog opens its menu");
    check(bool(ui.close_context_menu()) && bool(dialog.settle(false)), "close");
    core(ui).renderer_failed(true);
    check(ui.open_context_menu(field.handle).status == Status::renderer_failed, "a failed renderer opens no menu");
}

void list_menu_selects_the_row_and_hands_it_to_the_application() {
    Ui ui;
    auto selected = std::make_shared<std::optional<OptionValue>>();
    auto list = ui.add_list({.id = "items", .kind = ComponentKind::list, .label = u"項目"}, {{{"a"}, u"甲"}, {{"b"}, u"乙"}, {{"c"}, u"丙", false}}).value.value();
    check(bool(list.connect_source({[selected] { return *selected; }, [](std::function<void()>) { return [] {}; }})), "connect list source");
    check(bool(list.connect_immediate([selected](std::optional<OptionValue> value) { *selected = value; return Result{}; })), "connect list write");
    std::optional<OptionValue> got; int runs = 0;
    auto menu = ui.add_context_menu(list, {
        {.id = "edit", .label = u"編輯(E)", .access_key = u'E', .shortcut_hint = u"Enter",
         .action = [&](const MenuContext& c) { ++runs; got = c.item; return Result{}; }}}).value.value();
    check(bool(core(ui).menu_pointer_open("items", 1, std::nullopt, {10, 10})) && *ui.context_menu_open(), "right button on a row opens the menu");
    check(selected->has_value() && **selected == OptionValue{"b"}, "the row was selected first");
    check(ui.context_menu()->item && *ui.context_menu()->item == OptionValue{"b"}, "the menu knows its row");
    check(bool(ui.invoke_menu_item("edit")) && runs == 1 && got && *got == OptionValue{"b"}, "the application's code receives that row");
    check(core(ui).menu_pointer_open("items", 2, std::nullopt, {10, 10}).status == Status::disabled && !*ui.context_menu_open() && **selected == OptionValue{"b"},
          "a disabled row opens nothing and keeps the selection");
    check(bool(core(ui).menu_pointer_open("items", std::nullopt, std::nullopt, {10, 10})) && !ui.context_menu()->item && **selected == OptionValue{"b"},
          "empty space opens the menu with no row and keeps the selection");
    ui.close_context_menu();
    *selected = std::nullopt; got.reset();
    list.focus();
    check(bool(press(ui, Key::menu)) && ui.context_menu()->item && *ui.context_menu()->item == OptionValue{"b"} && !selected->has_value(),
          "the menu key hands over the list's current row without selecting it");
    ui.invoke_menu_item("edit");
    check(got && *got == OptionValue{"b"}, "the application's code received the current row");
    check(bool(ui.open_context_menu(list, MenuPosition{3, 4}, OptionValue{"b"})) && **selected == OptionValue{"b"}, "the typed call can name the row");
    check(ui.open_context_menu(list, MenuPosition{3, 4}, OptionValue{"zz"}).status == Status::not_found, "an unknown row is refused");
    check(ui.open_context_menu(list, MenuPosition{3, 4}, OptionValue{"c"}).status == Status::disabled, "a disabled row is refused");
    (void)menu;
}

void items_can_change_while_the_menu_is_open() {
    Ui ui; auto field = add_field(ui, "note", u"hello");
    int runs = 0;
    auto menu = ui.add_context_menu(field.handle, {command("one", u"一", 0, &runs), command("two", u"二", 0, &runs)}).value.value();
    ui.open_context_menu(field.handle);
    core(ui).menu_highlight("two");
    check(bool(menu.enable_item("two", false)) && !enabled(ui, "two") && highlighted(ui) == "two", "enabling and disabling updates the open menu");
    check(ui.invoke_menu_item("two").status == Status::disabled && *ui.context_menu_open(), "a disabled item does not run and the menu stays open");
    check(bool(menu.add_item(command("zero", u"零", 0), "one")), "add before an item");
    check(ids_of(*ui.context_menu()).end()[-3] == "zero" && ids_of(*ui.context_menu()).end()[-2] == "one", "the new item appears in place");
    check(menu.add_item(command("one", u"重複", 0)).status == Status::invalid_value, "a duplicate id is refused");
    check(bool(menu.update_item({.id = "one", .label = u"新的一", .action = [&](const MenuContext&) { runs += 100; return Result{}; }})) &&
          find_item(*ui.context_menu(), "one")->label == u"新的一", "update replaces an item");
    check(bool(menu.remove_item("two")) && highlighted(ui).empty(), "removing the highlighted item clears the highlight");
    check(menu.remove_item("two").status == Status::not_found, "removing it twice says so");
    check(bool(ui.invoke_menu_item("one")) && runs == 100, "the updated action runs");
    check(menu.items()->size() == 2 && menu.items()->front().id == "zero", "items() lists the application's own items");
    ui.open_context_menu(field.handle);
    check(bool(menu.set_mode(MenuMode::off)) && !*ui.context_menu_open(), "turning the menu off closes it");
    menu.set_mode(MenuMode::append);
    ui.open_context_menu(field.handle);
    check(ui.invoke_menu_item("separator:2").status == Status::invalid_value, "a separator cannot run");
    check(ui.invoke_menu_item("nothing").status == Status::not_found, "an unknown id is not found");
    // An action may rewrite the menu; the next opening sees it.
    menu.update_item({.id = "one", .label = u"一", .action = [&](const MenuContext&) { return menu.remove_item("zero"); }});
    check(bool(ui.invoke_menu_item("one")), "an action can edit the menu");
    ui.open_context_menu(field.handle);
    check(find_item(*ui.context_menu(), "zero") == nullptr, "and the edit is in effect");
    ui.close_context_menu();
    // Failure from the application's code is the item's result, and the menu is closed regardless.
    menu.update_item({.id = "one", .label = u"一", .action = [](const MenuContext&) { return Result{Status::invalid_value, u"nope"}; }});
    ui.open_context_menu(field.handle);
    check(ui.invoke_menu_item("one").status == Status::invalid_value && !*ui.context_menu_open(), "the application's result is returned and the menu closes");
}

Frame layout_of(Ui& ui, int width, int height, ScrollState& scroll, float scale = 1.f) {
    test::FakeMeasure measure;
    std::vector<layout::Node> nodes;
    for (const auto& control : core(ui).controls())
        if (control.kind != ComponentKind::label && control.page != "dialog") nodes.push_back(layout::control(core(ui).layout_handle(control.id)));
    return arrange(layout::column(std::move(nodes), 8), ui, web_appearance(), measure, width, height, scale, scroll);
}

void menu_panel_sits_beside_the_pointer_flips_and_scrolls() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard);
    auto field = add_field(ui, "note", u"hello world");
    ScrollState scroll;
    auto frame = layout_of(ui, 600, 400, scroll);
    check(frame.menu_panel() == nullptr, "no menu, no panel");
    check(bool(ui.open_context_menu(field.handle, MenuPosition{100, 60})), "open at a point");
    frame = layout_of(ui, 600, 400, scroll);
    const auto* panel = frame.menu_panel();
    check(panel && panel->bounds.x == 100 && panel->bounds.y == 60, "the panel's corner is at the pointer");
    check(panel->menu_rows.size() == 9 && panel->menu_rows[2].separator && !panel->menu_rows[0].separator, "one row per item, lines included");
    check(panel->menu_maximum == 0 && inside(panel->bounds, {0, 0, 600, 400}), "it fits, so nothing scrolls");
    check(frame.hit(110, 70) != frame.menu_panel() && frame.menu_contains(110, 70) && frame.menu_item_at(110, 70) == "lcars.undo",
          "a point on the first row hits the first item (and the page's hit test never returns the menu)");
    check(!frame.menu_contains(99, 59) && !frame.menu_item_at(99, 59), "just outside is outside");
    const auto size = panel->bounds;
    ui.close_context_menu();
    ui.open_context_menu(field.handle, MenuPosition{590, 390});
    frame = layout_of(ui, 600, 400, scroll);
    panel = frame.menu_panel();
    check(panel && panel->bounds.x + panel->bounds.w == 590 && panel->bounds.y + panel->bounds.h == 390 && panel->bounds.w == size.w && panel->bounds.h == size.h,
          "at the bottom right corner it opens up and to the left, the same size");
    ui.close_context_menu();
    ui.open_context_menu(field.handle, MenuPosition{590, 50});
    frame = layout_of(ui, 600, 400, scroll);
    panel = frame.menu_panel();
    check(panel && panel->bounds.x + panel->bounds.w == 590 && panel->bounds.y == 50, "near the right edge only: left, still downwards");
    ui.close_context_menu();
    ui.open_context_menu(field.handle, MenuPosition{20, 395});
    frame = layout_of(ui, 600, 400, scroll);
    panel = frame.menu_panel();
    check(panel && panel->bounds.x == 20 && panel->bounds.y + panel->bounds.h == 395, "near the bottom only: up, still rightwards");
    // A window too small for the whole menu: the menu scrolls inside the window.
    ui.close_context_menu();
    ui.open_context_menu(field.handle, MenuPosition{30, 40});
    frame = layout_of(ui, 300, 120, scroll);
    panel = frame.menu_panel();
    check(panel && inside(panel->bounds, {0, 0, 300, 120}) && panel->menu_maximum > 0 && panel->menu_thumb.h > 0, "a small window gets a scrolling menu inside it");
    const int first_row_y = panel->menu_rows.front().bounds.y;
    check(frame.wheel(ui, panel->bounds.x + 5, panel->bounds.y + 5, -40), "the wheel scrolls the menu");
    frame = layout_of(ui, 300, 120, scroll);
    check(frame.menu_panel()->menu_rows.front().bounds.y < first_row_y, "the rows moved up");
    press(ui, Key::end);
    frame = layout_of(ui, 300, 120, scroll);
    panel = frame.menu_panel();
    const auto& last = panel->menu_rows.back();
    check(last.highlighted && inside(last.bounds, panel->menu_inner), "keyboard to the end reveals the last item");
    press(ui, Key::home);
    frame = layout_of(ui, 300, 120, scroll);
    check(frame.menu_panel()->menu_rows.front().highlighted && inside(frame.menu_panel()->menu_rows.front().bounds, frame.menu_panel()->menu_inner), "and Home brings back the first");
}

void keyboard_position_follows_the_caret_and_the_current_row() {
    Ui ui;
    auto field = add_field(ui, "note", u"hello world");
    auto list = ui.add_list({.id = "items", .kind = ComponentKind::list, .label = u"項目"}, {{{"a"}, u"甲"}, {{"b"}, u"乙"}, {{"c"}, u"丙"}}).value.value();
    list.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    ScrollState scroll;
    field.handle.select(5, 5); field.handle.focus();
    auto frame = layout_of(ui, 600, 400, scroll);
    const auto* item = frame.find(field.handle);
    check(bool(press(ui, Key::menu)), "open from the keyboard in a text field");
    frame = layout_of(ui, 600, 400, scroll);
    const auto* panel = frame.menu_panel();
    item = frame.find(field.handle);
    const int caret = item->caret_x(5), line_bottom = item->bounds.y + item->field.editor.y + item->field.editor.h;
    check(panel && panel->bounds.x == caret && panel->bounds.y == line_bottom, "the menu hangs from the caret, under its line");
    ui.close_context_menu();
    field.handle.select(0, 0);
    ui.open_context_menu(field.handle);
    frame = layout_of(ui, 600, 400, scroll);
    check(frame.menu_panel()->bounds.x == frame.find(field.handle)->caret_x(0), "and follows a moved caret");
    ui.close_context_menu();
    list.focus(); press(ui, Key::down);  // the cursor moves to the second row
    check(press(ui, Key::menu).status == Status::not_supported, "a list with no attachment has no menu to open");
    frame = layout_of(ui, 600, 400, scroll);
    const auto* rows = frame.find(list);
    panel = frame.menu_panel();
    check(panel == nullptr, "and no panel appears");
    check(ui.add_context_menu(list, {command("x", u"x(X)", u'X')}).status == Status::ok, "attach");
    check(bool(press(ui, Key::menu)), "open again");
    frame = layout_of(ui, 600, 400, scroll);
    rows = frame.find(list); panel = frame.menu_panel();
    check(panel && rows && ui.context_menu()->item && panel->bounds.y == rows->options[1].y && panel->bounds.x >= rows->options[1].x,
          "the menu hangs from the list's current row");
}

// A multi-line field (#79) has the same built-in menu, and a keyboard-opened one hangs from the caret's own line.
void multi_line_field_menu_hangs_from_the_caret_line() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard); clipboard.content = u"x\ny";
    auto model = std::make_shared<std::u16string>(u"first\nsecond\nthird");
    auto note = ui.add_text({.id = "note", .kind = ComponentKind::text, .label = u"說明", .value = *model, .multiline = true, .visible_lines = 3}).value.value();
    check(bool(note.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})) &&
          bool(note.connect_immediate([model](const Change<TextHandle>& change) { *model = change.value; return Result{}; })), "connect the multi-line field");
    ScrollState scroll;
    note.select(8, 8); note.focus();
    auto frame = layout_of(ui, 600, 400, scroll);
    check(bool(press(ui, Key::menu)), "the menu key opens the menu in a multi-line field");
    check(ids_of(*ui.context_menu()) == builtin_layout && enabled(ui, "lcars.paste"), "with the same built-in items");
    frame = layout_of(ui, 600, 400, scroll);
    const auto* field = frame.find(note);
    const auto caret = field->caret_rect(8);
    const auto* panel = frame.menu_panel();
    check(panel && caret.y > field->view.text.y && panel->bounds.x == caret.x && panel->bounds.y == caret.y + caret.h, "it hangs from the second line's caret, under that line");
    ui.close_context_menu();
    check(bool(core(ui).menu_pointer_open("note", std::nullopt, 2, {30, 30})) && core(ui).edit("note")->selection.caret == 2, "right button moves the caret to the point");
    ui.invoke_menu_item("lcars.paste");
    check(*model == u"fix\nyrst\nsecond\nthird", "paste inserts the clipboard text with its line break at the caret");
    // Copy and cut over a selection that spans lines: the same functions as the shortcuts, so the clipboard gets CRLF (#79).
    core(ui).select("note", 0, 5);   // "fix", the line break and "y"
    check(bool(core(ui).menu_pointer_open("note", std::nullopt, std::nullopt, {30, 30})) && enabled(ui, "lcars.copy") && enabled(ui, "lcars.cut"), "copy and cut are on with a multi-line selection");
    check(bool(ui.invoke_menu_item("lcars.copy")) && clipboard.content == u"fix\r\ny" && *model == u"fix\nyrst\nsecond\nthird", "copy writes CRLF and leaves the text");
    core(ui).select("note", 0, 5);
    check(bool(core(ui).menu_pointer_open("note", std::nullopt, std::nullopt, {30, 30})) && bool(ui.invoke_menu_item("lcars.cut")) && clipboard.content == u"fix\r\ny" && *model == u"rst\nsecond\nthird",
          "cut writes CRLF and removes the text across the line break");
    check(bool(core(ui).menu_pointer_open("note", std::nullopt, std::nullopt, {30, 30})) && bool(ui.invoke_menu_item("lcars.paste")) && *model == u"fix\nyrst\nsecond\nthird",
          "pasting the CRLF clipboard back gives LF line breaks again");
}

void semantic_tree_shows_menu_and_items_while_open() {
    Ui ui; MemoryClipboard clipboard; core(ui).set_clipboard(&clipboard);
    auto field = add_field(ui, "note", u"hello");
    auto& tree = ui.semantics();
    const auto menu_id = menu_node_id("note");
    {
        auto before = tree.children({"note"});
        check(!tree.get({menu_id}) && std::find(before->begin(), before->end(), NodeRef{menu_id}) == before->end(), "no menu node while closed");
    }
    check(has_verb(ui, "note", Verb::context_menu), "a text field offers the context_menu verb");
    check(bool(tree.perform({"note"}, Verb::context_menu)) && *ui.context_menu_open(), "perform(context_menu) opens the menu");
    auto menu = tree.get({menu_id});
    check(menu && menu->role == Role::menu && !menu->hidden && menu->enabled, "a menu node appears");
    auto children = tree.children({"note"});
    check(std::find(children->begin(), children->end(), NodeRef{menu_id}) != children->end(), "as a child of the component");
    auto items = tree.children({menu_id});
    check(items && items->size() == 9 && items->front() == NodeRef{menu_id, OptionValue{"lcars.undo"}}, "with one child per item");
    auto copy = tree.get({menu_id, OptionValue{"lcars.copy"}});
    check(copy && copy->role == Role::menu_item && copy->name == u"複製(C)" && copy->access_key == u"C" && copy->accelerator_key == u"Ctrl+C" && !copy->enabled,
          "a disabled item: role, name with its letter, AccessKey, accelerator hint, not enabled");
    check(std::find(copy->verbs.begin(), copy->verbs.end(), Verb::invoke) == copy->verbs.end() && tree.perform(copy->ref, Verb::invoke).status == Status::disabled,
          "it offers no invoke and refuses one");
    auto line = tree.get({menu_id, OptionValue{"separator:2"}});
    check(line && line->role == Role::menu_separator && !line->focusable, "a separator node");
    check(tree.perform({menu_id, OptionValue{"lcars.select_all"}}, Verb::focus).status == Status::ok && highlighted(ui) == "lcars.select_all", "focus on an item highlights it");
    check(tree.get({menu_id, OptionValue{"lcars.select_all"}})->focused && !tree.get({"note"})->focused, "keyboard focus belongs to the item, not the field");
    check(bool(tree.perform({menu_id, OptionValue{"lcars.select_all"}}, Verb::invoke)) && !*ui.context_menu_open() && !tree.get({menu_id}), "invoke runs the item and the nodes vanish");
    check(tree.get({"note"})->focused && tree.get({"note"})->text_selection->caret == 5 && tree.get({"note"})->text_selection->anchor == 0, "focus is back on the field, with everything selected");
    tree.perform({"note"}, Verb::context_menu);
    check(bool(tree.perform({menu_id}, Verb::cancel)) && !*ui.context_menu_open(), "cancel on the menu closes it");
    check(bool(tree.perform({"note"}, Verb::context_menu, MenuPosition{7, 9})) && *ui.context_menu_open(), "a position can be passed");
    check(tree.perform({"note"}, Verb::context_menu, 5).status == Status::invalid_value, "a wrong argument is refused");
    ui.close_context_menu();
    check(has_verb(ui, "note", Verb::context_menu), "the verb is still there after closing");
    // The typed call and the semantic call agree on the result vocabulary.
    field.handle.enable(false);
    check(tree.perform({"note"}, Verb::context_menu).status == ui.open_context_menu(field.handle).status, "disabled: the same status both ways");
}

void semantic_events_announce_opening_focus_and_closing() {
    Ui ui; auto field = add_field(ui, "note", u"hello");
    field.handle.focus();
    std::vector<std::pair<SemanticEventKind, NodeRef>> events;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { events.push_back({e.kind, e.ref}); });
    auto has = [&](SemanticEventKind kind, const NodeRef& ref) {
        return std::any_of(events.begin(), events.end(), [&](const auto& e) { return e.first == kind && e.second == ref; });
    };
    const auto menu_id = menu_node_id("note");
    ui.open_context_menu(field.handle);
    ui.semantics().dispatch_events();
    check(has(SemanticEventKind::menu_opened, {menu_id}), "opening is announced");
    events.clear();
    press(ui, Key::down);
    ui.semantics().dispatch_events();
    check(has(SemanticEventKind::focus_changed, {menu_id, OptionValue{"lcars.undo"}}), "highlighting the first item moves focus to it");
    events.clear();
    press(ui, Key::down);
    ui.semantics().dispatch_events();
    check(has(SemanticEventKind::focus_changed, {menu_id, OptionValue{"lcars.redo"}}), "and then to the next");
    events.clear();
    press(ui, Key::escape);
    ui.semantics().dispatch_events();
    check(has(SemanticEventKind::menu_closed, {menu_id}), "closing is announced");
    check(has(SemanticEventKind::focus_changed, {"note"}), "and focus returns to the field");
}
}

int main() {
    std::cout << std::unitbuf;
    static const test::Case cases[] = {
        {"attach modes: append, replace, off", attach_modes_append_replace_and_off},
        {"other components have no menu until one is attached", other_components_have_no_menu_until_one_is_attached},
        {"menu closes by Escape, Alt, F10 and deactivation", menu_closes_by_escape_alt_f10_and_deactivation},
        {"every way of closing a menu announces menu_closed", every_way_of_closing_announces_menu_closed},
        {"removing the component announces menu_closed", removing_the_component_announces_menu_closed},
        {"menu cannot open or survive when the component cannot act", menu_cannot_open_or_survive_when_the_component_cannot_act},
        {"list menu selects the row and hands it to the application", list_menu_selects_the_row_and_hands_it_to_the_application},
        {"items can change while the menu is open", items_can_change_while_the_menu_is_open},
        {"menu panel sits beside the pointer, flips and scrolls", menu_panel_sits_beside_the_pointer_flips_and_scrolls},
        {"keyboard position follows the caret and the current row", keyboard_position_follows_the_caret_and_the_current_row},
        {"multi-line field menu hangs from the caret line", multi_line_field_menu_hangs_from_the_caret_line},
        {"semantic tree shows menu and items while open", semantic_tree_shows_menu_and_items_while_open},
        {"semantic events announce opening, focus and closing", semantic_events_announce_opening_focus_and_closing},
        {"text field menu has the Windows edit items", text_field_menu_has_the_windows_edit_items},
        {"items are unavailable when they cannot act", items_are_unavailable_when_they_cannot_act},
        {"read-only field only copies and selects", read_only_field_only_copies_and_selects},
        {"built-in items match their shortcuts", built_in_items_match_their_shortcuts},
        {"rich field menu adds bold italic underline", rich_field_menu_adds_bold_italic_underline},
        {"style items match their shortcuts", style_items_match_their_shortcuts},
        {"underline shares its quick key with undo", underline_shares_its_quick_key_with_undo},
        {"keyboard moves, skips lines and wraps", keyboard_moves_skips_lines_and_wraps},
        {"quick keys run unique and cycle shared letters", quick_keys_run_unique_and_cycle_shared_letters},
    };
    return test::run("context_menu", cases);
}
