#include "core_internal.hpp"
// C01-C03 behavior through the public core interface only; no window, GPU or font backend.
#include "check.hpp"
#include "lcars/core.hpp"
#include <thread>
using namespace lcars;
using test::check;

namespace {
// Application-owned source fixtures give legacy host input tests explicit write timing.
void populate(Ui& ui, std::vector<Control> controls) {
    for (auto& control : controls) {
        if (control.kind == Kind::text) {
            auto model = std::make_shared<std::u16string>(control.value);
            auto field = ui.add_text(std::move(control));
            check(bool(field.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect fixture source");
            check(bool(field.connect_confirmed([](const TextChange&) { return Result{}; })), "Connect fixture write");
        } else if (control.kind == Kind::button) ui.add_button(std::move(control));
        else ui.add_label(std::move(control));
    }
}
std::vector<Control> name_form() {
    return {
        {.id = "name.label", .kind = Kind::label, .value = u"顯示名稱", .target = "name", .role = TextRole::name},
        {.id = "name", .kind = Kind::text, .value = u"測試使用者"},
        {.id = "name.help", .kind = Kind::label, .value = u"單行文字", .target = "name", .role = TextRole::description},
        {.id = "name.error", .kind = Kind::label, .target = "name", .role = TextRole::error},
        {.id = "note", .kind = Kind::label, .value = u"第一行\n第二行"},
    };
}

void labels_associate_by_target_id() {
    Ui ui({}); populate(ui, name_form());
    auto field = detail::access(ui).describe("name");
    check(field && field->name == u"顯示名稱", "Field name comes from its name label");
    check(field->description == u"單行文字", "Description label is queryable from the field");
    check(field->labels == std::vector<Id>{"name.label", "name.help", "name.error"}, "Field lists every associated label");
    check(!field->invalid && field->error.empty(), "No error initially");
    auto error = detail::access(ui).describe("name.error");
    check(error && error->target == "name" && error->role == TextRole::error, "Error label points at its field");
    check(detail::access(ui).describe("note")->value == u"第一行\n第二行", "Plain labels keep multi-line display text");
}

void field_error_has_one_source() {
    Ui ui({}); populate(ui, name_form());
    check(bool(detail::access(ui).set_error("name", u"請填寫顯示名稱")), "Owner sets a field error");
    auto field = detail::access(ui).describe("name");
    check(field->invalid && field->error == u"請填寫顯示名稱", "Field reports invalid with its message");
    check(detail::access(ui).text("name.error") == u"請填寫顯示名稱", "Error label displays the field error");
    check(detail::access(ui).set_error("note", u"x").status == Status::not_supported, "Labels cannot carry errors");
    check(detail::access(ui).set_error("missing", u"x").status == Status::not_found, "Unknown target is reported");
    check(bool(detail::access(ui).set_error("name", u"")) && !detail::access(ui).describe("name")->invalid && detail::access(ui).text("name.error").empty(),
          "Empty message clears the error and its label");
}

std::vector<Control> form_with_buttons() {
    auto controls = name_form();
    controls.push_back({.id = "apply", .kind = Kind::button, .label = u"套用"});
    controls.push_back({.id = "reset", .kind = Kind::button, .label = u"重設"});
    return controls;
}
KeyPress press(Key key, bool shift = false) { return {.key = key, .shift = shift}; }

void tab_order_skips_labels_and_disabled() {
    Ui ui({}); populate(ui, form_with_buttons());
    check(detail::access(ui).focused().empty(), "Nothing focused initially");
    std::vector<Id> forward;
    for (int i = 0; i < 4; ++i) { detail::access(ui).key_down(press(Key::tab)); forward.push_back(detail::access(ui).focused()); }
    check(forward == std::vector<Id>{"name", "apply", "reset", "name"}, "Tab walks fields and buttons, wraps, skips labels");
    detail::access(ui).key_down(press(Key::tab, true));
    check(detail::access(ui).focused() == "reset", "Shift+Tab walks backwards and wraps");
    check(detail::access(ui).describe("reset")->focused && !detail::access(ui).describe("name")->focused, "Focus is queryable");
    detail::access(ui).enable("apply", false);
    detail::access(ui).focus("name");
    detail::access(ui).key_down(press(Key::tab));
    check(detail::access(ui).focused() == "reset", "Disabled button leaves the Tab order");
    check(detail::access(ui).focus("apply").status == Status::disabled && detail::access(ui).focused() == "reset", "Disabled control refuses focus");
    check(detail::access(ui).focus("note").status == Status::not_supported, "Labels never take focus");
    check(detail::access(ui).focus("missing").status == Status::not_found, "Unknown focus target is reported");
}

void button_activates_once_per_press() {
    Ui ui({}); populate(ui, form_with_buttons());
    int applies = 0, resets = 0;
    detail::access(ui).bind("apply", [&] { ++applies; return Result{}; });
    detail::access(ui).bind("reset", [&] { ++resets; return Result{}; });
    detail::access(ui).focus("apply");
    detail::access(ui).key_down(press(Key::enter));
    detail::access(ui).key_down({.key = Key::enter, .repeat = true});
    detail::access(ui).key_up(Key::enter);
    check(applies == 1, "Enter activates on press; auto-repeat does not repeat it");
    detail::access(ui).key_down(press(Key::space));
    detail::access(ui).key_down({.key = Key::space, .repeat = true});
    check(applies == 1, "Space arms on press");
    detail::access(ui).key_up(Key::space);
    detail::access(ui).key_up(Key::space);
    check(applies == 2, "Space activates once on release; a stray release does nothing");
    detail::access(ui).pointer_down("apply");
    detail::access(ui).pointer_up("reset");
    check(applies == 2 && resets == 0, "Pointer released outside cancels and does not activate the other button");
    detail::access(ui).pointer_down("apply");
    detail::access(ui).pointer_up("apply");
    check(applies == 3, "Pointer pressed and released inside activates once");
    detail::access(ui).key_down(press(Key::space));
    detail::access(ui).cancel_input();
    detail::access(ui).key_up(Key::space);
    detail::access(ui).pointer_down("apply");
    detail::access(ui).cancel_input();
    detail::access(ui).pointer_up("apply");
    check(applies == 3, "Window deactivation or capture loss leaves nothing armed");
    detail::access(ui).key_down(press(Key::space));
    detail::access(ui).focus("reset");
    detail::access(ui).key_up(Key::space);
    check(applies == 3 && resets == 0, "Moving focus drops the armed press; the release is not delivered to the new focus");
    detail::access(ui).enable("apply", false);
    check(detail::access(ui).pointer_down("apply").status == Status::disabled, "Disabled button refuses pointer press");
    detail::access(ui).pointer_up("apply");
    check(detail::access(ui).invoke("apply").status == Status::disabled && applies == 3, "Disabled button never activates");
}

KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
std::vector<Control> two_fields() {
    auto controls = form_with_buttons();
    controls.push_back({.id = "alias", .kind = Kind::text, .value = u"別名"});
    return controls;
}

void typing_records_one_history_entry_per_input() {
    Ui ui({}); populate(ui, two_fields());
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 5, 5);
    for (auto ch : std::u16string(u"AB")) detail::access(ui).text_input(std::u16string(1, ch));
    detail::access(ui).text_input(u"\r");
    check(detail::access(ui).text("name") == u"測試使用者AB", "Committed characters insert at the caret; control characters are ignored");
    detail::access(ui).focus("alias");
    detail::access(ui).text_input(u"X");
    detail::access(ui).focus("name");
    detail::access(ui).key_down(ctrl(u'Z'));
    check(detail::access(ui).text("name") == u"測試使用者A" && detail::access(ui).text("alias") == u"X別名", "Ctrl+Z undoes one input of the focused field only");
    detail::access(ui).key_down(ctrl(u'Z', true));
    check(detail::access(ui).text("name") == u"測試使用者AB", "Ctrl+Shift+Z redoes");
    detail::access(ui).key_down(ctrl(u'Z'));
    detail::access(ui).key_down(ctrl(u'Z'));
    detail::access(ui).key_down(ctrl(u'Y'));
    check(detail::access(ui).text("name") == u"測試使用者A", "Ctrl+Y redoes");
    detail::access(ui).text_input(u"C");
    check(detail::access(ui).redo("name").status == Status::empty_history && detail::access(ui).text("name") == u"測試使用者AC", "A new edit clears the field's redo");
    check(detail::access(ui).undo("alias") && detail::access(ui).text("alias") == u"別名", "Each field keeps its own history");
}

Selection selection(Ui& ui, std::string_view id) { return detail::access(ui).edit(id)->selection; }
bool at(Ui& ui, unsigned anchor, unsigned caret) {
    auto s = selection(ui, detail::access(ui).focused());
    return s.anchor == anchor && s.caret == caret;
}

void caret_moves_by_code_point_and_extends_with_shift() {
    // "A", U+20000 (two UTF-16 units), "B", "中": UTF-16 length 5.
    Ui ui({}); populate(ui, {{.id = "t", .kind = Kind::text, .value = u"A\U00020000B中"}});
    detail::access(ui).focus("t");
    detail::access(ui).key_down(press(Key::home));
    check(at(ui, 0, 0), "Home puts the caret at the start");
    detail::access(ui).key_down(press(Key::right));
    detail::access(ui).key_down(press(Key::right));
    check(at(ui, 3, 3), "Right steps over a surrogate pair as one character");
    detail::access(ui).key_down(press(Key::right, true));
    detail::access(ui).key_down(press(Key::end, true));
    check(at(ui, 3, 5), "Shift extends from the anchor; Shift+End reaches the end");
    detail::access(ui).key_down(press(Key::left));
    check(at(ui, 3, 3), "Left without Shift collapses to the selection start");
    detail::access(ui).key_down(press(Key::backspace));
    check(detail::access(ui).text("t") == u"AB中" && at(ui, 1, 1), "Backspace removes the whole surrogate pair");
    detail::access(ui).key_down(press(Key::del));
    check(detail::access(ui).text("t") == u"A中", "Delete removes the next character");
    detail::access(ui).key_down(ctrl(u'A'));
    check(at(ui, 0, 2), "Ctrl+A selects all");
    detail::access(ui).key_down(press(Key::backspace));
    check(detail::access(ui).text("t").empty(), "Backspace removes a selection");
    detail::access(ui).undo("t"); detail::access(ui).undo("t"); detail::access(ui).undo("t");
    check(detail::access(ui).text("t") == u"A\U00020000B中", "Each deletion was one undo entry");
    check(detail::access(ui).select("t", 2, 2) && at(ui, 1, 1), "Semantic selection inside a surrogate pair snaps to its start");
}

struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    std::optional<std::u16string> read() override { return content; }
};

void paste_turns_each_line_break_into_one_space() {
    Ui ui({}); populate(ui, name_form());
    MemoryClipboard clipboard;
    detail::access(ui).set_clipboard(&clipboard);
    detail::access(ui).focus("name");
    clipboard.content = u"Alpha\r\nBeta";
    detail::access(ui).key_down(ctrl(u'A'));
    detail::access(ui).key_down(ctrl(u'V'));
    check(detail::access(ui).text("name") == u"Alpha Beta", "CRLF pastes as one space");
    detail::access(ui).key_down(ctrl(u'Z'));
    check(detail::access(ui).text("name") == u"測試使用者", "Select-all plus paste is one undo entry");
    detail::access(ui).key_down(ctrl(u'Y'));
    check(detail::access(ui).text("name") == u"Alpha Beta", "Redo restores the paste");
    check(detail::access(ui).paste("name", u"a\rb\nc\r\n\nd") && detail::access(ui).text("name") == u"Alpha Betaa b c  d",
          "Lone CR and LF each become one space; semantic paste inserts at the caret");
    detail::access(ui).select("name", 0, 5);
    detail::access(ui).key_down(ctrl(u'C'));
    check(clipboard.content == u"Alpha" && detail::access(ui).text("name") == u"Alpha Betaa b c  d", "Copy writes the selection only");
    detail::access(ui).key_down(ctrl(u'X'));
    check(clipboard.content == u"Alpha" && detail::access(ui).text("name") == u" Betaa b c  d", "Cut writes then removes the selection");
    detail::access(ui).undo("name");
    check(detail::access(ui).text("name") == u"Alpha Betaa b c  d", "Cut is one undo entry");
    check(detail::access(ui).paste("name", u"x\ty").status == Status::invalid_value && detail::access(ui).text("name") == u"Alpha Betaa b c  d",
          "Other control characters reject the whole paste and keep the text");
}

void read_only_allows_viewing_and_copying_only() {
    Ui ui({}); populate(ui, form_with_buttons());
    MemoryClipboard clipboard;
    detail::access(ui).set_clipboard(&clipboard);
    detail::access(ui).focus("name");
    detail::access(ui).text_input(u"!");
    detail::access(ui).set_read_only("name", true);
    check(detail::access(ui).describe("name")->read_only && detail::access(ui).focus("name"), "Read-only field stays focusable and reports its state");
    detail::access(ui).key_down(ctrl(u'A'));
    detail::access(ui).key_down(ctrl(u'C'));
    check(clipboard.content == u"!測試使用者", "Read-only text can be selected and copied");
    clipboard.content = u"貼上";
    std::vector<Status> refused;
    refused.push_back(detail::access(ui).text_input(u"Y").status);
    refused.push_back(detail::access(ui).key_down(ctrl(u'X')).status);
    refused.push_back(detail::access(ui).key_down(ctrl(u'V')).status);
    refused.push_back(detail::access(ui).key_down(press(Key::backspace)).status);
    refused.push_back(detail::access(ui).key_down(ctrl(u'Z')).status);
    refused.push_back(detail::access(ui).set_value("name", u"semantic").status);
    check(refused == std::vector<Status>(6, Status::read_only), "Every modifying path reports read_only");
    check(detail::access(ui).text("name") == u"!測試使用者" && clipboard.content == u"貼上", "Text and clipboard are untouched");
    detail::access(ui).set_read_only("name", false);
    check(detail::access(ui).undo("name") && detail::access(ui).text("name") == u"測試使用者", "History position survived the read-only period");
}

void disabled_field_refuses_every_entry_and_keeps_data() {
    Ui ui({}); populate(ui, form_with_buttons());
    int applies = 0;
    detail::access(ui).bind("apply", [&] { ++applies; return Result{}; });
    detail::access(ui).focus("name");
    detail::access(ui).text_input(u"!");
    detail::access(ui).enable("name", false);
    detail::access(ui).enable("apply", false);
    check(detail::access(ui).focused().empty(), "Disabling the focused control releases focus");
    std::vector<Status> refused{
        detail::access(ui).focus("name").status, detail::access(ui).pointer_down("name").status, detail::access(ui).select("name", 0, 1).status,
        detail::access(ui).copy("name").status, detail::access(ui).paste("name", u"x").status, detail::access(ui).undo("name").status,
        detail::access(ui).set_value("name", u"x").status, detail::access(ui).invoke("apply").status, detail::access(ui).pointer_down("apply").status};
    check(refused == std::vector<Status>(refused.size(), Status::disabled), "Pointer, keyboard and semantic entries all refuse");
    check(detail::access(ui).focus_order() == std::vector<Id>{"reset"}, "Only the recovery entry stays in the Tab order");
    detail::access(ui).enable("name", true);
    detail::access(ui).enable("apply", true);
    check(detail::access(ui).text("name") == u"!測試使用者" && applies == 0 && detail::access(ui).undo("name"), "Re-enabling keeps text and history");
}

void composition_commits_once_and_never_applies() {
    Ui ui({}); populate(ui, form_with_buttons());
    int applies = 0;
    detail::access(ui).bind("apply", [&] { ++applies; return Result{}; });
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 5, 5);
    detail::access(ui).composition_update(u"ㄨㄛˇ", 3);
    check(detail::access(ui).edit("name")->visible() == u"測試使用者ㄨㄛˇ" && detail::access(ui).text("name") == u"測試使用者",
          "Preedit is shown but not committed");
    check(detail::access(ui).key_down(ctrl(u'Z')).status == Status::busy && detail::access(ui).key_down(press(Key::backspace)).status == Status::busy,
          "Editing keys belong to the IME while composing");
    check(detail::access(ui).invoke("apply").status == Status::busy && applies == 0, "No command runs while composing");
    check(detail::access(ui).select("name", 0, 0).status == Status::busy && detail::access(ui).edit("name")->composing,
          "Moving the selection cannot silently drop a composition the IME still holds");
    detail::access(ui).composition_update(u"我", 1);
    detail::access(ui).composition_commit(u"我");
    detail::access(ui).key_down(press(Key::enter));
    check(detail::access(ui).text("name") == u"測試使用者我" && !detail::access(ui).edit("name")->composing && applies == 0,
          "Commit inserts once; neither commit nor a later Enter applies");
    detail::access(ui).undo("name");
    check(detail::access(ui).text("name") == u"測試使用者", "One commit is one undo entry");
    detail::access(ui).select("name", 0, 2);
    detail::access(ui).composition_update(u"工作", 2);
    detail::access(ui).composition_commit(u"工作");
    check(detail::access(ui).text("name") == u"工作使用者", "Commit replaces the selection");
    detail::access(ui).undo("name");
    detail::access(ui).composition_update(u"ㄅ", 1);
    detail::access(ui).composition_cancel();
    check(detail::access(ui).text("name") == u"測試使用者" && detail::access(ui).redo("name") && detail::access(ui).text("name") == u"工作使用者",
          "Cancel keeps committed text and history, including redo");
    detail::access(ui).select("name", 5, 5);
    detail::access(ui).composition_update(u"我", 1);
    detail::access(ui).focus("apply");
    check(detail::access(ui).text("name") == u"工作使用者我" && !detail::access(ui).edit("name")->composing && applies == 0,
          "E01: focus leaving commits the preedit once, the same default the native IME showed");
    detail::access(ui).focus("name");
    detail::access(ui).undo("name");
    check(detail::access(ui).text("name") == u"工作使用者", "The blur commit is one undo entry");
    detail::access(ui).focus("apply");
    check(detail::access(ui).composition_commit(u"遲到").status == Status::invalid_value && detail::access(ui).text("name") == u"工作使用者",
          "A commit with no composition and no focused field lands nowhere");
}

void focus_notifications_queue_instead_of_recursing() {
    Ui ui({}); populate(ui, form_with_buttons());
    std::vector<std::pair<Id, Id>> heard;
    int depth = 0, deepest = 0;
    detail::access(ui).on_focus([&](const Id& from, const Id& to) {
        deepest = std::max(deepest, ++depth);
        heard.emplace_back(from, to);
        if (to == "apply") check(bool(detail::access(ui).focus("reset")) && detail::access(ui).focused() == "reset", "A write inside the callback is readable at once");
        --depth;
    });
    detail::access(ui).focus("apply");
    check(deepest == 1, "The focus callback is never re-entered");
    check((heard == std::vector<std::pair<Id, Id>>{{"", "apply"}, {"apply", "reset"}}) && detail::access(ui).focused() == "reset",
          "The nested move is delivered after the current notification, in order");
    detail::access(ui).on_focus([](const Id&, const Id&) { throw std::runtime_error("owner bug"); });
    check(detail::access(ui).focus("name").status == Status::failed && detail::access(ui).stopped(), "A throwing focus callback is reported, not hidden");
}

void keyboard_editing_obeys_the_same_admission() {
    Ui ui({}); populate(ui, form_with_buttons());
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 2, 2);
    detail::access(ui).modal(true);
    check(detail::access(ui).key_down(press(Key::right)).status == Status::blocked_by_modal && at(ui, 2, 2), "Caret keys respect the modal block");
    check(detail::access(ui).key_down(ctrl(u'A')).status == Status::blocked_by_modal && at(ui, 2, 2), "Select-all respects the modal block");
    detail::access(ui).modal(false);
    MemoryClipboard clipboard;
    detail::access(ui).set_clipboard(&clipboard);
    detail::access(ui).text_input(u"!");
    detail::access(ui).undo("name");
    detail::access(ui).set_read_only("name", true);
    clipboard.content = u"x";
    std::vector<Status> refused{detail::access(ui).key_down(ctrl(u'Y')).status, detail::access(ui).key_down(press(Key::del)).status,
                                detail::access(ui).paste("name", u"x").status, detail::access(ui).cut("name").status, detail::access(ui).undo("name").status, detail::access(ui).redo("name").status};
    check(refused == std::vector<Status>(refused.size(), Status::read_only), "Read-only refuses redo, Delete and semantic edits alike");
    detail::access(ui).set_read_only("name", false);
    check(detail::access(ui).redo("name") && detail::access(ui).text("name") == u"測試!使用者", "The redo position survived");
}

void over_limit_edit_is_rejected_whole() {
    Ui ui({}); populate(ui, {{.id = "code", .kind = Kind::text, .value = u"AB", .max_length = 4}});
    check(Control{.id = "x", .kind = Kind::text}.max_length == default_max_text_length && default_max_text_length == 4096,
          "The default limit is public");
    detail::access(ui).focus("code");
    detail::access(ui).select("code", 2, 2);
    detail::access(ui).text_input(u"CD");
    check(detail::access(ui).text_input(u"E").status == Status::invalid_value, "Typing past the limit is rejected");
    check(detail::access(ui).paste("code", u"XYZ").status == Status::invalid_value, "Pasting past the limit is rejected");
    check(detail::access(ui).set_value("code", u"12345").status == Status::invalid_value, "Semantic writes obey the same limit");
    check(detail::access(ui).text("code") == u"ABCD" && detail::access(ui).undo("code") && detail::access(ui).text("code") == u"AB" && !detail::access(ui).undo("code"),
          "Rejected edits keep text and history");
    bool refused = false;
    try { Ui bad({{.id = "x", .kind = Kind::text, .value = u"toolong", .max_length = 3}}); }
    catch (const std::invalid_argument&) { refused = true; }
    check(refused, "Initial text must respect the field limit");
}

bool has(const std::vector<Operation>& actions, Operation operation) {
    return std::find(actions.begin(), actions.end(), operation) != actions.end();
}

void owner_hears_focus_moves_and_semantics_list_actions() {
    Ui ui({}); populate(ui, form_with_buttons());
    std::vector<std::pair<Id, Id>> moves;
    detail::access(ui).on_focus([&](const Id& from, const Id& to) {
        moves.emplace_back(from, to);
        if (from == "name") detail::access(ui).set_error("name", detail::access(ui).text("name").empty() ? u"必填" : u"");
    });
    detail::access(ui).focus("name");
    detail::access(ui).key_down(ctrl(u'A'));
    detail::access(ui).key_down(press(Key::backspace));
    detail::access(ui).key_down(press(Key::tab));
    check((moves == std::vector<std::pair<Id, Id>>{{"", "name"}, {"name", "apply"}}), "Owner hears each focus move once");
    check(detail::access(ui).describe("name")->error == u"必填", "Owner can validate on blur");
    auto field = detail::access(ui).describe("name")->operations;
    check(has(field, Operation::focus) && has(field, Operation::edit) && has(field, Operation::paste) && has(field, Operation::undo) &&
          !has(field, Operation::redo) && !has(field, Operation::activate), "Editable field lists what it can do now");
    detail::access(ui).set_read_only("name", true);
    field = detail::access(ui).describe("name")->operations;
    check(has(field, Operation::select) && has(field, Operation::copy) && !has(field, Operation::edit) && !has(field, Operation::undo),
          "Read-only field lists only viewing actions");
    check((detail::access(ui).describe("apply")->operations == std::vector<Operation>{Operation::focus, Operation::activate}), "Button can focus and activate");
    detail::access(ui).enable("apply", false);
    check(detail::access(ui).describe("apply")->operations.empty() && detail::access(ui).describe("note")->operations.empty(), "Disabled controls and labels offer nothing");
    int count = 0;
    detail::access(ui).bind_text("note", [&] { return u"套用 " + std::u16string(1, char16_t(u'0' + count)) + u" 次"; });
    count = 2;
    check(detail::access(ui).text("note") == u"套用 2 次" && detail::access(ui).describe("note")->value == u"套用 2 次", "Bound label reads the owner's data on demand");
}
}

void confirmed_field_respects_shared_command_guard() {
    Ui ui({});
    auto field = ui.add_text({.id = "guard.field", .kind = Kind::text});
    auto button = ui.add_button({.id = "guard.button", .kind = Kind::button});
    field.connect_source({[] { return std::u16string(u"source"); }, [](std::function<void()>) { return [] {}; }});
    int writes = 0;
    Status nested = Status::ok;
    field.connect_confirmed([&](const TextChange&) { ++writes; nested = button.invoke().status; return Result{}; });
    field.set_value(u"draft");
    int commands = 0;
    button.bind([&] { ++commands; return field.confirm(); });
    check(button.invoke().status == Status::busy && writes == 0 && commands == 1, "A command cannot nest a standalone confirmation");
    check(bool(field.confirm()) && writes == 1 && nested == Status::busy && commands == 1,
          "Standalone confirmation works and blocks nested commands");
}

void public_text_operations_obey_restrictions() {
    Ui ui({}); MemoryClipboard clipboard; detail::access(ui).set_clipboard(&clipboard);
    auto field = ui.add_text({.id = "public.text", .kind = Kind::text});
    field.connect_source({[] { return std::u16string(u"alpha"); }, [](std::function<void()>) { return [] {}; }});
    field.connect_confirmed([](const TextChange&) { return Result{}; });
    check(bool(field.select(0, 5)) && bool(field.copy()) && clipboard.content == u"alpha", "Public selection and copy");
    check(bool(field.cut()) && field.describe()->value.empty(), "Public cut");
    check(bool(field.undo()) && bool(field.redo()) && field.describe()->value.empty(), "Public undo and redo");
    field.read_only(true);
    check(field.cut().status == Status::read_only && field.redo().status == Status::read_only && bool(field.copy()),
          "Read-only public operations share admission rules");
}

void handle_queries_report_expiry() {
    TextHandle field; ButtonHandle button; LabelHandle label;
    {
        Ui ui({});
        field = ui.add_text({.id = "query.field", .kind = Kind::text});
        button = ui.add_button({.id = "query.button", .kind = Kind::button});
        label = ui.add_label({.id = "query.label", .kind = Kind::label});
        check(button.describe()->kind == Kind::button && label.describe()->kind == Kind::label,
              "Button and label expose semantic snapshots");
    }
    check(field.describe().status == Status::not_found && field.source_conflict().status == Status::not_found &&
          button.describe().status == Status::not_found && button.executing().status == Status::not_found &&
          label.describe().status == Status::not_found, "Expired queries return an explicit error");
}

void configuration_requires_ui_thread() {
    Ui ui({});
    auto field = ui.add_text({.id = "thread.field", .kind = Kind::text});
    auto label = ui.add_label({.id = "thread.label", .kind = Kind::label});
    field.connect_source({[] { return std::u16string(u"source"); }, [](std::function<void()>) { return [] {}; }});
    Status reset = Status::ok, connect = Status::ok;
    std::thread worker([&] {
        reset = field.reset_to_source().status;
        connect = label.connect_source([] { return std::u16string(u"wrong"); }).status;
    });
    worker.join();
    check(reset == Status::wrong_thread && connect == Status::wrong_thread, "Configuration rejects another thread");
    auto fault = ui.add_button({.id = "thread.fault", .kind = Kind::button});
    fault.bind([]() -> Result { throw std::runtime_error("failure"); });
    check(fault.invoke().status == Status::failed && field.reset_to_source().status == Status::failed &&
          label.connect_source([] { return std::u16string(u"late"); }).status == Status::failed,
          "Stopped UI rejects both configuration paths");
}

void group_action_cannot_be_replaced() {
    Ui ui({});
    auto field = ui.add_text({.id = "group.field", .kind = Kind::text});
    auto button = ui.add_button({.id = "group.button", .kind = Kind::button});
    field.connect_source({[] { return std::u16string(u"source"); }, [](std::function<void()>) { return [] {}; }});
    int submitted = 0, replacement = 0;
    check(bool(ui.connect_confirmation(button, {field}, [&](const auto&) { ++submitted; return ConfirmationResult{}; })), "Group connects");
    auto rebind = button.bind([&] { ++replacement; return Result{}; });
    check(rebind.status == Status::invalid_value, "Rebinding a confirmation button returns an error");
    check(bool(button.invoke()) && submitted == 1 && replacement == 0, "The original confirmation still runs");
}

void unconnected_text_rejects_edits() {
    Ui ui({});
    auto field = ui.add_text({.id = "unconnected", .kind = Kind::text, .value = u"initial"});
    auto snapshot = field.describe();
    check(field.set_value(u"changed").status == Status::invalid_value && snapshot->value == u"initial" &&
          !has(snapshot->operations, Operation::edit), "An unconnected field rejects edits and does not advertise editing");
}

void handles_expire_with_their_component_tree() {
    TextHandle field;
    {
        Ui ui({});
        field = ui.add_text({.id = "handle.field", .kind = Kind::text, .value = u"initial"});
        field.connect_source({[] { return std::u16string(u"initial"); }, [](std::function<void()>) { return [] {}; }});
        field.connect_confirmed([](const TextChange&) { return Result{}; });
        check(bool(field.set_value(u"edited")) && field.describe()->value == u"edited",
              "Typed handle edits and describes its field");
    }
    check(field.set_value(u"late").status == Status::not_found && !field.describe(),
          "A retained handle cannot keep the component tree alive or operate after its end");
    check(field.enable(false).status == Status::not_found && field.read_only(true).status == Status::not_found,
          "Configuration operations also report an expired component");
}

void source_updates_follow_the_component_lifetime() {
    std::u16string model = u"first";
    std::function<void()> changed;
    TextHandle field;
    {
        Ui ui({});
        field = ui.add_text({.id = "source.field", .kind = Kind::text});
        field.connect_source({[&] { return model; }, [&](std::function<void()> callback) {
            changed = std::move(callback);
            return [&] { changed = {}; };
        }});
        check(field.describe()->value == u"first", "A field reads its initial value from the app model");
        model = u"second";
        changed();
        check(field.describe()->value == u"second", "A source notification refreshes an unedited field");
    }
    check(!changed, "Ending the component tree disconnects its source subscription");
}

void source_conflict_keeps_draft_until_cancelled() {
    std::u16string model = u"original";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "conflict.field", .kind = Kind::text});
    field.connect_source({[&] { return model; }, [&](std::function<void()> callback) {
        changed = std::move(callback); return [&] { changed = {}; };
    }});
    auto confirm = ui.add_button({.id = "conflict.apply", .kind = Kind::button});
    ui.connect_confirmation(confirm, {field}, [](const std::vector<TextChange>&) { return ConfirmationResult{}; });
    field.set_value(u"draft");
    model = u"newest";
    changed();
    check(field.describe()->value == u"draft" && field.describe()->source_conflict && field.source_conflict().value.value_or(false),
          "A different source value preserves the draft and signals the conflict");
    field.enable(false);
    check(field.describe()->source_conflict, "Conflict remains visible in disabled semantics");
    field.enable(true);
    check(bool(field.use_latest()) && field.describe()->value == u"newest" && !field.source_conflict().value.value_or(false),
          "Choosing the latest value discards the draft and clears the conflict");
}

void confirmation_submits_draft_and_origin_and_keeps_history() {
    std::u16string model = u"before";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "confirm.field", .kind = Kind::text});
    auto apply = ui.add_button({.id = "confirm.apply", .kind = Kind::button});
    field.connect_source({[&] { return model; }, [&](std::function<void()> callback) {
        changed = std::move(callback); return [&] { changed = {}; };
    }});
    std::u16string received_origin;
    ui.connect_confirmation(apply, {field}, [&](const std::vector<TextChange>& changes) {
        received_origin = changes.at(0).origin;
        model = changes.at(0).value;
        changed();
        return ConfirmationResult{};
    });
    field.set_value(u"draft");
    check(bool(apply.invoke()) && model == u"draft" && received_origin == u"before",
          "One application entry receives the draft and its origin");
    check(bool(field.undo()) && field.describe()->value == u"before" && model == u"draft",
          "Undo after apply creates a draft without undoing the model change");
}

void rejected_group_keeps_drafts_and_focuses_first_error() {
    std::u16string a = u"one", b = u"two";
    std::function<void()> changed_a, changed_b;
    Ui ui({});
    auto first = ui.add_text({.id = "reject.first", .kind = Kind::text});
    auto second = ui.add_text({.id = "reject.second", .kind = Kind::text});
    auto apply = ui.add_button({.id = "reject.apply", .kind = Kind::button});
    first.connect_source({[&] { return a; }, [&](std::function<void()> f) { changed_a = std::move(f); return [&] { changed_a = {}; }; }});
    second.connect_source({[&] { return b; }, [&](std::function<void()> f) { changed_b = std::move(f); return [&] { changed_b = {}; }; }});
    ui.connect_confirmation(apply, {first, second}, [&](const std::vector<TextChange>& changes) {
        check(changes.size() == 2 && changes[0].value == u"bad one" && changes[1].value == u"bad two",
              "Both drafts arrive together");
        return ConfirmationResult{{Status::invalid_value, u"Invalid"}, {{second, u"second problem"}, {first, u"first problem"}}};
    });
    first.set_value(u"bad one");
    second.set_value(u"bad two");
    check(apply.invoke().status == Status::invalid_value && first.describe()->value == u"bad one" && second.describe()->value == u"bad two",
          "Rejection keeps all drafts");
    check(first.describe()->error == u"first problem" && second.describe()->error == u"second problem" && first.describe()->focused,
          "Rejection labels each field and focuses the first field in group order");
}

void confirmation_blocks_reentry_but_preserves_new_edits() {
    std::u16string model = u"start";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "busy.field", .kind = Kind::text});
    auto apply = ui.add_button({.id = "busy.apply", .kind = Kind::button});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    int calls = 0;
    std::u16string second_origin;
    ui.connect_confirmation(apply, {field}, [&](const std::vector<TextChange>& changes) {
        ++calls;
        if (calls == 1) {
            check(apply.executing().value.value_or(false) && apply.invoke().status == Status::busy, "An executing confirmation cannot re-trigger");
            check(bool(field.set_value(u"newer")), "The field remains editable while the action executes");
        } else second_origin = changes[0].origin;
        model = changes[0].value;
        changed();
        return ConfirmationResult{};
    });
    field.set_value(u"sent");
    auto accepted = apply.invoke();
    check(bool(accepted), "The first confirmation succeeds");
    check(!apply.executing().value.value_or(false) && calls == 1, "Execution state resets and callback runs once");
    check(model == u"sent", "The submitted value reaches the model");
    check(field.describe()->value == u"newer", "Edits during execution stay in the field");
    check(bool(apply.invoke()) && model == u"newer" && second_origin == u"sent",
          "The new draft starts from the submitted value");
}

void connected_text_requires_explicit_write_timing() {
    std::u16string model = u"source";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "timing.field", .kind = Kind::text});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    check(field.set_value(u"draft").status == Status::invalid_value && field.describe()->value == u"source",
          "A connected editable field without explicit write timing rejects editing");
}

void source_update_during_composition_keeps_preedit() {
    std::u16string model = u"one";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "ime.source", .kind = Kind::text});
    auto apply = ui.add_button({.id = "ime.apply", .kind = Kind::button});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    ui.connect_confirmation(apply, {field}, [](const std::vector<TextChange>&) { return ConfirmationResult{}; });
    field.focus();
    detail::access(ui).composition_update(u"preedit", 7);
    model = u"external";
    changed();
    check(detail::access(ui).edit("ime.source")->composing && detail::access(ui).edit("ime.source")->preedit == u"preedit",
          "Source updates never discard an active composition");
    detail::access(ui).composition_commit(u"text");
    check(field.describe()->value == u"textone" && field.source_conflict().value.value_or(false),
          "The committed composition remains a conflicting draft");
}

void editing_again_after_matching_the_source_uses_the_new_origin() {
    std::u16string model = u"old";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "origin.field", .kind = Kind::text});
    auto apply = ui.add_button({.id = "origin.apply", .kind = Kind::button});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    std::u16string sent_origin;
    ui.connect_confirmation(apply, {field}, [&](const std::vector<TextChange>& changes) {
        sent_origin = changes[0].origin;
        return ConfirmationResult{};
    });
    field.set_value(u"draft");
    model = u"latest"; changed();
    field.set_value(u"latest");
    field.set_value(u"another draft");
    apply.invoke();
    check(sent_origin == u"latest", "A fresh draft starts from the current source, not an older conflict origin");
}

void explicit_immediate_write_keeps_a_rejected_draft() {
    std::u16string model = u"initial";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "immediate.field", .kind = Kind::text});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    field.connect_immediate([&](const TextChange& change) {
        if (change.value == u"bad") return Result{Status::invalid_value, u"Rejected"};
        model = change.value;
        changed();
        return Result{};
    });
    check(bool(field.set_value(u"accepted")) && model == u"accepted", "Immediate writes reach the app modification entry");
    check(field.set_value(u"bad").status == Status::invalid_value && field.describe()->value == u"bad" &&
          field.describe()->error == u"Rejected" && model == u"accepted",
          "A rejected immediate write keeps the draft and its reason");
}

void cancelling_a_rejected_draft_clears_its_error_without_writing() {
    std::u16string model = u"saved";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "cancel.field", .kind = Kind::text});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    int writes = 0;
    field.connect_immediate([&](const TextChange&) { ++writes; return Result{Status::invalid_value, u"Rejected"}; });
    field.set_value(u"bad");
    check(bool(field.use_latest()) && field.describe()->value == u"saved" && !field.describe()->invalid && writes == 1,
          "Cancelling restores the source, clears its rejection, and makes no new write");
}

void source_notifications_request_a_repaint() {
    std::u16string model = u"before";
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "paint.field", .kind = Kind::text});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    int invalidations = 0;
    detail::access(ui).on_invalidate([&] { ++invalidations; });
    model = u"after";
    changed();
    check(invalidations == 1 && field.describe()->value == u"after",
          "An external model update makes the new value visible to the host");
}

void ungrouped_confirmed_connection_writes_only_on_confirm() {
    std::u16string model = u"before", origin;
    std::function<void()> changed;
    Ui ui({});
    auto field = ui.add_text({.id = "single.field", .kind = Kind::text});
    field.connect_source({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    field.connect_confirmed([&](const TextChange& change) {
        origin = change.origin;
        model = change.value;
        changed();
        return Result{};
    });
    check(bool(field.set_value(u"draft")) && model == u"before", "Confirmed mode retains the draft without an immediate write");
    check(bool(field.confirm()) && model == u"draft" && origin == u"before", "Confirm submits the draft and its origin");
}

void checkbox_toggles_a_connected_boolean() {
    bool model = true;
    Ui ui({});
    auto box = ui.add_checkbox({.id = "notice", .kind = Kind::checkbox, .label = u"Notices"});
    box.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    box.connect_immediate([&](const BoolChange& change) { model = change.value; return Result{}; });
    check(bool(box.toggle()) && !model && !*box.draft() && box.describe()->value == u"false",
          "Toggling changes the connected boolean and its semantic value exactly once");
}

void checkbox_input_and_disabled_state_agree() {
    bool model = false;
    Ui ui({});
    auto box = ui.add_checkbox({.id = "notice", .kind = Kind::checkbox, .label = u"Notices"});
    box.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    box.connect_immediate([&](const BoolChange& change) { model = change.value; return Result{}; });
    auto& host = detail::access(ui);
    box.focus(); host.key_down({.key = Key::space}); host.key_down({.key = Key::space, .repeat = true}); host.key_up(Key::space);
    check(model && box.describe()->checked == true, "Space changes the value and check mark once");
    box.enable(false);
    check(box.toggle().status == Status::disabled && model && box.describe()->checked == true,
          "A disabled checkbox keeps its value and check mark");
}

void radio_can_start_empty_and_skip_disabled_options() {
    std::optional<OptionValue> model;
    Ui ui({});
    auto group = ui.add_radio_group({.id = "mode", .kind = Kind::radio_group, .label = u"Mode"},
        {{{"normal"}, u"Normal", false}, {{"focus"}, u"Focus", true}, {{"offline"}, u"Offline", false}});
    group.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    group.connect_immediate([&](const OptionChange& change) { model = change.value; return Result{}; });
    check(!group.draft()->has_value(), "A radio group can initially have no selection");
    check(group.select({"normal"}).status == Status::disabled && !model, "Disabled options cannot be selected");
    group.focus();
    detail::access(ui).key_down({.key = Key::right});
    check(model == OptionValue{"focus"} && *group.draft() == model, "Direction selects the first enabled option");
    detail::access(ui).key_down({.key = Key::right});
    check(model == OptionValue{"focus"}, "At the end navigation does not wrap");
}

void mixed_confirmation_submits_one_batch() {
    std::u16string name = u"before";
    bool enabled = true;
    std::optional<OptionValue> mode = OptionValue{"normal"};
    Ui ui({});
    auto text = ui.add_text({.id = "name", .kind = Kind::text});
    auto box = ui.add_checkbox({.id = "enabled", .kind = Kind::checkbox});
    auto radio = ui.add_radio_group({.id = "mode", .kind = Kind::radio_group}, {{{"normal"}, u"Normal"}, {{"focus"}, u"Focus"}});
    auto apply = ui.add_button({.id = "apply", .kind = Kind::button});
    text.connect_source({[&] { return name; }, [](std::function<void()>) { return [] {}; }});
    box.connect_source({[&] { return enabled; }, [](std::function<void()>) { return [] {}; }});
    radio.connect_source({[&] { return mode; }, [](std::function<void()>) { return [] {}; }});
    int calls = 0;
    ui.connect_mixed_confirmation(apply, {text, box, radio}, [&](const std::vector<FieldChange>& changes) {
        ++calls;
        name = std::get<TextChange>(changes[0]).value;
        enabled = std::get<BoolChange>(changes[1]).value;
        mode = std::get<OptionChange>(changes[2]).value;
        return MixedConfirmationResult{};
    });
    text.set_value(u"after"); box.toggle(); radio.select({"focus"});
    check(name == u"before" && enabled && mode == OptionValue{"normal"}, "All three edits stay drafts before apply");
    check(bool(apply.invoke()) && calls == 1 && name == u"after" && !enabled && mode == OptionValue{"focus"},
          "Apply submits one batch containing all three value types");
}

void mixed_rejection_marks_each_field_and_focuses_first() {
    Ui ui({});
    auto text = ui.add_text({.id = "text", .kind = Kind::text});
    auto box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox});
    auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group}, {{{"a"}, u"A"}});
    auto apply = ui.add_button({.id = "apply", .kind = Kind::button});
    text.connect_source({[] { return std::u16string(u"old"); }, [](std::function<void()>) { return [] {}; }});
    box.connect_source({[] { return false; }, [](std::function<void()>) { return [] {}; }});
    radio.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    ui.connect_mixed_confirmation(apply, {text, box, radio}, [&](const auto&) {
        return MixedConfirmationResult{{Status::invalid_value, u"Rejected"}, {{radio, u"Radio error"}, {box, u"Box error"}, {text, u"Text error"}}};
    });
    text.set_value(u"draft"); box.toggle(); radio.select({"a"});
    check(apply.invoke().status == Status::invalid_value && text.describe()->focused,
          "Rejection focuses the first field in the group, regardless of error ordering");
    check(text.describe()->error == u"Text error" && box.describe()->error == u"Box error" && radio.describe()->error == u"Radio error",
          "Each field exposes its own rejection reason");
    check(*text.draft() == u"draft" && *box.draft() && *radio.draft() == OptionValue{"a"}, "Rejected drafts are retained");
}

void radio_all_disabled_remains_empty_and_tab_can_leave() {
    Ui ui({});
    auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group},
        {{{"a"}, u"A", false}, {{"b"}, u"B", false}});
    auto next = ui.add_button({.id = "next", .kind = Kind::button});
    radio.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    radio.connect_immediate([](const OptionChange&) { return Result{}; });
    radio.focus();
    detail::access(ui).key_down({.key = Key::right});
    detail::access(ui).key_down({.key = Key::left});
    check(!radio.draft()->has_value(), "No direction selects a disabled option");
    detail::access(ui).key_down({.key = Key::tab});
    check(next.describe()->focused, "Tab leaves an all-disabled group");
}

void radio_source_conflict_can_be_resolved() {
    std::optional<OptionValue> model = OptionValue{"a"};
    std::function<void()> changed;
    Ui ui({});
    auto group = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group}, {{{"a"}, u"A"}, {{"b"}, u"B"}, {{"c"}, u"C"}});
    auto apply = ui.add_button({.id = "apply", .kind = Kind::button});
    group.connect_source({[&] { return model; }, [&](std::function<void()> cb) { changed = std::move(cb); return [&] { changed = {}; }; }});
    ui.connect_mixed_confirmation(apply, {group}, [](const auto&) { return MixedConfirmationResult{}; });
    group.select({"b"}); model = OptionValue{"c"}; changed();
    check(*group.draft() == OptionValue{"b"} && group.describe()->source_conflict, "A new source does not overwrite the draft and is reported");
    check(bool(group.use_latest()) && *group.draft() == OptionValue{"c"} && !group.describe()->source_conflict,
          "The latest source can replace the draft explicitly");
}

void standalone_boolean_and_option_confirmation() {
    bool enabled = false;
    std::optional<OptionValue> mode = OptionValue{"a"};
    Ui ui({});
    auto box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox});
    auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group}, {{{"a"}, u"A"}, {{"b"}, u"B"}});
    box.connect_source({[&] { return enabled; }, [](std::function<void()>) { return [] {}; }});
    radio.connect_source({[&] { return mode; }, [](std::function<void()>) { return [] {}; }});
    box.connect_confirmed([&](const BoolChange& c) { enabled = c.value; return Result{}; });
    radio.connect_confirmed([&](const OptionChange& c) { mode = c.value; return Result{}; });
    box.toggle(); radio.select({"b"});
    check(!enabled && mode == OptionValue{"a"}, "Standalone confirmed connections keep drafts until confirmed");
    check(bool(box.confirm()) && bool(radio.confirm()) && enabled && mode == OptionValue{"b"}, "Each explicit confirm writes its typed draft");
}

void unconnected_selection_controls_do_not_advertise_writes() {
    Ui ui({});
    auto box = ui.add_checkbox({.id = "box", .kind = Kind::checkbox});
    auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group}, {{{"a"}, u"A"}});
    auto has = [](const Semantics& s, Operation op) { return std::find(s.operations.begin(), s.operations.end(), op) != s.operations.end(); };
    check(!has(*box.describe(), Operation::activate) && !has(*radio.describe(), Operation::select),
          "Unconnected controls do not advertise unavailable modifications");
    box.connect_source({[] { return false; }, [](std::function<void()>) { return [] {}; }});
    radio.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    box.connect_immediate([](const BoolChange&) { return Result{}; });
    radio.connect_immediate([](const OptionChange&) { return Result{}; });
    check(has(*box.describe(), Operation::activate) && has(*radio.describe(), Operation::select),
          "Connected editable controls advertise their available modifications");
}

void slider_connected_draft_is_confirmed_as_a_number() {
    int model = 40;
    Ui ui({});
    auto slider = ui.add_slider({.id = "volume", .kind = Kind::slider, .label = u"Volume"}, {0, 100, 5});
    auto apply = ui.add_button({.id = "apply.slider", .kind = Kind::button});
    slider.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    ui.connect_mixed_confirmation(apply, {slider}, [&](const std::vector<FieldChange>& changes) {
        model = std::get<NumericChange>(changes[0]).value;
        return MixedConfirmationResult{};
    });
    check(bool(slider.set_value(55)) && *slider.draft() == 55 && model == 40 && slider.describe()->value == u"55",
          "The numeric edit and semantic value are drafts until apply");
    check(bool(apply.invoke()) && model == 55, "Apply submits the numeric draft through the application callback");
}

void slider_non_divisible_range_and_keyboard() {
    Ui ui({});
    auto slider = ui.add_slider({.id = "level", .kind = Kind::slider}, {-3, 8, 4});
    slider.connect_source({[] { return -3; }, [](std::function<void()>) { return [] {}; }});
    slider.connect_confirmed([](const NumericChange&) { return Result{}; });
    slider.set_value(3);
    check(*slider.draft() == 5, "A midpoint snaps to the higher step");
    slider.set_value(7);
    check(*slider.draft() == 8, "The shorter final interval snaps to the maximum");
    slider.focus();
    detail::access(ui).key_down({.key = Key::left});
    check(*slider.draft() == 5, "Left from the maximum reaches the last regular step");
    detail::access(ui).key_down({.key = Key::right});
    detail::access(ui).key_down({.key = Key::right});
    check(*slider.draft() == 8, "Right reaches and stops at the maximum");
    detail::access(ui).key_down({.key = Key::home});
    check(*slider.draft() == -3, "Home reaches the minimum");
    detail::access(ui).key_down({.key = Key::end});
    check(*slider.draft() == 8, "End reaches the non-divisible maximum");
    slider.enable(false);
    check(slider.set_value(1).status == Status::disabled && *slider.draft() == 8, "Disabled edits are rejected");
}

void slider_rejects_invalid_ranges() {
    Ui ui({}); int rejected = 0;
    for (auto range : {SliderRange{1, 1, 1}, SliderRange{2, 1, 1}, SliderRange{0, 1, 0}, SliderRange{0, 1, -1}}) {
        try { ui.add_slider({.id = "invalid", .kind = Kind::slider}, range); } catch (const std::invalid_argument&) { ++rejected; }
    }
    check(rejected == 4, "Empty or reversed ranges and non-positive steps are rejected");
    bool missing_range = false;
    try { Ui invalid({{.id = "missing", .kind = Kind::slider}}); } catch (const std::invalid_argument&) { missing_range = true; }
    check(missing_range, "A slider without a range cannot be created through the untyped constructor");
}

void slider_source_conflict_and_invalid_updates() {
    int model = 40; std::function<void()> changed; int origin = -1;
    SliderHandle saved;
    {
        Ui ui({});
        saved = ui.add_slider({.id = "volume", .kind = Kind::slider}, {0, 100, 5});
        auto apply = ui.add_button({.id = "send", .kind = Kind::button});
        saved.connect_source({[&] { return model; }, [&](std::function<void()> cb) { changed = std::move(cb); return [&] { changed = {}; }; }});
        ui.connect_mixed_confirmation(apply, {saved}, [&](const auto& changes) {
            origin = std::get<NumericChange>(changes[0]).origin;
            return MixedConfirmationResult{{Status::invalid_value, u"No"}, {{saved, u"Volume rejected"}}};
        });
        saved.set_value(55); model = 70; changed();
        check(*saved.draft() == 55 && saved.describe()->source_conflict, "External updates retain a conflicting numeric draft");
        check(apply.invoke().status == Status::invalid_value && origin == 40 && saved.describe()->error == u"Volume rejected" && *saved.draft() == 55,
              "A rejected group retains the draft, origin, field error and focus");
        check(bool(saved.use_latest()) && *saved.draft() == 70 && !saved.describe()->source_conflict, "Use latest restores the numeric source");
        model = 72; changed();
        check(*saved.draft() == 70 && saved.describe()->invalid, "An invalid notified source value cannot escape the range contract");
        model = 75; changed();
        check(*saved.draft() == 75 && !saved.describe()->invalid, "A valid source update clears its earlier source error");
    }
    check(!changed && saved.draft().status == Status::not_found, "Destroying the tree disconnects the numeric source and expires the handle");
}

void slider_group_preserves_edits_during_submission() {
    int model = 40, submitted_origin = -1; std::function<void()> changed;
    Ui ui({});
    auto slider = ui.add_slider({.id = "volume", .kind = Kind::slider}, {0, 100, 5});
    auto apply = ui.add_button({.id = "apply", .kind = Kind::button});
    slider.connect_source({[&] { return model; }, [&](std::function<void()> cb) { changed = std::move(cb); return [&] { changed = {}; }; }});
    int calls = 0;
    ui.connect_mixed_confirmation(apply, {slider}, [&](const std::vector<FieldChange>& changes) {
        const auto change = std::get<NumericChange>(changes[0]);
        ++calls; submitted_origin = change.origin;
        if (calls == 1) {
            check(apply.invoke().status == Status::busy, "A running confirmation cannot be reentered");
            slider.set_value(65);
        }
        model = change.value; changed();
        return MixedConfirmationResult{};
    });
    slider.set_value(55); apply.invoke();
    check(model == 55 && *slider.draft() == 65, "An edit during submission survives as a new draft");
    apply.invoke();
    check(model == 65 && submitted_origin == 55, "The next submission starts from the accepted value");
}

void dropdown_candidate_is_not_a_draft_until_confirmed() {
    std::optional<OptionValue> model = OptionValue{"north"};
    Ui ui({});
    auto region = ui.add_dropdown({.id = "region", .kind = Kind::dropdown},
        {{{"north"}, u"North"}, {{"south"}, u"South"}});
    check(bool(region.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }})), "Connect region source");
    check(bool(region.connect_confirmed([](const DropdownChange&) { return Result{}; })), "Connect confirmed write");
    check(bool(region.focus()) && bool(region.open()), "Open the dropdown");
    detail::access(ui).key_down({.key = Key::down});
    check(*region.draft() == OptionValue{"north"} && region.describe()->candidate == OptionValue{"south"},
          "Arrow changes the candidate only");
    detail::access(ui).key_down({.key = Key::escape});
    check(*region.draft() == OptionValue{"north"} && !region.describe()->expanded, "Escape cancels the candidate");
    region.open(); detail::access(ui).key_down({.key = Key::down}); detail::access(ui).key_down({.key = Key::enter});
    check(*region.draft() == OptionValue{"south"} && model == OptionValue{"north"}, "Enter confirms only the draft");
}

void dropdown_leaving_and_replaced_options_are_safe() {
    Ui ui({});
    auto dropdown = ui.add_dropdown({.id = "region", .kind = Kind::dropdown},
        {{{"north"}, u"North"}, {{"south"}, u"South"}});
    auto next = ui.add_button({.id = "next", .kind = Kind::button});
    int clicks = 0; next.bind([&] { ++clicks; return Result{}; });
    dropdown.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
    dropdown.connect_confirmed([](const DropdownChange&) { return Result{}; });
    auto& host = detail::access(ui);
    dropdown.open(); host.key_down({.key = Key::down}); host.key_down({.key = Key::tab});
    check(!dropdown.describe()->expanded && *dropdown.draft() == OptionValue{"north"} && next.describe()->focused, "Tab cancels and advances");
    dropdown.open(); host.key_down({.key = Key::down}); host.pointer_down("next"); host.pointer_up("next");
    check(!dropdown.describe()->expanded && *dropdown.draft() == OptionValue{"north"} && clicks == 1 && next.describe()->focused, "Outside click cancels and preserves the clicked action");
    dropdown.open(); host.pointer_down("region", 1);
    dropdown.set_options({{{"south"}, u"South"}, {{"north"}, u"North"}});
    host.pointer_up("region", 1);
    check(*dropdown.draft() == OptionValue{"north"}, "Reordered options cannot turn a press into a different selection");
    dropdown.set_options({});
    check(!*dropdown.draft() && !dropdown.describe()->candidate, "Empty options clear value and candidate");
    dropdown.set_options({{{"north"}, u"North"}, {{"south"}, u"South"}});
    check(!*dropdown.draft() && !dropdown.describe()->candidate, "Restoring options does not restore an invalidated value or candidate");
    dropdown.enable(false);
    check(dropdown.open().status == Status::disabled && dropdown.select({"north"}).status == Status::disabled, "Disabled dropdown refuses opening and selection");
    dropdown.enable(true); host.modal(true);
    check(dropdown.open().status == Status::blocked_by_modal && dropdown.select({"north"}).status == Status::blocked_by_modal, "Modal state blocks opening and selection");
}

void dropdown_option_changes_do_not_recurse_into_notifications() {
    Ui ui({});
    auto field = ui.add_dropdown({.id = "region", .kind = Kind::dropdown}, {{{"north"}, u"North"}});
    field.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
    field.connect_confirmed([](const DropdownChange&) { return Result{}; });
    int calls = 0; Status nested = Status::ok;
    ui.on_change([&] { if (++calls == 1) nested = field.set_options({}).status; });
    field.set_options({{{"north"}, u"North"}, {{"south"}, u"South"}});
    check(calls == 1 && nested == Status::busy && field.describe()->options.size() == 2,
          "An option update during a notification is refused without recursion or mutation");
}

void dropdown_programmatic_edits_request_a_frame() {
    Ui ui({});
    auto field = ui.add_dropdown({.id = "region", .kind = Kind::dropdown}, {{{"north"}, u"North"}, {{"south"}, u"South"}});
    field.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
    field.connect_confirmed([](const DropdownChange&) { return Result{}; });
    int frames = 0; detail::access(ui).on_invalidate([&] { ++frames; });
    field.select({"south"});
    check(frames > 0, "A public selection requests repaint");
    frames = 0; field.use_latest();
    check(frames > 0, "Restoring the source requests repaint");
}

void tabs_require_explicit_activation_and_guard_hidden_pages() {
    Ui ui({});
    auto first = ui.add_button({.id = "first", .kind = Kind::button});
    auto second = ui.add_button({.id = "second", .kind = Kind::button});
    int clicks = 0;
    second.bind([&] { ++clicks; return Result{}; });
    auto tabs = ui.add_tabs({.id = "tabs", .kind = Kind::tabs}, {
        {{"one"}, u"One", true, {first}}, {{"blocked"}, u"Blocked", false, {}},
        {{"two"}, u"Two", true, {second}}});
    auto& host = detail::access(ui);
    check(second.invoke().status == Status::hidden, "Hidden page refuses public activation");
    check(tabs.activate({"blocked"}).status == Status::disabled, "Disabled tab refuses activation");
    tabs.focus(); host.key_down({.key = Key::right});
    check(*tabs.active() == OptionValue{"one"} && tabs.describe()->candidate == OptionValue{"two"}, "Arrow skips disabled tab without activating");
    host.key_down({.key = Key::enter});
    check(*tabs.active() == OptionValue{"two"} && bool(second.invoke()) && clicks == 1, "Enter activates the focused page");
    check(first.invoke().status == Status::hidden, "Previous page is hidden");
}

void cross_page_confirmation_reveals_first_error_and_marks_other_pages() {
    Ui ui({});
    auto first = ui.add_text({.id = "first-field", .kind = Kind::text});
    auto second = ui.add_text({.id = "second-field", .kind = Kind::text});
    auto submit = ui.add_button({.id = "submit", .kind = Kind::button});
    auto source = TextSource{[] { return std::u16string(u"value"); }, [](std::function<void()>) { return [] {}; }};
    first.connect_source(source); second.connect_source(source);
    auto tabs = ui.add_tabs({.id = "pages", .kind = Kind::tabs}, {
        {{"one"}, u"One", true, {first, submit}}, {{"two"}, u"Two", true, {second}}});
    ui.connect_confirmation(submit, {second, first}, [=](const auto&) {
        return ConfirmationResult{{Status::invalid_value, u"Invalid"}, {{first, u"First error"}, {second, u"Second error"}}};
    });
    check(submit.invoke().status == Status::invalid_value, "Application rejects the batch");
    check(*tabs.active() == OptionValue{"two"} && second.describe()->focused, "First invalid field's page opens and receives focus");
    check(tabs.describe()->options[0].invalid && first.describe()->invalid && second.describe()->invalid,
          "Errors on the other page remain discoverable from its tab");
}

int main() {
    const test::Case cases[] = {
        {"G11 cross-page rejection", cross_page_confirmation_reveals_first_error_and_marks_other_pages},
        {"C09 manual tabs and hidden admission", tabs_require_explicit_activation_and_guard_hidden_pages},
        {"C06 candidate and draft are separate", dropdown_candidate_is_not_a_draft_until_confirmed},
        {"F07 dropdown exit and option identity", dropdown_leaving_and_replaced_options_are_safe},
        {"G07 dropdown changes do not recurse", dropdown_option_changes_do_not_recurse_into_notifications},
        {"G07 dropdown programmatic edits repaint", dropdown_programmatic_edits_request_a_frame},
        {"G11 edits during numeric submission", slider_group_preserves_edits_during_submission},
        {"G10 numeric source and conflict lifecycle", slider_source_conflict_and_invalid_updates},
        {"N04 non-divisible steps and keys", slider_non_divisible_range_and_keyboard},
        {"N04 invalid slider ranges", slider_rejects_invalid_ranges},
        {"C07 numeric draft and confirmation", slider_connected_draft_is_confirmed_as_a_number},
        {"G04 selection operations reflect write admission", unconnected_selection_controls_do_not_advertise_writes},
        {"G10 standalone typed confirmations", standalone_boolean_and_option_confirmation},
        {"G11 radio conflict and latest", radio_source_conflict_can_be_resolved},
        {"G11 mixed rejection", mixed_rejection_marks_each_field_and_focuses_first},
        {"N03 all disabled options", radio_all_disabled_remains_empty_and_tab_can_leave},
        {"G11 mixed confirmation batch", mixed_confirmation_submits_one_batch},
        {"C05 empty selection and disabled options", radio_can_start_empty_and_skip_disabled_options},
        {"C04 keyboard and disabled semantics", checkbox_input_and_disabled_state_agree},
        {"C04 connected checkbox toggles", checkbox_toggles_a_connected_boolean},
        {"Standalone confirmation shares command guard", confirmed_field_respects_shared_command_guard},
        {"Public text operations obey restrictions", public_text_operations_obey_restrictions},
        {"Handle queries report expiry", handle_queries_report_expiry},
        {"Configuration requires UI thread", configuration_requires_ui_thread},
        {"G11 confirmation action cannot be replaced", group_action_cannot_be_replaced},
        {"G10 unconnected text rejects edits", unconnected_text_rejects_edits},
        {"G10 standalone confirmed connection", ungrouped_confirmed_connection_writes_only_on_confirm},
        {"G10 source notifications request a repaint", source_notifications_request_a_repaint},
        {"G10 cancel clears rejection without writing", cancelling_a_rejected_draft_clears_its_error_without_writing},
        {"G10 explicit immediate write retains rejected draft", explicit_immediate_write_keeps_a_rejected_draft},
        {"G11 a fresh draft starts at the current source", editing_again_after_matching_the_source_uses_the_new_origin},
        {"G11 source updates preserve composition", source_update_during_composition_keeps_preedit},
        {"G10 connected text requires explicit write timing", connected_text_requires_explicit_write_timing},
        {"G11 confirmation blocks reentry but preserves new edits", confirmation_blocks_reentry_but_preserves_new_edits},
        {"G11 rejected group retains drafts and focuses first error", rejected_group_keeps_drafts_and_focuses_first_error},
        {"G11 confirmation submits draft and origin and keeps history", confirmation_submits_draft_and_origin_and_keeps_history},
        {"G11 source conflicts preserve drafts until cancel", source_conflict_keeps_draft_until_cancelled},
        {"G10 source updates follow component lifetime", source_updates_follow_the_component_lifetime},
        {"G10 typed handles expire with their component tree", handles_expire_with_their_component_tree},
        {"C01 labels associate by target id", labels_associate_by_target_id},
        {"C01 field error has one source", field_error_has_one_source},
        {"AC11 tab order skips labels and disabled", tab_order_skips_labels_and_disabled},
        {"AC06 button activates once per press", button_activates_once_per_press},
        {"AC09 typing records one history entry per input", typing_records_one_history_entry_per_input},
        {"AC08 caret moves by code point and extends with Shift", caret_moves_by_code_point_and_extends_with_shift},
        {"AC09 paste turns each line break into one space", paste_turns_each_line_break_into_one_space},
        {"AC10 read-only allows viewing and copying only", read_only_allows_viewing_and_copying_only},
        {"AC10 disabled field refuses every entry and keeps data", disabled_field_refuses_every_entry_and_keeps_data},
        {"AC14 composition commits once and never applies", composition_commits_once_and_never_applies},
        {"G07 focus notifications queue instead of recursing", focus_notifications_queue_instead_of_recursing},
        {"AC10 keyboard editing obeys the same admission", keyboard_editing_obeys_the_same_admission},
        {"AC08 over-limit edit is rejected whole", over_limit_edit_is_rejected_whole},
        {"AC11 owner hears focus moves and semantics list actions", owner_hears_focus_moves_and_semantics_list_actions},
    };
    return test::run("components", cases);
}
