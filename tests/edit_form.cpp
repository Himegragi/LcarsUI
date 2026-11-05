#include "core_internal.hpp"
#include "component_type.hpp"
// Assembled showcase acceptance through public operations and the host input seam.
// Every case runs for both appearances and both the shipped and alternate ID sets (AC02).
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

void long_field_label_and_error_keep_their_association() {
    Ui ui({});
    const std::u16string title = u"這是一個需要在狹窄視窗內換行的非常長的欄位名稱";
    auto label = ui.add_label({.id = "label", .kind = Kind::label, .value = title, .target = "field", .role = TextRole::name});
    auto field = ui.add_text({.id = "field", .kind = Kind::text, .value = u"value"});
    auto error = ui.add_label({.id = "error", .kind = Kind::label, .target = "field", .role = TextRole::error});
    auto next = ui.add_button({.id = "next", .kind = Kind::button, .label = u"Next"});
    auto root = layout::column({layout::control(field), layout::control(next)}, 12);
    test::FakeMeasure measure; ScrollState scroll;
    auto before = arrange(root, ui, *appearance, measure, 240, 600, 1.f, scroll);
    field.set_error(u"這是一段很長的錯誤訊息，需要換行而且不能覆蓋後面的按鈕");
    auto after = arrange(root, ui, *appearance, measure, 240, 600, 1.f, scroll);
    auto* p = after.find(field); auto* b = after.find(next);
    check(p && b && p->label_lines.size() > 1 && p->error_lines.size() > 1, "Long field label and error both wrap");
    bool inside = true;
    for (const auto& line : p->label_lines) inside = inside && intersect(line.bounds, p->bounds) == line.bounds;
    for (const auto& line : p->error_lines) inside = inside && intersect(line.bounds, p->bounds) == line.bounds;
    check(inside && b->bounds.y >= p->bounds.y + p->bounds.h && b->bounds.y > before.find(next)->bounds.y,
          "Remeasured field keeps all lines inside and pushes the next control down");
    check(field.describe()->name == title && field.describe()->invalid && label.describe()->target == "field" &&
          error.describe()->target == "field" && error.describe()->value == field.describe()->error,
          "Public field and labels retain their semantic association");
}

void radio_boundaries_under_the_appearance() {
    Ui ui({});
    auto radio = ui.add_radio_group({.id = "radio", .kind = Kind::radio_group, .label = u"Mode"},
        {{{"first"}, u"First", false}, {{"middle"}, u"Middle", true}, {{"last"}, u"Last", false}});
    auto next = ui.add_button({.id = "next", .kind = Kind::button, .label = u"Next"});
    std::optional<OptionValue> selected;
    radio.connect_source({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    radio.connect_immediate([&](const OptionChange& change) { selected = change.value; return Result{}; });
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::column({layout::control(radio), layout::control(next)}, 8);
    auto frame = arrange(root, ui, *appearance, measure, 240, 300, 1.f, scroll);
    check(frame.find(radio) && !radio.draft()->has_value() && radio.select({"first"}).status == Status::disabled &&
          radio.select({"last"}).status == Status::disabled, "Empty radio rejects disabled first and last options");
    radio.focus(); detail::access(ui).key_down({Key::right}); detail::access(ui).key_down({Key::right});
    check(selected == OptionValue{"middle"}, "Navigation chooses only the enabled option");
    detail::access(ui).key_down({Key::tab});
    check(next.describe()->focused, "Tab exits the group");
    Ui all_ui({});
    auto all = all_ui.add_radio_group({.id = "all", .kind = Kind::radio_group}, {{{"a"}, u"A", false}, {{"b"}, u"B", false}});
    auto after = all_ui.add_button({.id = "after", .kind = Kind::button});
    all.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    all.connect_immediate([](const OptionChange&) { return Result{}; });
    auto all_frame = arrange(layout::column({layout::control(all), layout::control(after)}), all_ui, *appearance, measure, 240, 300, 1.f, scroll);
    all.focus(); detail::access(all_ui).key_down({Key::right}); detail::access(all_ui).key_down({Key::tab});
    check(all_frame.find(all) && !all.draft()->has_value() && after.describe()->focused, "All-disabled radio remains empty and can be exited");
}

void slider_non_divisible_under_the_appearance() {
    Ui ui({});
    auto slider = ui.add_slider({.id = "slider", .kind = Kind::slider}, {-3, 8, 4}).value.value();
    slider.connect_source({[] { return -3; }, [](std::function<void()>) { return [] {}; }});
    slider.connect_confirmed([](const NumericChange&) { return Result{}; });
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::control(slider);
    auto frame = arrange(root, ui, *appearance, measure, 240, 120, 1.f, scroll);
    slider.set_value(3);
    check(frame.find(slider) && *slider.draft() == 5, "Non-divisible slider renders and midpoint rounds upward");
    slider.focus(); detail::access(ui).key_down({Key::home});
    check(*slider.draft() == -3, "Home reaches minimum");
    detail::access(ui).key_down({Key::end}); detail::access(ui).key_down({Key::left});
    check(*slider.draft() == 5, "Left from maximum reaches last regular step");
    detail::access(ui).key_down({Key::end});
    check(*slider.draft() == 8, "End reaches non-divisible maximum");
    int rejected = 0;
    for (auto range : {SliderRange{1, 1, 1}, SliderRange{2, 1, 1}, SliderRange{0, 1, 0}, SliderRange{0, 1, -1}})
        if (ui.add_slider({.id = "invalid", .kind = Kind::slider}, range).status == Status::invalid_value) ++rejected;
    check(rejected == 4, "Invalid ranges and steps are rejected");
}

void disabled_tabs_under_the_appearance() {
    Ui ui({});
    auto first = ui.add_button({.id = "first", .kind = Kind::button});
    auto hidden = ui.add_button({.id = "hidden", .kind = Kind::button});
    auto tabs = ui.add_tabs({.id = "tabs", .kind = Kind::tabs}, {
        {{"one"}, u"One", true, {first}}, {{"blocked"}, u"Blocked", false, {}}, {{"two"}, u"Two", true, {hidden}}});
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::column({layout::control(tabs), layout::control(first), layout::control(hidden)}), ui, *appearance, measure, 400, 300, 1.f, scroll);
    check(frame.find(tabs) && !frame.find(hidden) && hidden.invoke().status == Status::hidden &&
          tabs.activate({"blocked"}).status == Status::disabled, "Hidden page is not rendered or actionable and disabled tab rejects activation");
    tabs.focus(); detail::access(ui).key_down({Key::right});
    check(*tabs.active() == OptionValue{"one"} && tabs.describe()->candidate == OptionValue{"two"}, "Keyboard skips disabled tab without activating it");
    detail::access(ui).key_down({Key::enter});
    check(*tabs.active() == OptionValue{"two"} && first.invoke().status == Status::hidden, "Keyboard activates the enabled page and hides the previous page");
}

void keyboard_completes_the_three_page_journey() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    auto tap = [&](Key k) {
        host.key_down({k}); host.key_up(k);
        arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, host.focused());
    };
    auto walk = [&](const std::string& target) {
        for (int i = 0; i < 100 && host.focused() != target; ++i) tap(Key::tab);
        return host.focused() == target;
    };
    check(walk(h.apply.describe()->id), "Keyboard reaches V01 Apply");
    tap(Key::enter);
    check(form.applies() == 1, "Keyboard applies the normal form once");
    check(walk(h.disabled.describe()->id), "Keyboard reaches disabled fixture");
    tap(Key::enter);
    check(!h.name.describe()->enabled && walk(h.normal.describe()->id), "Data is disabled and keyboard reaches recovery");
    tap(Key::enter);
    check(walk(h.tabs.describe()->id), "Keyboard reaches page tabs");
    tap(Key::right); tap(Key::enter);
    check(*h.tabs.active() == OptionValue{"v02"}, "Keyboard opens V02");
    auto& list = form.list->handles;
    check(walk(list.zero.describe()->id), "Keyboard reaches empty-list fixture");
    tap(Key::enter);
    check(walk(list.list.describe()->id), "Keyboard reaches empty list");
    tap(Key::down); tap(Key::space);
    check(!form.list->selection() && !list.list.describe()->candidate, "Empty list remains unselected");
    tap(Key::tab);
    check(!list.list.describe()->focused, "Keyboard leaves empty list");
    check(walk(list.twenty.describe()->id), "Keyboard reaches 20-item fixture");
    tap(Key::enter);
    check(walk(list.list.describe()->id), "Keyboard reenters populated list");
    tap(Key::end);
    check(form.list->selection() == OptionValue{"T020"}, "Keyboard selects the last item");
    check(walk(h.tabs.describe()->id), "Keyboard returns to tabs from V02");
    tap(Key::right); tap(Key::enter);
    check(*h.tabs.active() == OptionValue{"v03"} && walk(h.dialog_open.describe()->id), "Keyboard opens V03 and reaches dialog opener");
    tap(Key::enter);
    check(h.dialog_cancel.describe()->focused && *h.dialog.is_open(), "Dialog starts on Cancel");
    tap(Key::tab); tap(Key::enter);
    check(!*h.dialog.is_open() && form.progress() == 0 && form.dialog_confirmations() == 1 && h.dialog_open.describe()->focused,
          "Keyboard confirms once and leaves the modal at its opener");
    check(walk(h.tabs.describe()->id), "Keyboard can leave V03");
}

void resets_reproduce_cases_and_preserve_other_pages() {
    edit_form::Form form(ids, true); auto& h = form.handles;
    test::FakeMeasure measure;
    auto frame = [&] { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, detail::access(form.ui).focused()); };
    h.tabs.activate({"v02"}); auto& list = form.list->handles;
    list.search.set_value(u"共用範本"); list.list.select({"T006"});
    h.tabs.activate({"v03"}); h.progress_full.invoke();
    h.tabs.activate({"v01"}); h.name.set_value(u"draft"); h.reset.invoke();
    check(frame().find(h.name) && *h.name.draft() == u"測試使用者" && form.applies() == 0, "V01 reset restores initial form");
    h.name.set_value(u"工作站 A"); h.apply.invoke();
    check(form.applies() == 1 && form.applied() == u"工作站 A", "Representative V01 apply works again after reset");
    h.tabs.activate({"v02"});
    check(*list.search.draft() == u"共用範本" && form.list->selection() == OptionValue{"T006"}, "V01 reset retained V02 state");
    list.reset.invoke();
    check(frame().find(list.list) && list.search.draft()->empty() && !form.list->selection() && list.list.describe()->options.size() == 20, "V02 reset restores initial list");
    list.list.select({"T003"}); list.erase.invoke();
    check(!form.list->selection() && list.list.describe()->candidate == OptionValue{"T004"}, "Representative V02 deletion works again after reset");
    h.tabs.activate({"v03"});
    check(form.progress() == 100, "V02 reset retained V03 progress");
    h.reset_v03.invoke();
    check(frame().find(h.progress) && form.progress() == 50 && form.dialog_confirmations() == 0, "V03 reset restores initial progress");
    h.dialog_open.invoke(); h.dialog_confirm.invoke();
    check(form.progress() == 0 && form.dialog_confirmations() == 1, "Representative V03 confirmation works again after reset");
    h.tabs.activate({"v01"});
    check(form.applied() == u"工作站 A" && form.applies() == 1, "Other-page resets retain V01 applied result");
    h.tabs.activate({"v02"});
    check(list.list.describe()->options.size() == 19 && !form.list->selection(), "V03 reset retained V02 deletion");
}
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

void checkbox_conflict_is_visible_and_can_use_latest() {
    bool source = true; std::function<void()> changed;
    Ui ui({});
    auto box = ui.add_checkbox({.id = "check", .kind = Kind::checkbox, .label = u"Check"});
    box.connect_source({[&] { return source; }, [&](std::function<void()> cb) { changed = std::move(cb); return [] {}; }});
    box.connect_confirmed([&](const BoolChange&) {
        box.toggle(); source = false; changed();
        return Result{Status::invalid_value, u"Rejected"};
    });
    box.toggle(); box.confirm();
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::control(box), ui, *appearance, measure, 400, 400, 1.f, scroll);
    const auto* placed = frame.find(box);
    check(box.describe()->source_conflict && *box.draft() && placed && !placed->description_lines.empty(),
          "A source update during a rejected confirmation keeps the draft and displays a conflict");
    check(box.use_latest() && !*box.draft() && !box.describe()->source_conflict,
          "Use latest adopts the checkbox source");
}

void option_conflicts_are_visible_and_can_use_latest() {
    edit_form::Form form(ids);
    auto& h = form.handles;
    h.mode.select({"focus"}); h.region.select({"central"});
    form.update_mode_source({"offline"}); form.update_region_source({"south"});
    test::FakeMeasure measure; ScrollState scroll;
    auto mode_frame = arrange(layout::control(h.mode), form.ui, *appearance, measure, 400, 400, 1.f, scroll);
    auto region_frame = arrange(layout::control(h.region), form.ui, *appearance, measure, 400, 400, 1.f, scroll);
    auto mode = mode_frame.find(h.mode); auto region = region_frame.find(h.region);
    check(h.mode.describe()->source_conflict && h.region.describe()->source_conflict && mode && region &&
          !mode->description_lines.empty() && !region->description_lines.empty() &&
          h.mode_latest.describe()->enabled && h.region_latest.describe()->enabled,
          "Conflicts preserve drafts, show notices and enable both latest actions");
    check(h.mode_latest.invoke() && h.region_latest.invoke() && *h.mode.draft() == OptionValue{"offline"} &&
          *h.region.draft() == OptionValue{"south"} && !h.mode.describe()->source_conflict && !h.region.describe()->source_conflict,
          "Each latest action adopts its own source value");
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

void v02_empty_single_and_navigation() {
    edit_form::Form form(ids, true);
    form.handles.tabs.activate({"v02"});
    auto& page = *form.list;
    auto& h = page.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    h.zero.invoke(); h.list.focus();
    host.key_down({Key::down}); host.key_down({Key::space});
    auto empty = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    check(empty.find(h.list) && h.list.describe()->focused && !h.list.describe()->candidate && !page.selection(),
          "V02 empty list is rendered, focusable and has no selection");
    host.key_down({Key::tab});
    check(!h.list.describe()->focused, "Tab leaves an empty V02 list");
    h.one.invoke(); h.list.focus(); host.key_down({Key::space}); host.key_down({Key::end});
    check(page.selection() == OptionValue{"T001"}, "Single-item endpoints stay on T001");
    h.twenty.invoke(); h.list.focus();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    const auto* item = frame.find(h.list);
    check(item && item->options.size() == 20, "Twenty V02 rows are arranged");
    auto row = item->options[2];
    check(frame.option_at(*item, row.x + 3, row.y + row.h / 2) == 2, "T003 has a visible pointer target");
    host.pointer_down(item->id, 2); host.pointer_up(item->id, 2);
    check(page.selection() == OptionValue{"T003"}, "Pointer selects T003");
    auto visible = [&](std::size_t index) {
        auto next = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
        auto* list = next.find(h.list);
        return list && index < list->options.size() && intersect(list->options[index], list->clip) == list->options[index];
    };
    host.key_down({Key::down});
    check(page.selection() == OptionValue{"T004"} && visible(3), "Down selects and reveals T004");
    host.key_down({Key::end});
    check(page.selection() == OptionValue{"T020"} && visible(19), "End selects and reveals T020");
    host.key_down({Key::home});
    check(page.selection() == OptionValue{"T001"} && visible(0), "Home selects and reveals T001");
}

void v02_identity_filter_and_delete() {
    edit_form::Form form(ids, true);
    form.handles.tabs.activate({"v02"});
    auto& page = *form.list; auto& h = page.handles;
    test::FakeMeasure measure;
    h.list.focus(); h.list.select({"T006"}); h.sort.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    check(frame.find(h.list) && page.selection() == OptionValue{"T006"} && h.detail.describe()->value.find(u"T006") != std::u16string::npos,
          "Reverse sorting retains the selected duplicate identity in the detail");
    h.sort.invoke();
    check(page.selection() == OptionValue{"T006"}, "Restoring sort preserves T006");
    h.list.select({"T003"}); h.search.set_value(u"範本 20");
    check(!page.selection() && h.list.describe()->options.size() == 1 && h.list.describe()->options[0].value == OptionValue{"T020"},
          "Filtering T003 out clears selection and leaves T020");
    h.search.set_value(u"");
    check(!page.selection() && h.list.describe()->options.size() == 20, "Clearing the filter does not restore selection");
    h.list.focus(); h.list.select({"T003"}); h.erase.invoke();
    const auto remaining = h.list.describe()->options;
    bool identities = remaining.size() == 19;
    for (int n = 1; n <= 20 && identities; ++n) {
        if (n == 3) continue;
        auto expected = "T" + std::string(n < 10 ? "00" : "0") + std::to_string(n);
        auto index = static_cast<std::size_t>(n < 3 ? n - 1 : n - 2);
        identities = remaining[index].value == OptionValue{expected};
    }
    check(!page.selection() && h.list.describe()->candidate == OptionValue{"T004"} && h.list.describe()->options.size() == 19 &&
          identities && h.erase.invoke().status == Status::disabled, "Deleting T003 keeps all other identities, clears selection and disables deletion");
    h.search.focus(); detail::access(form.ui).key_down({Key::tab});
    check(h.list.describe()->focused && !page.selection() && h.list.describe()->candidate == OptionValue{"T001"},
          "Tab reentry starts at T001 without selecting it");
}

void v02_disabled_and_filtered_neighbors() {
    edit_form::Form form(ids, true);
    form.handles.tabs.activate({"v02"});
    auto& page = *form.list; auto& h = page.handles;
    test::FakeMeasure measure;
    h.list.focus(); h.list.select({"T008"}); h.disable_eight.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    auto options = h.list.describe()->options;
    bool identities = options.size() == 20;
    for (int n = 1; n <= 20 && identities; ++n) {
        auto expected = "T" + std::string(n < 10 ? "00" : "0") + std::to_string(n);
        identities = options[static_cast<std::size_t>(n - 1)].value == OptionValue{expected};
    }
    check(frame.find(h.list) && !page.selection() && h.list.describe()->candidate == OptionValue{"T009"} &&
          h.list.select({"T008"}).status == Status::disabled && identities,
          "Disabling selected T008 clears it, moves to T009 and keeps every identity");
    h.reset.invoke(); h.hundred.invoke(); h.list.focus(); h.list.select({"T009"}); h.search.set_value(u"範本 1");
    check(!page.selection() && h.list.describe()->candidate == OptionValue{"T010"}, "Filtering recovers the next surviving identity T010");
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

void tooltip_keyboard_delay_and_escape() {
    Ui ui({});
    auto source = ui.add_button({.id = "source", .kind = Kind::button, .label = u"50%"});
    auto other = ui.add_button({.id = "other", .kind = Kind::button, .label = u"Other"});
    auto tip = ui.add_tooltip(source, u"Set progress to 50%", 100);
    check(source.describe()->description == u"Set progress to 50%", "The source exposes the tooltip as its semantic description");
    auto& host = detail::access(ui);
    host.tooltip_time(1000);
    host.window_active(true);
    source.focus();
    host.tooltip_time(1099);
    check(tip.visible() && !*tip.visible(), "Focus waits for the configured delay");
    host.tooltip_time(1100);
    check(tip.visible() && *tip.visible() && source.describe()->focused, "Tooltip appears without taking focus");
    host.key_down({Key::escape});
    host.tooltip_time(1300);
    check(!*tip.visible() && source.describe()->focused, "Escape hides for the current focus visit");
    other.focus(); source.focus();
    host.tooltip_time(1400);
    check(*tip.visible(), "A new focus visit can show the tooltip again");
}

void tooltip_pointer_visit_can_restart_after_escape() {
    Ui ui({});
    auto source = ui.add_button({.id = "source", .kind = Kind::button, .label = u"50%"});
    auto tip = ui.add_tooltip(source, u"Set progress to 50%", 100);
    auto& host = detail::access(ui);
    host.tooltip_time(0); host.window_active(true);
    host.tooltip_hover("source"); host.tooltip_time(100);
    check(*tip.visible(), "Pointer dwell displays the tooltip");
    host.key_down({Key::escape}); host.tooltip_time(500);
    check(!*tip.visible(), "Escape suppresses the same pointer visit");
    host.tooltip_hover(""); host.tooltip_hover("source"); host.tooltip_time(599);
    check(!*tip.visible(), "A fresh pointer visit waits again");
    host.tooltip_time(600);
    check(*tip.visible(), "A fresh pointer visit may show again");
    host.tooltip_hover("");
    check(!*tip.visible(), "Leaving the source closes the tooltip");
}

void tooltip_pointer_focus_ends_on_leave() {
    Ui ui({});
    auto source = ui.add_button({.id = "source", .kind = Kind::button, .label = u"50%"});
    auto tip = ui.add_tooltip(source, u"Help", 100);
    auto& host = detail::access(ui);
    host.tooltip_time(0); host.window_active(true);
    host.tooltip_hover("source");
    host.pointer_down("source"); host.pointer_up("source");
    host.tooltip_time(100);
    check(*tip.visible() && source.describe()->focused, "Pointer dwell shows the focused source tooltip");
    host.tooltip_hover("");
    check(!*tip.visible() && source.describe()->focused, "Leaving after a pointer click hides the tooltip without changing focus");
    host.key_down({Key::tab});
    host.tooltip_time(200);
    check(*tip.visible(), "Keyboard focus makes the source eligible again");
}

void tooltip_window_modal_and_source_lifetime() {
    Ui ui({});
    auto source = ui.add_button({.id = "source", .kind = Kind::button, .label = u"Source"});
    auto tip = ui.add_tooltip(source, u"Help", 100);
    auto& host = detail::access(ui);
    host.tooltip_time(0); host.window_active(true); source.focus(); host.tooltip_time(100);
    check(*tip.visible(), "Tooltip becomes visible");
    host.window_active(false); host.tooltip_time(1000);
    check(!*tip.visible(), "Window deactivation cancels the tooltip");
    host.window_active(true); host.tooltip_time(1099);
    check(!*tip.visible(), "Window reactivation starts a fresh delay");
    host.tooltip_time(1100);
    check(*tip.visible() && source.describe()->focused, "Tooltip returns without moving focus");
    host.modal(true);
    check(!*tip.visible(), "Opening a modal closes the tooltip");
    host.modal(false); host.tooltip_time(1200);
    check(*tip.visible(), "Eligibility is recalculated after a modal closes");
    source.enable(false); host.tooltip_time(1400);
    check(!*tip.visible(), "Disabling the source cancels pending and visible tooltips");
    source.enable(true); source.focus(); host.tooltip_time(1450); source.remove(); host.tooltip_time(2000);
    check(tip.visible().status == Status::not_found, "Removal invalidates the tooltip and its pending timer");
}

void tooltip_layout_is_readable_and_pointer_transparent() {
    Ui ui({});
    auto source = ui.add_button({.id = "source", .kind = Kind::button, .label = u"Source"});
    auto tip = ui.add_tooltip(source, u"An explanation that wraps near a window edge", 0);
    auto& host = detail::access(ui);
    host.window_active(true); source.focus();
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::column({layout::control(source)}, 0, 8);
    auto frame = arrange(root, ui, *appearance, measure, 160, 110, 1.f, scroll);
    check(*tip.visible() && frame.overlays.size() == 1, "A visible tooltip produces an overlay");
    auto& popup = frame.overlays.front();
    check(popup.bounds.x >= 0 && popup.bounds.y >= 0 && popup.bounds.x + popup.bounds.w <= 160 &&
          popup.bounds.y + popup.bounds.h <= 110 && popup.lines.size() > 1,
          "Tooltip wraps and stays inside a narrow window");
    for (const auto& line : popup.lines)
        check(intersect(line.bounds, popup.bounds) == line.bounds, "Each line remains within the tooltip");
    check(frame.hit(popup.bounds.x + 1, popup.bounds.y + 1) != &popup, "Tooltip does not intercept pointer input");
}

// Record the text sent to the component paint adapter, independently of layout's line lists.
struct PaintTextProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::u16string drawn;
    void fill(Rect, Color) override {}
    void rounded(Rect, int, Color) override {}
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color) override { drawn += value; }
};
void visible_text_agrees_with_semantics_on_each_page() {
    edit_form::Form form(ids, true);
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    for (auto page : {"v01", "v02", "v03"}) {
        form.handles.tabs.activate({page});
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
        for (const auto& item : frame.items) {
            auto semantic = host.describe(item.id);
            check(bool(semantic), "X04 every arranged control has semantics");
            PaintTextProbe paint(host, item, *semantic, *appearance, 1.f, true);
            detail::component_type(item.kind).paint(paint);
            auto contains = [&](std::u16string value) {
                value.erase(std::remove(value.begin(), value.end(), u'\n'), value.end());
                return value.empty() || paint.drawn.find(value) != std::u16string::npos;
            };
            if (item.kind == Kind::label || item.kind == Kind::text)
                check(contains(semantic->value), "X04 label or field value is sent to paint");
            if (item.kind == Kind::text || item.kind == Kind::button || item.kind == Kind::checkbox ||
                item.kind == Kind::radio_group || item.kind == Kind::dropdown || item.kind == Kind::slider || item.kind == Kind::progress)
                check(contains(semantic->name), "X04 component name is sent to paint");
            if (item.kind == Kind::dropdown || item.kind == Kind::slider || item.kind == Kind::progress)
                check(contains(semantic->value), "X04 value is sent to paint");
            if (item.kind == Kind::radio_group || item.kind == Kind::list || item.kind == Kind::tabs)
                for (const auto& option : semantic->options)
                    check(contains(option.label), "X04 option label is sent to paint");
        }
    }
}

void popup_and_scroll_matrix() {
    struct Size { int w, h; };
    for (auto size : {Size{960, 720}, Size{640, 480}}) for (float scale : {1.f, 1.5f}) {
        const int w = int(size.w * scale), height = int(size.h * scale);
        auto inside = [&](Rect r) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= height; };
        edit_form::Form form(ids, true);
        auto& h = form.handles;
        auto& host = detail::access(form.ui);
        test::FakeMeasure measure;
        auto draw = [&](std::string_view reveal = {}) {
            return arrange(form.page(), form.ui, *appearance, measure, w, height, scale, form.scroll, reveal);
        };
        auto dropdown_id = h.region.describe()->id;
        draw(dropdown_id);
        check(bool(h.region.open()), "X02 open dropdown after scroll reveal");
        auto dropdown = draw(dropdown_id);
        auto pop = std::find_if(dropdown.overlays.begin(), dropdown.overlays.end(), [](const Placement& p) { return p.popup && !p.tooltip; });
        check(pop != dropdown.overlays.end() && inside(pop->bounds) && !pop->options.empty(), "X02 dropdown stays in window after reveal");
        auto row = pop->options.front();
        auto hit = dropdown.hit(row.x + row.w / 2, row.y + row.h / 2);
        check(hit && hit->popup && hit->id == dropdown_id, "X02 dropdown receives the topmost hit");
        check(bool(h.region.cancel()), "X02 dropdown closes after interaction");

        check(bool(h.tabs.activate({"v02"})), "X02 open list page");
        auto& list = form.list->handles.list;
        check(bool(list.focus()), "X02 focus list");
        host.key_down({Key::end});
        auto list_frame = draw(list.describe()->id);
        auto* list_item = list_frame.find(list);
        check(list_item && !list_item->options.empty() && form.list->list_scroll.offset > 0 &&
              intersect(list_item->options.back(), list_item->clip) == list_item->options.back(),
              "X02 End scrolls nested list to the last visible row");

        check(bool(h.tabs.activate({"v03"})), "X02 open popup page");
        std::uint64_t now = 0;
        host.tooltip_clock([&] { return now; }); host.window_active(true);
        auto source_id = h.progress_half.describe()->id;
        draw(source_id); h.progress_half.focus(); now = 600; host.tooltip_time(now);
        auto tooltip = draw(source_id);
        auto tip = std::find_if(tooltip.overlays.begin(), tooltip.overlays.end(), [](const Placement& p) { return p.tooltip; });
        check(tip != tooltip.overlays.end() && inside(tip->bounds) && *h.progress_tip.visible(), "X02 tooltip appears inside the window after scroll reveal");
        check(bool(h.dialog_open.invoke()), "X02 open modal dialog");
        auto dialog = draw();
        auto* cancel = dialog.find(h.dialog_cancel);
        auto* dialog_hit = cancel ? dialog.hit(cancel->bounds.x + cancel->bounds.w / 2, cancel->bounds.y + cancel->bounds.h / 2) : nullptr;
        check(dialog.modal && inside(dialog.modal_panel) && cancel && dialog_hit && dialog_hit->id == cancel->id && !*h.progress_tip.visible(),
              "X02 dialog bounds and hit are valid and tooltip closes");
        check(bool(h.dialog_cancel.invoke()), "X02 cancel dialog");
    }
}

void repeated_v03_remove_reset_keeps_storage_bounded() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    h.tabs.activate({"v03"});
    const auto baseline = host.storage_counts();
    auto old = h.dialog_temporary;
    for (int i = 0; i < 1000; ++i) {
        check(bool(h.dialog_temporary.remove()) && bool(h.reset_v03.invoke()), "Remove and reset the V03 opener");
        check(host.storage_counts() == baseline && bool(h.dialog_temporary.describe()), "Reset has bounded component and page storage");
        check(old.describe().status == Status::not_found, "Original opener remains stale");
    }
}

void v03_reset_restores_the_temporary_source_and_keeps_other_pages() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    h.name.set_value(u"Retained V01 draft");
    h.tabs.activate({"v03"});
    auto old_source = h.dialog_temporary;
    h.dialog_temporary.invoke(); h.dialog_temporary.remove(); h.dialog_confirm.invoke();
    h.progress_full.invoke();
    check(form.progress() == 100 && form.dialog_confirmations() == 1 && !old_source.describe(),
          "The confirmed result and removed temporary source precede reset");
    h.reset_v03.invoke();
    check(form.progress() == 50 && form.dialog_confirmations() == 0 && h.dialog_result.describe()->value.find(u"No result") != std::u16string::npos &&
          h.dialog_temporary.describe() && !old_source.describe(), "Reset restores the initial model and creates a fresh temporary source");
    h.tabs.activate({"v01"});
    check(*h.name.draft() == u"Retained V01 draft", "Resetting V03 preserves the V01 draft");
}

void dialog_cancel_preserves_progress() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    h.tabs.activate({"v03"});
    check(h.progress_half.describe()->description == u"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002", "V03 tooltip uses the specified text");
    check(bool(h.dialog_open.invoke()), "Open dialog");
    check(h.dialog_title.describe()->value == u"\u91cd\u8a2d\u793a\u7bc4\u9032\u5ea6" && h.dialog_body.describe()->value == u"\u5c07\u9032\u5ea6\u6539\u70ba 0%\u3002\u5176\u4ed6\u9801\u9762\u8cc7\u6599\u6703\u4fdd\u7559\u3002" &&
          h.dialog_cancel.describe()->name == u"\u53d6\u6d88" && h.dialog_confirm.describe()->name == u"\u91cd\u8a2d\u9032\u5ea6",
          "Dialog uses the specified title and button text");
    check(h.dialog_cancel.describe()->focused, "Dialog starts on cancel");
    check(bool(h.dialog_cancel.invoke()), "Cancel dialog");
    check(form.progress() == 50 && form.dialog_confirmations() == 0 && h.dialog_open.describe()->focused,
          "Cancel preserves progress and returns focus to its opener");
}

void dialog_confines_input_and_confirms_once() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    h.tabs.activate({"v03"});
    h.dialog_open.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll);
    const auto* cancel = frame.find(h.dialog_cancel);
    check(cancel && frame.hit(cancel->bounds.x + 2, cancel->bounds.y + 2) == cancel && !frame.hit(1, 1),
          "Dialog controls are above an input-blocking mask");
    check(h.progress_full.invoke().status == Status::blocked_by_modal &&
          h.tabs.activate({"v01"}).status == Status::blocked_by_modal && form.progress() == 50,
          "Background operations and page switching are blocked");
    check(host.pointer_down(h.progress_full.describe()->id).status == Status::blocked_by_modal,
          "Background pointer input is blocked");
    host.key_down({Key::tab, true});
    check(h.dialog_confirm.describe()->focused, "Shift+Tab wraps within the dialog");
    host.key_down({Key::tab});
    check(h.dialog_cancel.describe()->focused, "Tab wraps back to cancel");
    host.key_down({Key::tab});
    check(h.dialog_confirm.describe()->focused, "Tab enters confirm");
    host.key_down({Key::enter}); host.key_down({Key::enter, false, false, true}); host.key_up(Key::enter);
    check(form.progress() == 0 && form.dialog_confirmations() == 1 && h.dialog_open.describe()->focused,
          "Confirm settles once and restores opener focus");
}

void dialog_escape_and_removed_opener() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    h.tabs.activate({"v03"});
    h.dialog_temporary.invoke();
    check(bool(h.dialog_temporary.remove()), "Remove the temporary opener with the dialog fixture");
    host.key_down({Key::escape});
    check(form.progress() == 50 && form.dialog_confirmations() == 0 && h.dialog_open.describe()->focused &&
          h.dialog_temporary.invoke().status == Status::not_found,
          "Escape cancels once and returns focus to the permanent fallback");
    check(bool(h.reset_v03.invoke()) && bool(h.dialog_temporary.invoke()) && h.dialog_cancel.describe()->focused,
          "Reset recreates the removed temporary opener");
}

void dialog_removed_opener_can_confirm() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    h.tabs.activate({"v03"}); h.dialog_temporary.invoke(); h.dialog_temporary.remove();
    check(bool(h.dialog_confirm.invoke()) && form.progress() == 0 && form.dialog_confirmations() == 1 &&
          h.dialog_open.describe()->focused && h.dialog_confirm.invoke().status != Status::ok,
          "Confirm settles once and restores the permanent fallback after removal");
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
        {"N01 long field label and error", long_field_label_and_error_keep_their_association},
        {"N03 radio boundaries in each appearance", radio_boundaries_under_the_appearance},
        {"N04 slider boundaries in each appearance", slider_non_divisible_under_the_appearance},
        {"N05 disabled tabs in each appearance", disabled_tabs_under_the_appearance},
        {"X01 keyboard journey", keyboard_completes_the_three_page_journey},
        {"X05 page reset and rerun", resets_reproduce_cases_and_preserve_other_pages},
        {"X04 painted text and semantic baseline", visible_text_agrees_with_semantics_on_each_page},
        {"X02 popup and scroll size/scale matrix", popup_and_scroll_matrix},
        {"P08 repeated removal and reset storage", repeated_v03_remove_reset_keeps_storage_bounded},
        {"P08 reset and page isolation", v03_reset_restores_the_temporary_source_and_keeps_other_pages},
        {"P06 tooltip layout", tooltip_layout_is_readable_and_pointer_transparent},
        {"C11 tooltip lifetime and window focus", tooltip_window_modal_and_source_lifetime},
        {"P05 tooltip pointer visit", tooltip_pointer_visit_can_restart_after_escape},
        {"P05 tooltip keyboard delay and Escape", tooltip_keyboard_delay_and_escape},
        {"P01 cancel dialog", dialog_cancel_preserves_progress},
        {"P02 P03 modal input and confirm", dialog_confines_input_and_confirms_once},
        {"P05 pointer focus tooltip", tooltip_pointer_focus_ends_on_leave},
        {"P04 removed opener and Escape", dialog_escape_and_removed_opener},
        {"P04 removed opener and confirm", dialog_removed_opener_can_confirm},
        {"P07 progress display and source", progress_displays_clamped_values_without_changing_the_model},
        {"L07 L08 R07 page scopes", pages_keep_independent_drafts_history_and_scroll},
        {"L01 L02 V02 navigation in each appearance", v02_empty_single_and_navigation},
        {"L03 L04 L05 R03 R10 V02 identities", v02_identity_filter_and_delete},
        {"N08 R11 V02 neighbors", v02_disabled_and_filtered_neighbors},
        {"F08 region confirmation and error order", region_is_in_the_form_confirmation_group},
        {"F01 five fields apply once", all_five_fields_apply_once},
        {"G11 checkbox conflict is visible and recoverable", checkbox_conflict_is_visible_and_can_use_latest},
        {"G11 option conflicts are visible and recoverable", option_conflicts_are_visible_and_can_use_latest},
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
