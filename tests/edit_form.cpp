#include "core_internal.hpp"
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
    i.volume_latest = "volume.latest"; i.volume = "volume"; i.notices = "notices"; i.mode = "mode"; i.offline_disabled = "offline";
    i.region = "region.alt"; i.region_empty = "region.empty.alt";
    i.apply = "go"; i.state = "s"; i.draft = "d"; i.result = "r"; i.count = "c"; i.latest = "latest";
    return i;
}
KeyPress key(Key k, bool shift = false) { return {.key = k, .shift = shift}; }
// Arrange the page with the current appearance and check the role carriers follow the state.
void settle(edit_form::Form& form) {
    test::FakeMeasure measure;
    for (int width : {960, 640}) {
        auto frame = arrange(form.page(), form.ui, *appearance, measure, width, 480, 1.f, form.scroll, detail::access(form.ui).focused());
        auto field = frame.find(form.handles.name);
        bool invalid = form.handles.name.describe()->invalid;
        check(field && field->label_lines.size() == 1 && field->error_lines.empty() != invalid,
              "Under this appearance the name carries its label, and an error exactly when invalid");
    }
}

void region_is_in_the_form_confirmation_group() {
    edit_form::Form form(ids);
    check(*form.handles.region.draft() == OptionValue{"north"} && form.region_applied() == OptionValue{"north"}, "Region begins at North");
    check(bool(form.handles.region.select({"south"})) && form.region_applied() == OptionValue{"north"}, "Selecting South changes only the draft");
    check(bool(form.handles.apply.invoke()) && form.region_applied() == OptionValue{"south"} && form.applies() == 1, "Apply commits region with the form");
    form.handles.region_empty.invoke();
    check(!*form.handles.region.draft(), "Clearing options clears the draft");
    check(form.handles.apply.invoke().status == Status::invalid_value && form.handles.region.describe()->focused &&
          form.handles.region.describe()->invalid && !form.handles.name.describe()->invalid && form.applies() == 1, "Missing region is rejected and focuses its own error");
    form.handles.region_empty.invoke();
    check(!*form.handles.region.draft(), "Restoring options does not resurrect the old selection");
    form.handles.name.set_value(u"");
    form.handles.apply.invoke();
    check(form.handles.name.describe()->focused && form.handles.region.describe()->invalid, "Name error takes focus before region");
    form.handles.name.set_value(u"Valid"); form.handles.region.select({"central"});
    check(bool(form.handles.apply.invoke()) && form.region_applied() == OptionValue{"central"} && form.applies() == 2, "Corrected region is explicitly selected and applied");
}

void all_five_fields_apply_once() {
    edit_form::Form form(ids);
    form.handles.name.set_value(u"\u5de5\u4f5c\u7ad9 A");
    form.handles.notices.toggle(); form.handles.mode.select({"focus"});
    form.handles.region.select({"south"}); form.handles.volume.set_value(65);
    check(form.applies() == 0 && form.region_applied() == OptionValue{"north"} && form.volume_applied() == 40,
          "Five edits remain drafts before Apply");
    check(bool(form.handles.apply.invoke()) && form.applies() == 1 && form.applied() == u"\u5de5\u4f5c\u7ad9 A" &&
          !form.notices_applied() && form.mode_applied() == OptionValue{"focus"} &&
          form.region_applied() == OptionValue{"south"} && form.volume_applied() == 65,
          "F01 applies all five values in one action");
}

void volume_is_a_confirmed_form_field() {
    edit_form::Form form(ids);
    check(*form.handles.volume.draft() == 40 && form.volume_applied() == 40 && form.handles.volume.describe()->name == u"\u97f3\u91cf", "Volume starts at 40 with its accessible name");
    form.handles.volume.set_value(75);
    check(*form.handles.volume.draft() == 75 && form.volume_applied() == 40 &&
          form.handles.draft.describe()->value.find(u"75") != std::u16string::npos, "Volume is shown as an unapplied draft");
    check(bool(form.handles.apply.invoke()) && form.volume_applied() == 75 && form.applies() == 1 &&
          form.handles.result.describe()->value.find(u"75") != std::u16string::npos, "Apply commits and displays volume");
    form.handles.reset.invoke();
    check(*form.handles.volume.draft() == 40 && form.volume_applied() == 40 && form.applies() == 0, "Reset restores volume");
}

void volume_conflict_offers_latest() {
    edit_form::Form form(ids);
    form.handles.volume.set_value(55);
    form.update_volume_source(70);
    check(*form.handles.volume.draft() == 55 && form.handles.volume.describe()->source_conflict &&
          form.handles.volume_latest.describe()->enabled, "Volume conflict preserves the draft and enables use latest");
    check(bool(form.handles.volume_latest.invoke()) && *form.handles.volume.draft() == 70 &&
          !form.handles.volume.describe()->source_conflict, "Use latest discards the volume draft");
}

void initial_values_apply_and_reset() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    check(form.handles.name.describe()->value == u"測試使用者" && form.applied() == u"測試使用者" && form.applies() == 0, "Initial draft, result and count");
    form.handles.name.set_value( u"工作站 A");
    check(form.applied() == u"測試使用者" && form.handles.result.describe()->value.find(u"測試使用者") != std::u16string::npos,
          "Editing changes only the draft");
    check(form.handles.draft.describe()->value.find(u"工作站 A") != std::u16string::npos, "Current draft is displayed");
    check(bool(form.handles.apply.invoke()) && form.applied() == u"工作站 A" && form.applies() == 1, "Valid apply updates result once");
    form.handles.name.focus();
    form.handles.name.select( 6, 6);
    form.handles.name.paste(u"，這是一段會超出欄位寬度的很長很長的名稱");
    test::FakeMeasure measure;
    arrange(form.page(), ui, *appearance, measure, 360, 240, 1.f, form.scroll, ids.apply);
    check(detail::access(ui).text_offset(ids.name) > 0 && form.scroll.offset > 0, "Long draft and small window scroll the field and the page");
    form.handles.disabled.invoke();
    check(bool(form.handles.reset.invoke()), "Reset works while the data area is disabled");
    check(form.handles.name.describe()->value == u"測試使用者" && form.applied() == u"測試使用者" && form.applies() == 0, "Reset restores initial values");
    check(!form.handles.name.undo() && detail::access(ui).text_offset(ids.name) == 0 && form.scroll.offset == 0, "Reset clears history and display offsets");
    check(form.handles.reset.describe()->focused && form.handles.name.describe()->enabled, "Focus stays on reset; the data area is enabled again");
    settle(form);
}

void blank_name_error_follows_blur_and_apply() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    form.handles.name.focus();
    form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size()));
    form.handles.name.paste(u"　 ");
    detail::access(ui).key_down(key(Key::tab));
    auto name = form.handles.name.describe();
    check(name->invalid && name->error == u"請填寫顯示名稱" && form.handles.name_error.describe()->value == name->error, "Blur on a blank name shows its error");
    check(form.handles.name_error.describe()->target == ids.name, "The error is associated with the name field");
    auto result = form.handles.apply.invoke();
    check(result.status == Status::invalid_value && form.handles.name.describe()->focused, "Invalid apply is rejected and focuses the name");
    check(form.applied() == u"測試使用者" && form.applies() == 0, "Invalid apply keeps the old result and count");
    form.handles.name.paste(u"有效名稱");
    check(!form.handles.name.describe()->invalid, "Fixing the name clears the error");
    form.handles.apply.invoke();
    check(form.applies() == 1 && form.applied() == u"　 有效名稱", "Successful apply adds exactly one");
    form.handles.name.focus();
    detail::access(ui).key_down(key(Key::enter));
    check(form.applies() == 1, "Enter in the name field does not apply");
    settle(form);
}

void apply_button_starts_once() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    detail::access(ui).pointer_down(ids.apply);
    detail::access(ui).pointer_up({});
    check(form.applies() == 0, "Pressing apply then releasing outside cancels");
    detail::access(ui).pointer_down(ids.apply);
    detail::access(ui).pointer_up(ids.apply);
    detail::access(ui).key_down(key(Key::space));
    detail::access(ui).key_down({.key = Key::space, .repeat = true});
    detail::access(ui).key_up(Key::space);
    detail::access(ui).key_down(key(Key::enter));
    detail::access(ui).key_down({.key = Key::enter, .repeat = true});
    check(form.applies() == 3, "Click, Space and Enter each apply once; holding a key does not repeat");
    settle(form);
}

void paste_undo_redo_never_touches_applied() {
    edit_form::Form form(ids);
    form.handles.name.focus();
    form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size()));
    form.handles.name.paste( u"Alpha\nBeta");
    check(form.handles.name.describe()->value == u"Alpha Beta", "Pasted line break becomes a space");
    form.handles.name.undo();
    check(form.handles.name.describe()->value == u"測試使用者", "Undo restores the original name");
    form.handles.name.redo();
    check(form.handles.name.describe()->value == u"Alpha Beta" && form.applied() == u"測試使用者" && form.applies() == 0,
          "Redo restores the paste; the applied result never changes");
    settle(form);
}

void read_only_and_disabled_fixtures_restrict_and_recover() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    form.handles.name.focus();
    form.handles.name.paste(u"!");
    form.handles.read_only.invoke();
    check(form.handles.name.describe()->read_only && form.handles.state.describe()->value.find(u"唯讀") != std::u16string::npos,
          "Read-only state has readable text, not just color");
    form.handles.name.focus();
    check(form.handles.name.paste(u"x").status == Status::read_only && form.handles.name.undo().status == Status::read_only,
          "Read-only name refuses input and undo");
    form.handles.disabled.invoke();
    check(form.handles.state.describe()->value.find(u"停用") != std::u16string::npos, "Disabled state has readable text");
    std::vector<Status> refused{form.handles.name.focus().status, detail::access(ui).pointer_down(ids.apply).status,
                                form.handles.name.set_value( u"x").status, form.handles.apply.invoke().status};
    check(refused == std::vector<Status>(4, Status::disabled) && form.applies() == 0, "Pointer, keyboard and semantic retries are refused");
    auto order = detail::access(ui).focus_order();
    check(std::find(order.begin(), order.end(), ids.normal) != order.end() && std::find(order.begin(), order.end(), ids.name) == order.end(),
          "Recovery entry stays reachable while the name leaves the Tab order");
    form.handles.normal.invoke();
    check(form.handles.name.describe()->value == u"!測試使用者" && bool(form.handles.name.undo()), "Recovery keeps the draft and its history");
    settle(form);
}

void keyboard_reaches_every_entry_and_semantics_answer() {
    edit_form::Form form(ids);
    auto& ui = form.ui;
    std::vector<Id> seen;
    for (int i = 0; i < 15; ++i) { detail::access(ui).key_down(key(Key::tab)); seen.push_back(detail::access(ui).focused()); }
    std::vector<Id> expected{ids.normal, ids.read_only, ids.disabled, ids.error, ids.long_text, ids.reset, ids.name, ids.notices, ids.mode, ids.region, ids.volume, ids.offline_disabled, ids.region_empty, ids.apply, ids.normal};
    check(seen == expected, "Tab reaches fixtures, name and apply, then wraps");
    detail::access(ui).key_down(key(Key::tab, true));
    check(form.handles.apply.describe()->focused, "Shift+Tab walks back");
    auto name = form.handles.name.describe();
    check(name->kind == Kind::text && name->name == u"顯示名稱" && name->value == u"測試使用者" && !name->focused,
          "Name field answers identity, type, name and value");
    auto apply = form.handles.apply.describe();
    check(apply->kind == Kind::button && apply->name == u"套用" && apply->focused, "Apply answers its name and focus");
    check(!form.handles.title.describe()->focusable && !form.handles.result.describe()->focusable, "Display text stays out of the Tab order");
    form.handles.error.invoke();
    check(form.handles.name.describe()->invalid && form.handles.name.describe()->focused, "Error fixture shows the name error and focuses it");
    settle(form);
}

void source_updates_offer_the_latest_value() {
    edit_form::Form form(ids);
    form.handles.name.set_value( u"my draft");
    form.update_source(u"remote value");
    check(form.handles.name.describe()->value == u"my draft" && form.handles.state.describe()->value.find(u"\u4f86\u6e90\u5df2\u66f4\u65b0") != std::u16string::npos,
          "An external change retains the draft and displays a conflict");
    check(bool(form.handles.latest.invoke()) && form.handles.name.describe()->value == u"remote value",
          "The user can discard the draft in favor of the latest source value");
}

void checkbox_and_radio_are_applied_together() {
    edit_form::Form form(ids);
    check(form.handles.notices.describe()->name == u"\u555f\u7528\u63d0\u793a" &&
          form.handles.mode.describe()->options[0].label == u"\u4e00\u822c", "New controls retain their Traditional Chinese labels");
    check(*form.handles.notices.draft() && *form.handles.mode.draft() == OptionValue{"normal"},
          "Notices and normal mode are initially selected");
    form.handles.notices.focus();
    detail::access(form.ui).key_down({.key = Key::space});
    detail::access(form.ui).key_up(Key::space);
    form.handles.mode.focus();
    detail::access(form.ui).key_down({.key = Key::right});
    check(!*form.handles.notices.draft() && *form.handles.mode.draft() == OptionValue{"focus"} && form.notices_applied(),
          "Keyboard changes drafts while the applied result stays unchanged");
    check(bool(form.handles.apply.invoke()) && !form.notices_applied() && form.mode_applied() == OptionValue{"focus"},
          "Apply commits the boolean and radio drafts together");
    form.handles.offline_disabled.invoke();
    check(form.handles.mode.select({"offline"}).status == Status::disabled, "Offline can be disabled");
    form.handles.reset.invoke();
    check(form.notices_applied() && form.mode_applied() == OptionValue{"normal"} && *form.handles.notices.draft(),
          "Reset restores the initial checkbox and radio values");
}

void pages_keep_independent_drafts_history_and_scroll() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    test::FakeMeasure measure;
    h.name.set_value(u"a very long draft that needs horizontal scrolling in a narrow field");
    h.name.focus(); h.name.select(66, 66);
    h.long_text.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll, ids.name);
    auto offset = frame.find(h.name)->offset;
    form.scroll.offset = frame.maximum;
    int saved_scroll = form.scroll.offset;
    check(offset > 0 && saved_scroll > 0, "V01 has horizontal and vertical offsets to preserve");
    check(bool(h.tabs.activate({"v02"})), "Open V02");
    check(h.name.set_value(u"blocked").status == Status::hidden, "Hidden V01 cannot be edited");
    auto& list = form.list->handles;
    list.search.set_value(u"\u5171\u7528\u7bc4\u672c"); list.list.select({"T006"});
    check(bool(h.tabs.activate({"v01"})), "Return to V01");
    check(*h.name.draft() == u"a very long draft that needs horizontal scrolling in a narrow field", "V01 draft survives switch");
    check(form.applied() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "V01 applied value survives switch");
    check(form.scroll.offset == saved_scroll, "V01 vertical offset survives switch");
    frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll);
    check(frame.find(h.name)->offset == offset, "V01 horizontal offset survives switch");
    h.tabs.activate({"v02"});
    check(*list.search.draft() == u"\u5171\u7528\u7bc4\u672c" && form.list->selection() == OptionValue{"T006"}, "V02 search and T006 survive a switch");
    list.reset.invoke();
    check(list.reset.describe()->focused && list.search.draft()->empty() && !form.list->selection() && list.list.describe()->options.size() == 20, "V02 reset restores the real list and keeps focus");
    list.hundred.invoke(); list.list.focus(); list.list.select({"T100"});
    auto list_frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll, list.list.describe()->id);
    auto row = list_frame.find(list.list)->options.back();
    auto clip = list_frame.find(list.list)->clip;
    int list_outer = form.scroll.offset, list_inner = form.list->list_scroll.offset;
    check(list_outer > 0 && list_inner > 0 && row.y >= clip.y && row.y + row.h <= clip.y + clip.h,
          "V02 target is fully revealed by both viewports");
    h.tabs.activate({"v01"}); h.tabs.activate({"v02"});
    check(form.scroll.offset == list_outer && form.list->list_scroll.offset == list_inner &&
          form.list->selection() == OptionValue{"T100"}, "Both V02 offsets and selection survive switching");
    list.reset.invoke();
    check(form.scroll.offset == 0 && form.list->list_scroll.offset == 0 && !form.list->selection(),
          "Reset restores both V02 offsets and clears selection");
    h.tabs.activate({"v01"});
    frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll);
    check(form.scroll.offset == saved_scroll && frame.find(h.name)->offset == offset && bool(h.name.undo()) &&
          *h.name.draft() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "V02 reset preserves V01 offsets and undo history");
}

void progress_displays_clamped_values_without_changing_the_model() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    test::FakeMeasure measure;
    h.name.set_value(u"kept draft");
    check(bool(h.tabs.activate({"v03"})), "Open V03");
    check(form.progress() == 50 && h.progress.describe()->value == u"50%", "Progress starts at 50%");
    const struct { ButtonHandle button; int source; int shown; std::u16string text; } cases[] = {
        {h.progress_zero, 0, 0, u"0%"}, {h.progress_half, 50, 50, u"50%"},
        {h.progress_full, 100, 100, u"100%"}, {h.progress_below, -10, 0, u"0%"},
        {h.progress_above, 110, 100, u"100%"}
    };
    for (const auto& c : cases) {
        check(bool(c.button.invoke()), "Progress fixture is available");
        auto semantic = h.progress.describe();
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll);
        const auto* p = frame.find(h.progress);
        check(form.progress() == c.source && semantic->numeric_value == c.shown && semantic->value == c.text,
              "Source stays unchanged while semantic value is clamped");
        check(p && !p->lines.empty() && p->lines.front().text.find(c.text) != std::u16string::npos &&
              p->progress_fill.w == p->progress_track.w * c.shown / 100,
              "Rendered text and filled track agree");
    }
    h.tabs.activate({"v02"});
    form.list->handles.search.set_value(u"\u5171\u7528\u7bc4\u672c");
    form.list->handles.list.select({"T006"});
    h.tabs.activate({"v03"});
    check(form.progress() == 110 && h.progress.describe()->value == u"100%", "Progress survives page switches");
    check(bool(h.reset_v03.invoke()) && form.progress() == 50 && h.progress.describe()->value == u"50%",
          "Reset restores V03 progress to 50%");
    h.tabs.activate({"v02"});
    check(*form.list->handles.search.draft() == u"\u5171\u7528\u7bc4\u672c" && form.list->selection() == OptionValue{"T006"},
          "Resetting V03 preserves V02 search and selection");
    h.tabs.activate({"v01"});
    check(*h.name.draft() == u"kept draft", "Resetting V03 preserves the V01 draft");
}

void both_appearances_give_same_results() {
    std::vector<std::vector<Semantics>> runs;
    std::vector<std::vector<int>> outcomes;
    for (const auto* each : {&web_appearance(), &material_appearance()}) {
        edit_form::Form form(ids);
        auto& ui = form.ui;
        test::FakeMeasure measure;
        std::vector<int> statuses;
        auto step = [&](Result r) { statuses.push_back(static_cast<int>(r.status)); arrange(form.page(), ui, *each, measure, 640, 480, 1.f, form.scroll, detail::access(ui).focused()); };
        step(form.handles.name.focus());
        step(form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size())));
        step(form.handles.name.paste( u"Alpha\r\nBeta"));
        step(form.handles.apply.invoke());
        step(form.handles.read_only.invoke());
        step(form.handles.name.paste( u"x"));
        step(form.handles.disabled.invoke());
        step(form.handles.apply.invoke());
        step(form.handles.normal.invoke());
        step(form.handles.name.focus());
        step(form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size())));
        step(form.handles.name.paste(u" "));
        step(form.handles.apply.invoke());
        auto frame = arrange(form.page(), ui, *each, measure, 960, 720, 1.f, form.scroll);
        auto field = frame.find(form.handles.name);
        check(field && !field->error_lines.empty() && field->label_lines.front().text == u"顯示名稱",
              "Each appearance carries the name label and error with the field");
        std::vector<Semantics> snapshot;
        const auto& h = form.handles;
        for (auto label : {h.title, h.intro, h.name_label, h.name_help, h.name_error, h.state, h.draft, h.result, h.count})
            snapshot.push_back(*label.describe());
        snapshot.push_back(*h.name.describe());
        for (auto button : {h.normal, h.read_only, h.disabled, h.error, h.long_text, h.reset, h.apply, h.latest})
            snapshot.push_back(*button.describe());
        Semantics applied;
        applied.id = "applied";
        applied.value = form.applied() + u"/" + std::u16string(1, char16_t(u'0' + form.applies()));
        snapshot.push_back(applied);
        runs.push_back(std::move(snapshot));
        int depth = 0;  // measured last: walking history re-runs the form's fix-clears-error policy
        while (form.handles.name.undo()) ++depth;
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
        {"P07 progress display and source", progress_displays_clamped_values_without_changing_the_model},
        {"L07 L08 R07 page scopes", pages_keep_independent_drafts_history_and_scroll},
        {"F08 region confirmation and error order", region_is_in_the_form_confirmation_group},
        {"F01 five fields apply once", all_five_fields_apply_once},
        {"G11 volume conflict", volume_conflict_offers_latest},
        {"F09 volume confirmation", volume_is_a_confirmed_form_field},
        {"F09 checkbox and radio apply together", checkbox_and_radio_are_applied_together},
        {"G11 source updates offer the latest value", source_updates_offer_the_latest_value},
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
