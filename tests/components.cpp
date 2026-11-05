// C01-C03 behavior through the public core interface only; no window, GPU or font backend.
#include "check.hpp"
#include "lcars/core.hpp"
using namespace lcars;
using test::check;

namespace {
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
    Ui ui(name_form());
    auto field = ui.describe("name");
    check(field && field->name == u"顯示名稱", "Field name comes from its name label");
    check(field->description == u"單行文字", "Description label is queryable from the field");
    check(field->labels == std::vector<Id>{"name.label", "name.help", "name.error"}, "Field lists every associated label");
    check(!field->invalid && field->error.empty(), "No error initially");
    auto error = ui.describe("name.error");
    check(error && error->target == "name" && error->role == TextRole::error, "Error label points at its field");
    check(ui.describe("note")->value == u"第一行\n第二行", "Plain labels keep multi-line display text");
}

void field_error_has_one_source() {
    Ui ui(name_form());
    check(bool(ui.set_error("name", u"請填寫顯示名稱")), "Owner sets a field error");
    auto field = ui.describe("name");
    check(field->invalid && field->error == u"請填寫顯示名稱", "Field reports invalid with its message");
    check(ui.text("name.error") == u"請填寫顯示名稱", "Error label displays the field error");
    check(ui.set_error("note", u"x").status == Status::not_supported, "Labels cannot carry errors");
    check(ui.set_error("missing", u"x").status == Status::not_found, "Unknown target is reported");
    check(bool(ui.set_error("name", u"")) && !ui.describe("name")->invalid && ui.text("name.error").empty(),
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
    Ui ui(form_with_buttons());
    check(ui.focused().empty(), "Nothing focused initially");
    std::vector<Id> forward;
    for (int i = 0; i < 4; ++i) { ui.key_down(press(Key::tab)); forward.push_back(ui.focused()); }
    check(forward == std::vector<Id>{"name", "apply", "reset", "name"}, "Tab walks fields and buttons, wraps, skips labels");
    ui.key_down(press(Key::tab, true));
    check(ui.focused() == "reset", "Shift+Tab walks backwards and wraps");
    check(ui.describe("reset")->focused && !ui.describe("name")->focused, "Focus is queryable");
    ui.enable("apply", false);
    ui.focus("name");
    ui.key_down(press(Key::tab));
    check(ui.focused() == "reset", "Disabled button leaves the Tab order");
    check(ui.focus("apply").status == Status::disabled && ui.focused() == "reset", "Disabled control refuses focus");
    check(ui.focus("note").status == Status::not_supported, "Labels never take focus");
    check(ui.focus("missing").status == Status::not_found, "Unknown focus target is reported");
}

void button_activates_once_per_press() {
    Ui ui(form_with_buttons());
    int applies = 0, resets = 0;
    ui.bind("apply", [&] { ++applies; return Result{}; });
    ui.bind("reset", [&] { ++resets; return Result{}; });
    ui.focus("apply");
    ui.key_down(press(Key::enter));
    ui.key_down({.key = Key::enter, .repeat = true});
    ui.key_up(Key::enter);
    check(applies == 1, "Enter activates on press; auto-repeat does not repeat it");
    ui.key_down(press(Key::space));
    ui.key_down({.key = Key::space, .repeat = true});
    check(applies == 1, "Space arms on press");
    ui.key_up(Key::space);
    ui.key_up(Key::space);
    check(applies == 2, "Space activates once on release; a stray release does nothing");
    ui.pointer_down("apply");
    ui.pointer_up("reset");
    check(applies == 2 && resets == 0, "Pointer released outside cancels and does not activate the other button");
    ui.pointer_down("apply");
    ui.pointer_up("apply");
    check(applies == 3, "Pointer pressed and released inside activates once");
    ui.key_down(press(Key::space));
    ui.cancel_input();
    ui.key_up(Key::space);
    ui.pointer_down("apply");
    ui.cancel_input();
    ui.pointer_up("apply");
    check(applies == 3, "Window deactivation or capture loss leaves nothing armed");
    ui.key_down(press(Key::space));
    ui.focus("reset");
    ui.key_up(Key::space);
    check(applies == 3 && resets == 0, "Moving focus drops the armed press; the release is not delivered to the new focus");
    ui.enable("apply", false);
    check(ui.pointer_down("apply").status == Status::disabled, "Disabled button refuses pointer press");
    ui.pointer_up("apply");
    check(ui.invoke("apply").status == Status::disabled && applies == 3, "Disabled button never activates");
}

KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
std::vector<Control> two_fields() {
    auto controls = form_with_buttons();
    controls.push_back({.id = "alias", .kind = Kind::text, .value = u"別名"});
    return controls;
}

void typing_records_one_history_entry_per_input() {
    Ui ui(two_fields());
    ui.focus("name");
    ui.select("name", 5, 5);
    for (auto ch : std::u16string(u"AB")) ui.text_input(std::u16string(1, ch));
    ui.text_input(u"\r");
    check(ui.text("name") == u"測試使用者AB", "Committed characters insert at the caret; control characters are ignored");
    ui.focus("alias");
    ui.text_input(u"X");
    ui.focus("name");
    ui.key_down(ctrl(u'Z'));
    check(ui.text("name") == u"測試使用者A" && ui.text("alias") == u"X別名", "Ctrl+Z undoes one input of the focused field only");
    ui.key_down(ctrl(u'Z', true));
    check(ui.text("name") == u"測試使用者AB", "Ctrl+Shift+Z redoes");
    ui.key_down(ctrl(u'Z'));
    ui.key_down(ctrl(u'Z'));
    ui.key_down(ctrl(u'Y'));
    check(ui.text("name") == u"測試使用者A", "Ctrl+Y redoes");
    ui.text_input(u"C");
    check(ui.redo("name").status == Status::not_supported && ui.text("name") == u"測試使用者AC", "A new edit clears the field's redo");
    check(ui.undo("alias") && ui.text("alias") == u"別名", "Each field keeps its own history");
}

Selection selection(Ui& ui, std::string_view id) { return ui.edit(id)->selection; }
bool at(Ui& ui, unsigned anchor, unsigned caret) {
    auto s = selection(ui, ui.focused());
    return s.anchor == anchor && s.caret == caret;
}

void caret_moves_by_code_point_and_extends_with_shift() {
    // "A", U+20000 (two UTF-16 units), "B", "中": UTF-16 length 5.
    Ui ui({{.id = "t", .kind = Kind::text, .value = u"A\U00020000B中"}});
    ui.focus("t");
    ui.key_down(press(Key::home));
    check(at(ui, 0, 0), "Home puts the caret at the start");
    ui.key_down(press(Key::right));
    ui.key_down(press(Key::right));
    check(at(ui, 3, 3), "Right steps over a surrogate pair as one character");
    ui.key_down(press(Key::right, true));
    ui.key_down(press(Key::end, true));
    check(at(ui, 3, 5), "Shift extends from the anchor; Shift+End reaches the end");
    ui.key_down(press(Key::left));
    check(at(ui, 3, 3), "Left without Shift collapses to the selection start");
    ui.key_down(press(Key::backspace));
    check(ui.text("t") == u"AB中" && at(ui, 1, 1), "Backspace removes the whole surrogate pair");
    ui.key_down(press(Key::del));
    check(ui.text("t") == u"A中", "Delete removes the next character");
    ui.key_down(ctrl(u'A'));
    check(at(ui, 0, 2), "Ctrl+A selects all");
    ui.key_down(press(Key::backspace));
    check(ui.text("t").empty(), "Backspace removes a selection");
    ui.undo("t"); ui.undo("t"); ui.undo("t");
    check(ui.text("t") == u"A\U00020000B中", "Each deletion was one undo entry");
    check(ui.select("t", 2, 2) && at(ui, 1, 1), "Semantic selection inside a surrogate pair snaps to its start");
}

struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    std::optional<std::u16string> read() override { return content; }
};

void paste_turns_each_line_break_into_one_space() {
    Ui ui(name_form());
    MemoryClipboard clipboard;
    ui.set_clipboard(&clipboard);
    ui.focus("name");
    clipboard.content = u"Alpha\r\nBeta";
    ui.key_down(ctrl(u'A'));
    ui.key_down(ctrl(u'V'));
    check(ui.text("name") == u"Alpha Beta", "CRLF pastes as one space");
    ui.key_down(ctrl(u'Z'));
    check(ui.text("name") == u"測試使用者", "Select-all plus paste is one undo entry");
    ui.key_down(ctrl(u'Y'));
    check(ui.text("name") == u"Alpha Beta", "Redo restores the paste");
    check(ui.paste("name", u"a\rb\nc\r\n\nd") && ui.text("name") == u"Alpha Betaa b c  d",
          "Lone CR and LF each become one space; semantic paste inserts at the caret");
    ui.select("name", 0, 5);
    ui.key_down(ctrl(u'C'));
    check(clipboard.content == u"Alpha" && ui.text("name") == u"Alpha Betaa b c  d", "Copy writes the selection only");
    ui.key_down(ctrl(u'X'));
    check(clipboard.content == u"Alpha" && ui.text("name") == u" Betaa b c  d", "Cut writes then removes the selection");
    ui.undo("name");
    check(ui.text("name") == u"Alpha Betaa b c  d", "Cut is one undo entry");
    check(ui.paste("name", u"x\ty").status == Status::invalid_value && ui.text("name") == u"Alpha Betaa b c  d",
          "Other control characters reject the whole paste and keep the text");
}

void read_only_allows_viewing_and_copying_only() {
    Ui ui(form_with_buttons());
    MemoryClipboard clipboard;
    ui.set_clipboard(&clipboard);
    ui.focus("name");
    ui.text_input(u"!");
    ui.set_read_only("name", true);
    check(ui.describe("name")->read_only && ui.focus("name"), "Read-only field stays focusable and reports its state");
    ui.key_down(ctrl(u'A'));
    ui.key_down(ctrl(u'C'));
    check(clipboard.content == u"!測試使用者", "Read-only text can be selected and copied");
    clipboard.content = u"貼上";
    std::vector<Status> refused;
    refused.push_back(ui.text_input(u"Y").status);
    refused.push_back(ui.key_down(ctrl(u'X')).status);
    refused.push_back(ui.key_down(ctrl(u'V')).status);
    refused.push_back(ui.key_down(press(Key::backspace)).status);
    refused.push_back(ui.key_down(ctrl(u'Z')).status);
    refused.push_back(ui.set_value("name", u"semantic").status);
    check(refused == std::vector<Status>(6, Status::read_only), "Every modifying path reports read_only");
    check(ui.text("name") == u"!測試使用者" && clipboard.content == u"貼上", "Text and clipboard are untouched");
    ui.set_read_only("name", false);
    check(ui.undo("name") && ui.text("name") == u"測試使用者", "History position survived the read-only period");
}

void disabled_field_refuses_every_entry_and_keeps_data() {
    Ui ui(form_with_buttons());
    int applies = 0;
    ui.bind("apply", [&] { ++applies; return Result{}; });
    ui.focus("name");
    ui.text_input(u"!");
    ui.enable("name", false);
    ui.enable("apply", false);
    check(ui.focused().empty(), "Disabling the focused control releases focus");
    std::vector<Status> refused{
        ui.focus("name").status, ui.pointer_down("name").status, ui.select("name", 0, 1).status,
        ui.copy("name").status, ui.paste("name", u"x").status, ui.undo("name").status,
        ui.set_value("name", u"x").status, ui.invoke("apply").status, ui.pointer_down("apply").status};
    check(refused == std::vector<Status>(refused.size(), Status::disabled), "Pointer, keyboard and semantic entries all refuse");
    check(ui.focus_order() == std::vector<Id>{"reset"}, "Only the recovery entry stays in the Tab order");
    ui.enable("name", true);
    ui.enable("apply", true);
    check(ui.text("name") == u"!測試使用者" && applies == 0 && ui.undo("name"), "Re-enabling keeps text and history");
}

void composition_commits_once_and_never_applies() {
    Ui ui(form_with_buttons());
    int applies = 0;
    ui.bind("apply", [&] { ++applies; return Result{}; });
    ui.focus("name");
    ui.select("name", 5, 5);
    ui.composition_update(u"ㄨㄛˇ", 3);
    check(ui.edit("name")->visible() == u"測試使用者ㄨㄛˇ" && ui.text("name") == u"測試使用者",
          "Preedit is shown but not committed");
    check(ui.key_down(ctrl(u'Z')).status == Status::busy && ui.key_down(press(Key::backspace)).status == Status::busy,
          "Editing keys belong to the IME while composing");
    check(ui.invoke("apply").status == Status::busy && applies == 0, "No command runs while composing");
    check(ui.select("name", 0, 0).status == Status::busy && ui.edit("name")->composing,
          "Moving the selection cannot silently drop a composition the IME still holds");
    ui.composition_update(u"我", 1);
    ui.composition_commit(u"我");
    ui.key_down(press(Key::enter));
    check(ui.text("name") == u"測試使用者我" && !ui.edit("name")->composing && applies == 0,
          "Commit inserts once; neither commit nor a later Enter applies");
    ui.undo("name");
    check(ui.text("name") == u"測試使用者", "One commit is one undo entry");
    ui.select("name", 0, 2);
    ui.composition_update(u"工作", 2);
    ui.composition_commit(u"工作");
    check(ui.text("name") == u"工作使用者", "Commit replaces the selection");
    ui.undo("name");
    ui.composition_update(u"ㄅ", 1);
    ui.composition_cancel();
    check(ui.text("name") == u"測試使用者" && ui.redo("name") && ui.text("name") == u"工作使用者",
          "Cancel keeps committed text and history, including redo");
    ui.select("name", 5, 5);
    ui.composition_update(u"我", 1);
    ui.focus("apply");
    check(ui.text("name") == u"工作使用者我" && !ui.edit("name")->composing && applies == 0,
          "E01: focus leaving commits the preedit once, the same default the native IME showed");
    ui.focus("name");
    ui.undo("name");
    check(ui.text("name") == u"工作使用者", "The blur commit is one undo entry");
    ui.focus("apply");
    check(ui.composition_commit(u"遲到").status == Status::invalid_value && ui.text("name") == u"工作使用者",
          "A commit with no composition and no focused field lands nowhere");
}

void focus_notifications_queue_instead_of_recursing() {
    Ui ui(form_with_buttons());
    std::vector<std::pair<Id, Id>> heard;
    int depth = 0, deepest = 0;
    ui.on_focus([&](const Id& from, const Id& to) {
        deepest = std::max(deepest, ++depth);
        heard.emplace_back(from, to);
        if (to == "apply") check(bool(ui.focus("reset")) && ui.focused() == "reset", "A write inside the callback is readable at once");
        --depth;
    });
    ui.focus("apply");
    check(deepest == 1, "The focus callback is never re-entered");
    check((heard == std::vector<std::pair<Id, Id>>{{"", "apply"}, {"apply", "reset"}}) && ui.focused() == "reset",
          "The nested move is delivered after the current notification, in order");
    ui.on_focus([](const Id&, const Id&) { throw std::runtime_error("owner bug"); });
    check(ui.focus("name").status == Status::failed && ui.stopped(), "A throwing focus callback is reported, not hidden");
}

void keyboard_editing_obeys_the_same_admission() {
    Ui ui(form_with_buttons());
    ui.focus("name");
    ui.select("name", 2, 2);
    ui.modal(true);
    check(ui.key_down(press(Key::right)).status == Status::blocked_by_modal && at(ui, 2, 2), "Caret keys respect the modal block");
    check(ui.key_down(ctrl(u'A')).status == Status::blocked_by_modal && at(ui, 2, 2), "Select-all respects the modal block");
    ui.modal(false);
    MemoryClipboard clipboard;
    ui.set_clipboard(&clipboard);
    ui.text_input(u"!");
    ui.undo("name");
    ui.set_read_only("name", true);
    clipboard.content = u"x";
    std::vector<Status> refused{ui.key_down(ctrl(u'Y')).status, ui.key_down(press(Key::del)).status,
                                ui.paste("name", u"x").status, ui.cut("name").status, ui.undo("name").status, ui.redo("name").status};
    check(refused == std::vector<Status>(refused.size(), Status::read_only), "Read-only refuses redo, Delete and semantic edits alike");
    ui.set_read_only("name", false);
    check(ui.redo("name") && ui.text("name") == u"測試!使用者", "The redo position survived");
}

void over_limit_edit_is_rejected_whole() {
    Ui ui({{.id = "code", .kind = Kind::text, .value = u"AB", .max_length = 4}});
    check(Control{.id = "x", .kind = Kind::text}.max_length == default_max_text_length && default_max_text_length == 4096,
          "The default limit is public");
    ui.focus("code");
    ui.select("code", 2, 2);
    ui.text_input(u"CD");
    check(ui.text_input(u"E").status == Status::invalid_value, "Typing past the limit is rejected");
    check(ui.paste("code", u"XYZ").status == Status::invalid_value, "Pasting past the limit is rejected");
    check(ui.set_value("code", u"12345").status == Status::invalid_value, "Semantic writes obey the same limit");
    check(ui.text("code") == u"ABCD" && ui.undo("code") && ui.text("code") == u"AB" && !ui.undo("code"),
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
    Ui ui(form_with_buttons());
    std::vector<std::pair<Id, Id>> moves;
    ui.on_focus([&](const Id& from, const Id& to) {
        moves.emplace_back(from, to);
        if (from == "name") ui.set_error("name", ui.text("name").empty() ? u"必填" : u"");
    });
    ui.focus("name");
    ui.key_down(ctrl(u'A'));
    ui.key_down(press(Key::backspace));
    ui.key_down(press(Key::tab));
    check((moves == std::vector<std::pair<Id, Id>>{{"", "name"}, {"name", "apply"}}), "Owner hears each focus move once");
    check(ui.describe("name")->error == u"必填", "Owner can validate on blur");
    auto field = ui.describe("name")->operations;
    check(has(field, Operation::focus) && has(field, Operation::edit) && has(field, Operation::paste) && has(field, Operation::undo) &&
          !has(field, Operation::redo) && !has(field, Operation::activate), "Editable field lists what it can do now");
    ui.set_read_only("name", true);
    field = ui.describe("name")->operations;
    check(has(field, Operation::select) && has(field, Operation::copy) && !has(field, Operation::edit) && !has(field, Operation::undo),
          "Read-only field lists only viewing actions");
    check((ui.describe("apply")->operations == std::vector<Operation>{Operation::focus, Operation::activate}), "Button can focus and activate");
    ui.enable("apply", false);
    check(ui.describe("apply")->operations.empty() && ui.describe("note")->operations.empty(), "Disabled controls and labels offer nothing");
    int count = 0;
    ui.bind_text("note", [&] { return u"套用 " + std::u16string(1, char16_t(u'0' + count)) + u" 次"; });
    count = 2;
    check(ui.text("note") == u"套用 2 次" && ui.describe("note")->value == u"套用 2 次", "Bound label reads the owner's data on demand");
}
}

void handles_expire_with_their_component_tree() {
    TextHandle field;
    {
        Ui ui({});
        field = ui.add_text({.id = "handle.field", .kind = Kind::text, .value = u"initial"});
        check(bool(field.set_value(u"edited")) && field.describe()->value == u"edited",
              "Typed handle edits and describes its field");
    }
    check(field.set_value(u"late").status == Status::not_found && !field.describe(),
          "A retained handle cannot keep the component tree alive or operate after its end");
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
    check(field.describe()->value == u"draft" && field.source_conflict(),
          "A different source value preserves the draft and signals the conflict");
    check(bool(field.use_latest()) && field.describe()->value == u"newest" && !field.source_conflict(),
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
        return ConfirmationResult{{Status::invalid_value, u"Invalid"}, {{1, u"second problem"}, {0, u"first problem"}}};
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
            check(apply.executing() && apply.invoke().status == Status::busy, "An executing confirmation cannot re-trigger");
            check(bool(field.set_value(u"newer")), "The field remains editable while the action executes");
        } else second_origin = changes[0].origin;
        model = changes[0].value;
        changed();
        return ConfirmationResult{};
    });
    field.set_value(u"sent");
    auto accepted = apply.invoke();
    check(bool(accepted), "The first confirmation succeeds");
    check(!apply.executing() && calls == 1, "Execution state resets and callback runs once");
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
    ui.composition_update(u"preedit", 7);
    model = u"external";
    changed();
    check(ui.edit("ime.source")->composing && ui.edit("ime.source")->preedit == u"preedit",
          "Source updates never discard an active composition");
    ui.composition_commit(u"text");
    check(field.describe()->value == u"textone" && field.source_conflict(),
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

int main() {
    const test::Case cases[] = {
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
