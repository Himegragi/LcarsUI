// V01 minimal edit form acceptance through the assembled application's public operations.
// Every case runs twice: with the shipped IDs and with an unrelated ID set (AC02).
#include "check.hpp"
#include "edit_form.hpp"
#include "fake_measure.hpp"
using namespace lcars;
using test::check;

namespace {
edit_form::Ids ids = edit_form::default_ids();
const Appearance* appearance = &web_appearance();  // every case is rerun under each appearance

edit_form::Ids alternate_ids() {
    edit_form::Ids i;
    i.title = "t"; i.intro = "i"; i.normal = "f1"; i.read_only = "f2"; i.disabled = "f3"; i.error = "f4";
    i.long_text = "f5"; i.reset = "f6"; i.name_label = "n.l"; i.name = "n"; i.name_help = "n.h"; i.name_error = "n.e";
    i.apply = "go"; i.state = "s"; i.draft = "d"; i.result = "r"; i.count = "c";
    return i;
}
KeyPress key(Key k, bool shift = false) { return {.key = k, .shift = shift}; }
KeyPress ctrl(char16_t letter) { return {.ctrl = true, .letter = letter}; }
// Arrange the page with the current appearance and check the role carriers follow the state.
void settle(edit_form::Form& form) {
    test::FakeMeasure measure;
    for (int width : {960, 640}) {
        auto frame = arrange(form.page(), form.ui, *appearance, measure, width, 480, 1.f, form.scroll, form.ui.focused());
        auto field = frame.find(ids.name);
        bool invalid = form.ui.describe(ids.name)->invalid;
        check(field && field->label_lines.size() == 1 && field->error_lines.empty() != invalid,
              "Under this appearance the name carries its label, and an error exactly when invalid");
    }
}

void initial_values_apply_and_reset() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    check(ui.text(ids.name) == u"測試使用者" && form.applied() == u"測試使用者" && form.applies() == 0, "Initial draft, result and count");
    ui.set_value(ids.name, u"工作站 A");
    check(form.applied() == u"測試使用者" && ui.text(ids.result).find(u"測試使用者") != std::u16string::npos,
          "Editing changes only the draft");
    check(ui.text(ids.draft).find(u"工作站 A") != std::u16string::npos, "Current draft is displayed");
    check(bool(ui.invoke(ids.apply)) && form.applied() == u"工作站 A" && form.applies() == 1, "Valid apply updates result once");
    ui.focus(ids.name);
    ui.select(ids.name, 6, 6);
    ui.text_input(u"，這是一段會超出欄位寬度的很長很長的名稱");
    test::FakeMeasure measure;
    arrange(form.page(), ui, *appearance, measure, 360, 240, 1.f, form.scroll, ids.apply);
    check(ui.text_offset(ids.name) > 0 && form.scroll.offset > 0, "Long draft and small window scroll the field and the page");
    ui.invoke(ids.disabled);
    check(bool(ui.invoke(ids.reset)), "Reset works while the data area is disabled");
    check(ui.text(ids.name) == u"測試使用者" && form.applied() == u"測試使用者" && form.applies() == 0, "Reset restores initial values");
    check(!ui.undo(ids.name) && ui.text_offset(ids.name) == 0 && form.scroll.offset == 0, "Reset clears history and display offsets");
    check(ui.focused() == ids.reset && ui.describe(ids.name)->enabled, "Focus stays on reset; the data area is enabled again");
    settle(form);
}

void blank_name_error_follows_blur_and_apply() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    ui.focus(ids.name);
    ui.key_down(ctrl(u'A'));
    ui.text_input(u"　 ");
    ui.key_down(key(Key::tab));
    auto name = ui.describe(ids.name);
    check(name->invalid && name->error == u"請填寫顯示名稱" && ui.text(ids.name_error) == name->error, "Blur on a blank name shows its error");
    check(ui.describe(ids.name_error)->target == ids.name, "The error is associated with the name field");
    auto result = ui.invoke(ids.apply);
    check(result.status == Status::invalid_value && ui.focused() == ids.name, "Invalid apply is rejected and focuses the name");
    check(form.applied() == u"測試使用者" && form.applies() == 0, "Invalid apply keeps the old result and count");
    ui.text_input(u"有效名稱");
    check(!ui.describe(ids.name)->invalid, "Fixing the name clears the error");
    ui.invoke(ids.apply);
    check(form.applies() == 1 && form.applied() == u"　 有效名稱", "Successful apply adds exactly one");
    ui.focus(ids.name);
    ui.key_down(key(Key::enter));
    check(form.applies() == 1, "Enter in the name field does not apply");
    settle(form);
}

void apply_button_starts_once() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    ui.pointer_down(ids.apply);
    ui.pointer_up({});
    check(form.applies() == 0, "Pressing apply then releasing outside cancels");
    ui.pointer_down(ids.apply);
    ui.pointer_up(ids.apply);
    ui.key_down(key(Key::space));
    ui.key_down({.key = Key::space, .repeat = true});
    ui.key_up(Key::space);
    ui.key_down(key(Key::enter));
    ui.key_down({.key = Key::enter, .repeat = true});
    check(form.applies() == 3, "Click, Space and Enter each apply once; holding a key does not repeat");
    settle(form);
}

void paste_undo_redo_never_touches_applied() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    ui.focus(ids.name);
    ui.key_down(ctrl(u'A'));
    ui.paste(ids.name, u"Alpha\nBeta");
    check(ui.text(ids.name) == u"Alpha Beta", "Pasted line break becomes a space");
    ui.key_down(ctrl(u'Z'));
    check(ui.text(ids.name) == u"測試使用者", "Undo restores the original name");
    ui.key_down(ctrl(u'Y'));
    check(ui.text(ids.name) == u"Alpha Beta" && form.applied() == u"測試使用者" && form.applies() == 0,
          "Redo restores the paste; the applied result never changes");
    settle(form);
}

void read_only_and_disabled_fixtures_restrict_and_recover() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    ui.focus(ids.name);
    ui.text_input(u"!");
    ui.invoke(ids.read_only);
    check(ui.describe(ids.name)->read_only && ui.text(ids.state).find(u"唯讀") != std::u16string::npos,
          "Read-only state has readable text, not just color");
    ui.focus(ids.name);
    check(ui.text_input(u"x").status == Status::read_only && ui.undo(ids.name).status == Status::read_only,
          "Read-only name refuses input and undo");
    ui.invoke(ids.disabled);
    check(ui.text(ids.state).find(u"停用") != std::u16string::npos, "Disabled state has readable text");
    std::vector<Status> refused{ui.focus(ids.name).status, ui.pointer_down(ids.apply).status,
                                ui.set_value(ids.name, u"x").status, ui.invoke(ids.apply).status};
    check(refused == std::vector<Status>(4, Status::disabled) && form.applies() == 0, "Pointer, keyboard and semantic retries are refused");
    auto order = ui.focus_order();
    check(std::find(order.begin(), order.end(), ids.normal) != order.end() && std::find(order.begin(), order.end(), ids.name) == order.end(),
          "Recovery entry stays reachable while the name leaves the Tab order");
    ui.invoke(ids.normal);
    check(ui.text(ids.name) == u"!測試使用者" && bool(ui.undo(ids.name)), "Recovery keeps the draft and its history");
    settle(form);
}

void keyboard_reaches_every_entry_and_semantics_answer() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    std::vector<Id> seen;
    for (int i = 0; i < 9; ++i) { ui.key_down(key(Key::tab)); seen.push_back(ui.focused()); }
    std::vector<Id> expected{ids.normal, ids.read_only, ids.disabled, ids.error, ids.long_text, ids.reset, ids.name, ids.apply, ids.normal};
    check(seen == expected, "Tab reaches fixtures, name and apply, then wraps");
    ui.key_down(key(Key::tab, true));
    check(ui.focused() == ids.apply, "Shift+Tab walks back");
    auto name = ui.describe(ids.name);
    check(name->kind == Kind::text && name->name == u"顯示名稱" && name->value == u"測試使用者" && !name->focused,
          "Name field answers identity, type, name and value");
    auto apply = ui.describe(ids.apply);
    check(apply->kind == Kind::button && apply->name == u"套用" && apply->focused, "Apply answers its name and focus");
    check(!ui.describe(ids.title)->focusable && !ui.describe(ids.result)->focusable, "Display text stays out of the Tab order");
    ui.invoke(ids.error);
    check(ui.describe(ids.name)->invalid && ui.focused() == ids.name, "Error fixture shows the name error and focuses it");
    settle(form);
}

void both_appearances_give_same_results() {
    std::vector<std::vector<Semantics>> runs;
    std::vector<std::vector<int>> outcomes;
    for (const auto* each : {&web_appearance(), &material_appearance()}) {
        edit_form::Form form(ids);
        auto& ui = form.ui;
        test::FakeMeasure measure;
        std::vector<int> statuses;
        auto step = [&](Result r) { statuses.push_back(static_cast<int>(r.status)); arrange(form.page(), ui, *each, measure, 640, 480, 1.f, form.scroll, ui.focused()); };
        step(ui.focus(ids.name));
        step(ui.key_down(ctrl(u'A')));
        step(ui.paste(ids.name, u"Alpha\r\nBeta"));
        step(ui.invoke(ids.apply));
        step(ui.invoke(ids.read_only));
        step(ui.paste(ids.name, u"x"));
        step(ui.invoke(ids.disabled));
        step(ui.invoke(ids.apply));
        step(ui.invoke(ids.normal));
        step(ui.focus(ids.name));
        step(ui.key_down(ctrl(u'A')));
        step(ui.text_input(u" "));
        step(ui.invoke(ids.apply));
        auto frame = arrange(form.page(), ui, *each, measure, 960, 720, 1.f, form.scroll);
        auto field = frame.find(ids.name);
        check(field && !field->error_lines.empty() && field->label_lines.front().text == u"顯示名稱",
              "Each appearance carries the name label and error with the field");
        std::vector<Semantics> snapshot;
        for (const auto& control : ui.controls()) snapshot.push_back(*ui.describe(control.id));
        Semantics applied;
        applied.id = "applied";
        applied.value = form.applied() + u"/" + std::u16string(1, char16_t(u'0' + form.applies()));
        snapshot.push_back(applied);
        runs.push_back(std::move(snapshot));
        int depth = 0;  // measured last: walking history re-runs the form's fix-clears-error policy
        while (ui.undo(ids.name)) ++depth;
        statuses.push_back(depth);
        outcomes.push_back(statuses);
    }
    check(outcomes[0] == outcomes[1], "Every operation result and the undo depth agree across appearances");
    check(runs[0].size() == runs[1].size(), "Same controls under both appearances");
    for (std::size_t i = 0; i < runs[0].size(); ++i) {
        const auto &a = runs[0][i], &b = runs[1][i];
        check(a.id == b.id && a.value == b.value && a.error == b.error && a.focused == b.focused && a.enabled == b.enabled &&
              a.read_only == b.read_only && a.labels == b.labels && a.operations == b.operations,
              "Draft, result, errors, focus and history agree across appearances");
    }
}
}

int main() {
    const test::Case cases[] = {
        {"AC04 initial values, apply and reset", initial_values_apply_and_reset},
        {"AC05 blank name error follows blur and apply", blank_name_error_follows_blur_and_apply},
        {"AC06 apply button starts once", apply_button_starts_once},
        {"AC09 paste, undo and redo never touch the applied result", paste_undo_redo_never_touches_applied},
        {"AC10 read-only and disabled fixtures restrict and recover", read_only_and_disabled_fixtures_restrict_and_recover},
        {"AC11 keyboard reaches every entry and semantics answer", keyboard_reaches_every_entry_and_semantics_answer},
        {"AC12 both appearances give same results", both_appearances_give_same_results},
    };
    int failed = 0;
    for (const auto* each : {&web_appearance(), &material_appearance()}) {
        appearance = each;
        ids = edit_form::default_ids();
        failed |= test::run(each == &web_appearance() ? "edit_form (web, shipped IDs)" : "edit_form (material, shipped IDs)", cases);
        ids = alternate_ids();
        failed |= test::run(each == &web_appearance() ? "edit_form (web, alternate IDs)" : "edit_form (material, alternate IDs)", cases);
    }
    return failed;
}
