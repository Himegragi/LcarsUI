#include "x04_drivers.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
// Assembled showcase acceptance through public operations and the host input seam.
// Every case runs for both appearances and both the shipped and alternate ID sets (AC02).
#include "check.hpp"
#include "edit_form.hpp"
#include "fake_measure.hpp"
#include <cmath>
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

void assembled_checkbox_conflict_offers_latest() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    form.reject_next_apply_with_notices_source_change();
    check(bool(h.notices.toggle()), "X04 prepare checkbox draft");
    check(h.apply.invoke().status == Status::invalid_value && h.notices.describe()->source_conflict &&
          *h.notices.draft() && !form.notices_applied() && h.notices_latest.describe()->enabled,
          "X04 assembled rejected apply exposes the checkbox conflict and latest action");
    test::FakeMeasure measure;
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll, h.notices.describe()->id);
    auto* box = frame.find(h.notices);
    check(box && !box->description_lines.empty(), "X04 assembled checkbox draws conflict wording");
    check(bool(h.notices_latest.invoke()) && !*h.notices.draft() && !h.notices.describe()->source_conflict,
          "X04 assembled latest action adopts the external checkbox value");
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
    std::vector<Color> colors;
    void fill(Rect, Color color) override { colors.push_back(color); }
    void rounded(Rect, int, Color color) override { colors.push_back(color); }
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color color) override { drawn += value; colors.push_back(color); }
};
// Model the core paint adapter's clipping, including associated-label text that is
// emitted by the field rather than by a separate placement.
struct ClippedPaintProbe : detail::ComponentPaint {
    TextMeasure& measure;
    Rect current;
    const std::vector<Line>* associated;
    bool visible = false;
    ClippedPaintProbe(detail::UiState& state, const Placement& item, const Semantics& owner,
                      const Appearance& look, float scale, TextMeasure& metrics, const Semantics& target)
        : ComponentPaint(state, item, owner, look, scale, true), measure(metrics), current(item.clip), associated(nullptr) {
        if (!target.target.empty()) {
            if (target.role == TextRole::name) associated = &item.label_lines;
            else if (target.role == TextRole::description) associated = &item.description_lines;
            else if (target.role == TextRole::error) associated = &item.error_lines;
        }
    }
    void mark(Rect r) { auto v = intersect(r, current); visible = visible || (v.w > 0 && v.h > 0); }
    void fill(Rect r, Color) override { if (!associated) mark(r); }
    void rounded(Rect r, int, Color) override { if (!associated) mark(r); }
    void clip(Rect r) override { current = intersect(current, r); }
    void text(std::u16string_view value, int pixels, int x, int y, Color) override {
        if (value.empty()) return;
        if (associated && std::none_of(associated->begin(), associated->end(), [&](const Line& line) { return line.text == value; })) return;
        mark({x, y, static_cast<int>(std::ceil(measure.measure(value, pixels).width)), measure.line_height(pixels)});
    }
};
void painted_states_agree_with_semantics() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    struct Observation { std::u16string text; std::vector<Color> colors; };
    auto observe = [&](const auto& handle) {
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 4000, 1.f, form.scroll);
        auto* item = frame.find(handle);
        check(item != nullptr, "X04 state target is arranged");
        auto semantic = handle.describe();
        check(bool(semantic), "X04 state target has semantics");
        PaintTextProbe paint(host, *item, *semantic, *appearance, 1.f, true);
        detail::component_type(item->kind).paint(paint);
        return Observation{paint.drawn, paint.colors};
    };
    auto has_color = [](const Observation& o, Color expected) {
        return std::any_of(o.colors.begin(), o.colors.end(), [&](Color c) {
            return c.r == expected.r && c.g == expected.g && c.b == expected.b;
        });
    };
    auto contains = [](const Observation& o, const std::u16string& text) { return o.text.find(text) != std::u16string::npos; };
    const auto& wording = appearance->component_presentation();
    const auto palette = appearance->palette();

    h.name.focus(); h.name.select(0, 2);
    check(has_color(observe(h.name), palette.selection), "X04 selected text has a selection highlight");
    h.read_only.invoke();
    auto read_only = observe(h.name);
    check(h.name.describe()->read_only && contains(read_only, wording.read_only_suffix) &&
          contains(read_only, h.name.describe()->value), "X04 read-only field wording and value agree");
    h.disabled.invoke();
    auto disabled_name = observe(h.name);
    check(!h.name.describe()->enabled && contains(disabled_name, wording.disabled_suffix) && has_color(disabled_name, palette.muted),
          "X04 disabled field is identified and muted");
    auto disabled_button = observe(h.apply);
    auto disabled_box = observe(h.notices);
    auto disabled_radio = observe(h.mode);
    auto disabled_region = observe(h.region);
    auto disabled_volume = observe(h.volume);
    check(!h.apply.describe()->enabled && has_color(disabled_button, palette.muted) &&
          !h.notices.describe()->enabled && has_color(disabled_box, palette.muted) &&
          !h.mode.describe()->enabled && has_color(disabled_radio, palette.muted) &&
          !h.region.describe()->enabled && contains(disabled_region, wording.disabled_suffix) && has_color(disabled_region, palette.muted) &&
          !h.volume.describe()->enabled && contains(disabled_volume, wording.disabled_suffix) && has_color(disabled_volume, palette.muted),
          "X04 disabled controls use their restricted presentation");

    h.normal.invoke(); h.error.invoke();
    auto error = observe(h.name);
    check(h.name.describe()->invalid && contains(error, h.name.describe()->error) && has_color(error, palette.error),
          "X04 field error text and error color agree with its semantics");
    h.name.set_value(u"draft"); form.update_source(u"new source");
    h.mode.select({"focus"}); form.update_mode_source({"offline"});
    h.region.select({"central"}); form.update_region_source({"south"});
    h.volume.set_value(55); form.update_volume_source(70);
    check(h.name.describe()->source_conflict && contains(observe(h.state), u"來源已更新") &&
          h.latest.describe()->enabled && contains(observe(h.latest), u"改用最新值"),
          "X04 field conflict notice and use-latest action are drawn");
    check(h.mode.describe()->source_conflict && contains(observe(h.mode), wording.conflict_suffix) &&
          h.mode_latest.describe()->enabled && contains(observe(h.mode_latest), u"改用最新值") &&
          h.region.describe()->source_conflict && contains(observe(h.region), wording.conflict_suffix) &&
          h.region_latest.describe()->enabled && contains(observe(h.region_latest), u"改用最新值") &&
          h.volume.describe()->source_conflict && contains(observe(h.volume), wording.conflict_suffix) &&
          h.volume_latest.describe()->enabled && contains(observe(h.volume_latest), u"改用最新值"),
          "X04 option and slider conflicts expose their notices and actions");

    check(h.notices.describe()->checked == true && has_color(observe(h.notices), palette.accent),
          "X04 checked checkbox has an accent indicator");
    h.notices.toggle();
    check(h.notices.describe()->checked == false && !has_color(observe(h.notices), palette.accent),
          "X04 unchecked checkbox has no checked indicator");
    auto mode = h.mode.describe();
    check(std::any_of(mode->options.begin(), mode->options.end(), [](const auto& o) { return o.selected; }) &&
          has_color(observe(h.mode), palette.accent), "X04 radio selection has an accent indicator");
    h.offline_disabled.invoke();
    check(!h.mode.describe()->options[2].enabled && contains(observe(h.mode), wording.disabled_suffix),
          "X04 disabled radio option is identified in the paint text");
    h.reset.invoke(); h.region_empty.invoke(); h.apply.invoke();
    check(h.region.describe()->invalid && !h.region.describe()->error.empty() &&
          contains(observe(h.region), h.region.describe()->error) && has_color(observe(h.region), palette.error),
          "X04 empty dropdown error is painted with the semantic error and error color");

    h.tabs.activate({"v02"});
    auto& list = form.list->handles.list;
    check(!form.list->handles.erase.describe()->enabled && has_color(observe(form.list->handles.erase), palette.muted),
          "X04 disabled list delete button is muted");
    form.list->handles.zero.invoke();
    check(list.describe()->options.empty() && contains(observe(list), wording.no_items), "X04 empty list has visible empty wording");
    form.list->handles.twenty.invoke(); form.list->handles.disable_eight.invoke();
    check(!list.describe()->options[7].enabled && contains(observe(list), wording.disabled_suffix),
          "X04 disabled list row has disabled wording");
    list.select({"T003"});
    check(list.describe()->options[2].selected && has_color(observe(list), palette.selection) &&
          contains(observe(list), wording.selected_marker + list.describe()->options[2].label),
          "X04 selected list row has a selection highlight");
    check(h.tabs.describe()->options[1].selected && has_color(observe(h.tabs), palette.selection),
          "X04 active tab has a selection highlight");
    h.tabs.activate({"v03"}); h.dialog_open.invoke();
    check(contains(observe(h.dialog_title), h.dialog_title.describe()->value) &&
          contains(observe(h.dialog_body), h.dialog_body.describe()->value) &&
          contains(observe(h.dialog_cancel), h.dialog_cancel.describe()->name) &&
          contains(observe(h.dialog_confirm), h.dialog_confirm.describe()->name),
          "X04 open dialog text and button names are painted");
}
void hidden_and_clipped_components_are_not_visible_or_admitted() {
    edit_form::Form form(ids, true);
    auto& h = form.handles;
    test::FakeMeasure measure;
    auto frame = [&] { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll); };
    auto first = frame();
    check(!first.find(form.list->handles.list) && !first.find(h.progress) &&
          form.list->handles.list.select({"T001"}).status == Status::hidden && h.dialog_open.invoke().status == Status::hidden,
          "X04 other pages are absent from paint placements and reject operations");
    const auto* apply = first.find(h.apply);
    check(apply != nullptr, "X04 visible page contains the apply button");
    auto area = intersect(apply->bounds, apply->clip);
    check(area.w == 0 || area.h == 0, "X04 apply starts fully clipped in this viewport");
    check(!h.apply.describe()->visible && !form.list->handles.list.describe()->visible && h.tabs.describe()->visible,
          "X04 semantics distinguish a clipped component, a hidden page and visible tabs");
    auto* hit = first.hit(std::max(0, apply->bounds.x), std::max(0, apply->bounds.y));
    check(!hit || hit->id != apply->id, "X04 a clipped button is not a hit target");
    h.tabs.activate({"v02"});
    auto second = frame();
    check(!h.name.describe()->visible && second.find(h.tabs) && h.tabs.describe()->visible,
          "X04 semantic visibility follows the active page");
    check(!second.find(h.name) && !second.find(h.progress) && h.name.set_value(u"hidden").status == Status::hidden &&
          h.dialog_open.invoke().status == Status::hidden, "X04 V01 and V03 are absent and reject actions on V02");
    h.tabs.activate({"v03"});
    auto third = frame();
    check(!third.find(h.name) && !third.find(form.list->handles.list) &&
          h.name.set_value(u"hidden").status == Status::hidden && form.list->handles.list.select({"T001"}).status == Status::hidden,
          "X04 V01 and V02 are absent and reject actions on V03");
    auto& state = detail::access(form.ui);
    std::vector<std::vector<Id>> page_items;
    for (auto page : {"v01", "v02", "v03"}) {
        h.tabs.activate({page});
        auto current = frame();
        std::vector<Id> ids_on_page;
        for (const auto& item : current.items) if (item.id != h.tabs.describe()->id) ids_on_page.push_back(item.id);
        page_items.push_back(std::move(ids_on_page));
    }
    int page_index = 0;
    for (auto page : {"v01", "v02", "v03"}) {
        h.tabs.activate({page});
        auto current = frame();
        for (int other = 0; other < 3; ++other) if (other != page_index) for (const auto& id : page_items[other]) {
            check(!detail::find_placement(current, id) && state.available(id).status == Status::hidden,
                  "X04 each other-page placement is absent and not admitted");
        }
        ++page_index;
    }
}
// Every fixed-page member is checked after switching to each other page.
void x04_executing_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure;
        for (const auto& entry : x04::inventory(form)) {
            auto button = std::get_if<ButtonHandle>(&entry.handle);
            if (!button || !detail::component_type(button->describe()->kind).supports_state(detail::ComponentState::executing) ||
                !x04::execution_supported(form, *button)) continue;
            check(x04::baseline_context(form), "X04 executing baseline");
            auto baseline = x04::snapshot(form);
            auto before = button->describe(); auto message = std::string("X04 executing ") + before->id;
            check(x04::prepare_execution(form, entry), (message + " prepare").c_str());
            bool observed = false;
            check(bool(button->bind([&] {
                auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                     int(size.second * scale), scale, form.scroll, before->id);
                auto semantic = button->describe(); auto* item = frame.find(*button);
                if (!item) return Result{Status::failed, u"Executing button was not arranged"};
                PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
                detail::component_type(item->kind).paint(paint);
                observed = semantic->visible && semantic->executing &&
                    std::find(semantic->operations.begin(), semantic->operations.end(), Operation::activate) == semantic->operations.end() &&
                    button->invoke().status == Status::busy && paint.drawn.find(semantic->name) != std::u16string::npos &&
                    paint.drawn.find(appearance->component_presentation().executing_suffix) != std::u16string::npos;
                return Result{};
            })), (message + " bind").c_str());
            check(bool(button->invoke()) && observed && !*button->executing(), (message + " callback").c_str());
            check(x04::restore_execution(form, entry, before->enabled) && x04::same_model(baseline, x04::snapshot(form)),
                  (message + " restore").c_str());
        }
        check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.progress_zero.invoke()) && form.progress() == 0,
              "X04 restored action still invokes the application model");
    }
}

void x04_clipped_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); auto entries = x04::inventory(form);
        for (const auto& entry : entries) {
            if (entry.page == "shared" || entry.page == "modal") continue;
            auto baseline = x04::snapshot(form);
            auto semantic = x04::describe(entry.handle); auto message = std::string("X04 clipped ") + semantic->id;
            check(x04::prepare_clipped(form, entry), (message + " fixture").c_str());
            test::FakeMeasure measure;
            auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                int(size.second * scale), scale, form.scroll); };
            form.scroll.offset = 0; auto first = draw();
            auto rect = x04::clipped_rect(first, *x04::describe(entry.handle));
            check(rect && first.maximum > 0, (message + " reachable scroll region").c_str());
            bool fully = false, partial = false;
            for (int offset : x04::clipped_offsets(first, *rect)) {
                form.scroll.offset = offset; auto frame = draw(); semantic = x04::describe(entry.handle);
                auto geometry = x04::clipped_rect(frame, *semantic); if (!geometry) continue;
                auto* owner = x04::clipped_owner(frame, *semantic);
                auto intersection = intersect(*geometry, owner->clip);
                auto owner_semantic = detail::access(form.ui).describe(owner->id);
                ClippedPaintProbe paint(detail::access(form.ui), *owner, *owner_semantic, *appearance, scale, measure, *semantic);
                detail::component_type(owner->kind).paint(paint);
                if (intersection.w == 0 || intersection.h == 0) {
                    auto* hit = frame.hit(geometry->x + std::max(0, geometry->w / 2), geometry->y + std::max(0, geometry->h / 2));
                    check(!semantic->visible && !paint.visible && (!hit || hit->id != (semantic->target.empty() ? semantic->id : semantic->target)),
                          (message + " fully clipped semantics, paint and hit").c_str());
                    fully = true;
                } else if (intersection != *geometry) {
                    check(semantic->visible && paint.visible, (message + " partial semantics and paint").c_str()); partial = true;
                }
            }
            check(fully && partial, (message + " fully and partially clipped positions").c_str());
            check(x04::restore_clipped(form, entry) && x04::same_model(baseline, x04::snapshot(form)),
                  (message + " model restoration").c_str());
        }
    }
}

void x04_error_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form inventory_form(ids, true);
        auto entries = x04::inventory(inventory_form);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            edit_form::Form form(ids, true); auto entry = x04::inventory(form)[index];
            auto semantic = x04::describe(entry.handle);
            if (!detail::component_type(semantic->kind).supports_state(detail::ComponentState::error) || !x04::error_supported(*semantic)) continue;
            auto message = std::string("X04 error ") + semantic->id;
            check(x04::drive_error(form, entry), (message + " drive").c_str());
            test::FakeMeasure measure;
            auto owner = semantic->target.empty() ? semantic->id : semantic->target;
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, owner);
            semantic = x04::describe(entry.handle); auto* item = detail::find_placement(frame, owner);
            check(item && semantic->visible && x04::error_semantics(form, *semantic), (message + " semantics").c_str());
            auto owner_semantic = detail::access(form.ui).describe(owner);
            PaintTextProbe paint(detail::access(form.ui), *item, *owner_semantic, *appearance, scale, true);
            detail::component_type(item->kind).paint(paint);
            if (semantic->kind == Kind::tabs) {
                check(paint.drawn.find(appearance->component_presentation().invalid_tab_marker) != std::u16string::npos,
                      (message + " tab cue").c_str());
            } else {
                auto color = appearance->palette().error;
                check(paint.drawn.find(x04::error_message) != std::u16string::npos &&
                      std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { return c.r == color.r && c.g == color.g && c.b == color.b; }),
                      (message + " error text and cue").c_str());
            }
            check(x04::restore_error(form, entry) && !x04::error_semantics(form, *x04::describe(entry.handle)), (message + " restore").c_str());
        }
    }
}

void x04_candidate_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baseline_context(form), "X04 restore baseline page and focus");
            auto baseline = x04::snapshot(form);
            auto before = x04::describe(entry.handle);
            if (!detail::component_type(before->kind).supports_state(detail::ComponentState::candidate)) continue;
            auto message = std::string("X04 candidate ") + before->id;
            check(x04::prepare_candidate(form, entry), (message + " prepare").c_str());
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, before->id);
            check(x04::open_candidate(entry), (message + " open").c_str());
            state.key_down({before->kind == Kind::tabs ? Key::right : Key::down});
            frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                            int(size.second * scale), scale, form.scroll, before->id);
            auto semantic = x04::describe(entry.handle);
            auto* item = x04::candidate_placement(frame, *semantic);
            check(item && semantic->visible && semantic->candidate && x04::candidate_label(*semantic).has_value(),
                  (message + " semantics").c_str());
            PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
            detail::component_type(item->kind).paint(paint);
            auto cue = semantic->kind == Kind::dropdown ? appearance->palette().selection : appearance->palette().accent;
            check(paint.drawn.find(*x04::candidate_label(*semantic)) != std::u16string::npos &&
                  std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { return c.r == cue.r && c.g == cue.g && c.b == cue.b; }),
                  (message + " paint").c_str());
            if (semantic->kind == Kind::tabs || semantic->kind == Kind::dropdown)
                check(semantic->value == before->value, (message + " candidate preserves selection").c_str());
            check(x04::restore_candidate(form, entry) && x04::same_model(baseline, x04::snapshot(form)),
                  (message + " restore").c_str());
        }
    }
}

void x04_search_read_only_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure; auto& search = form.list->handles.search;
        check(x04::drive_search_read_only(form) && bool(search.focus()), "X04 drive search read-only");
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                             int(size.second * scale), scale, form.scroll, search.describe()->id);
        auto semantic = search.describe(); auto* item = frame.find(search);
        check(item && semantic->visible && semantic->read_only && semantic->enabled &&
              search.set_value(u"blocked").status == Status::read_only &&
              detail::access(form.ui).text_input(u"x").status == Status::read_only, "X04 search read-only restriction");
        PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
        detail::component_type(item->kind).paint(paint);
        check(paint.drawn.find(semantic->name) != std::u16string::npos &&
              paint.drawn.find(semantic->value) != std::u16string::npos &&
              paint.drawn.find(appearance->component_presentation().read_only_suffix) != std::u16string::npos,
              "X04 search read-only paint");
        check(x04::restore_search_read_only(form) && !search.describe()->read_only, "X04 search read-only recovery");
    }
}

void x04_modal_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        check(x04::open_modal(form), "X04 open modal");
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                             int(size.second * scale), scale, form.scroll);
        check(frame.modal && form.handles.progress_zero.invoke().status == Status::blocked_by_modal, "X04 modal blocks background");
        for (const auto& entry : x04::inventory(form)) {
            if (entry.page != "modal") continue;
            auto semantic = x04::describe(entry.handle); auto* item = x04::placement(frame, entry.handle);
            auto message = std::string("X04 modal ") + semantic->id;
            check(item && semantic->visible && x04::normal_semantics(*semantic), (message + " semantics").c_str());
            PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
            detail::component_type(item->kind).paint(paint);
            for (const auto& expected : x04::normal_text(*semantic))
                check(paint.drawn.find(expected) != std::u16string::npos, (message + " paint").c_str());
        }
        check(x04::close_modal(form) && !*form.handles.dialog.is_open(), "X04 close modal");
    }
}

void x04_tooltip_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        std::uint64_t now = 100; state.tooltip_clock([&] { return now; }); state.window_active(true);
        check(x04::prepare_tip(form), "X04 prepare tooltip");
        auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
            int(size.second * scale), scale, form.scroll, form.handles.progress_half.describe()->id); };
        draw(); now += 600; state.tooltip_time(now); auto frame = draw();
        auto tip = std::find_if(frame.overlays.begin(), frame.overlays.end(), [](const auto& p) { return p.tooltip; });
        check(*form.handles.progress_tip.visible() && tip != frame.overlays.end(), "X04 tooltip visible");
        std::u16string text; for (const auto& line : tip->lines) text += line.text;
        check(text.find(form.handles.progress_half.describe()->description) != std::u16string::npos &&
              (!frame.hit(tip->bounds.x + 1, tip->bounds.y + 1) || !frame.hit(tip->bounds.x + 1, tip->bounds.y + 1)->tooltip),
              "X04 tooltip text and pointer transparency");
        state.key_down({Key::escape});
        check(!*form.handles.progress_tip.visible() && form.handles.progress_half.describe()->focused, "X04 tooltip Escape");
        state.tooltip_clock({});
    }
}

void x04_normal_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baseline_context(form), "X04 restore baseline page and focus");
            bool modal = entry.page == "modal";
            if (modal) check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialog_open.invoke()), "X04 open normal modal");
            else if (entry.page != "shared") check(bool(form.handles.tabs.activate({std::string(entry.page)})), "X04 normal page");
            auto semantic = x04::describe(entry.handle); auto message = std::string("X04 normal ") + semantic->id;
            const auto& owner = semantic->target.empty() ? semantic->id : semantic->target;
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, owner);
            semantic = x04::describe(entry.handle);
            auto* item = detail::find_placement(frame, owner);
            if (!semantic->visible && semantic->kind == Kind::label && semantic->value.empty()) {
                check(item || semantic->target.empty(), (message + " empty label").c_str());
            } else {
                check(item && semantic->visible && x04::normal_semantics(*semantic), (message + " semantics").c_str());
                auto owner_semantic = state.describe(owner);
                PaintTextProbe paint(state, *item, *owner_semantic, *appearance, scale, true);
                detail::component_type(item->kind).paint(paint);
                for (const auto& expected : x04::normal_text(*semantic))
                    check(paint.drawn.find(expected) != std::u16string::npos, (message + " paint").c_str());
            }
            if (modal) check(bool(form.handles.dialog.settle(false)) && bool(form.handles.reset_v03.invoke()), "X04 close and reset normal modal");
            check(x04::baseline_context(form), "X04 restore normal page and focus");
        }
    }
}

void x04_disabled_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baseline_context(form), "X04 restore baseline page and focus");
            auto baseline = x04::snapshot(form);
            auto initial = x04::describe(entry.handle);
            if (!detail::component_type(initial->kind).supports_state(detail::ComponentState::disabled)) continue;
            if (initial->kind == Kind::tabs) {
                auto tabs = std::get<TabsHandle>(entry.handle);
                check(bool(tabs.activate({"v01"})) && bool(tabs.enable_page({"v02"}, false)) && bool(tabs.focus()), "X04 disable tab page");
                auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                     int(size.second * scale), scale, form.scroll);
                auto semantic = tabs.describe(); auto* item = frame.find(tabs);
                check(item && !semantic->options[1].enabled && tabs.activate({"v02"}).status == Status::disabled,
                      "X04 disabled tab restriction");
                PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
                detail::component_type(item->kind).paint(paint);
                check(paint.drawn.find(appearance->component_presentation().disabled_tab_marker) != std::u16string::npos,
                      "X04 disabled tab cue");
                state.pointer_down(semantic->id, 1);
                check(state.pointer_up(semantic->id, 1).status == Status::disabled && *tabs.active() == OptionValue{"v01"},
                      "X04 disabled tab rejects pointer");
                state.key_down({Key::right});
                check(tabs.describe()->candidate == OptionValue{"v03"} && *tabs.active() == OptionValue{"v01"},
                      "X04 keyboard skips disabled tab");
                check(bool(tabs.enable_page({"v02"}, true)) && bool(tabs.activate({"v02"})) && bool(tabs.activate({"v01"})),
                      "X04 disabled tab recovers");
                check(x04::baseline_context(form) && x04::same_model(baseline, x04::snapshot(form)), "X04 disabled tab restores model");
                continue;
            }
            if (entry.page == "modal") {
                check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialog_open.invoke()), "X04 open modal");
            } else check(bool(form.handles.tabs.activate({std::string(entry.page)})), "X04 activate page");
            auto message = std::string("X04 disabled ") + initial->id;
            check(x04::set_enabled(entry.handle, true) && x04::focus(entry.handle), (message + " prepare").c_str());
            check(x04::set_enabled(entry.handle, false), (message + " drive").c_str());
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, initial->id);
            auto semantic = x04::describe(entry.handle);
            auto* item = x04::placement(frame, entry.handle);
            check(item && semantic->visible && !semantic->enabled && !semantic->focused && x04::disabled_retry(entry.handle),
                  (message + " restriction").c_str());
            PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
            detail::component_type(item->kind).paint(paint);
            check(paint.drawn.find(appearance->component_presentation().disabled_suffix) != std::u16string::npos,
                  (message + " non-color cue").c_str());
            auto snapshot = x04::snapshot(form);
            check(state.pointer_down(semantic->id).status == Status::disabled, (message + " pointer").c_str());
            state.key_down({Key::space}); state.key_up(Key::space);
            auto after = x04::describe(entry.handle);
            check(after->value == semantic->value && x04::same_model(snapshot, x04::snapshot(form)), (message + " keyboard").c_str());
            check(x04::set_enabled(entry.handle, true) && x04::focus(entry.handle) &&
                  x04::set_enabled(entry.handle, initial->enabled), (message + " restore").c_str());
            if (semantic->kind == Kind::radio_group && initial->enabled) {
                auto radio = std::get<RadioGroupHandle>(entry.handle); auto previous = *radio.draft();
                check(previous && bool(radio.select({"focus"})) && bool(radio.enable_option({"offline"}, false)) && bool(radio.focus()),
                      "X04 prepare disabled radio option");
                auto option_frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                            int(size.second * scale), scale, form.scroll, initial->id);
                auto option_semantic = radio.describe(); auto* option_item = option_frame.find(radio);
                check(option_item && !option_semantic->options[2].enabled && radio.select({"offline"}).status == Status::disabled,
                      "X04 disabled radio option rejects public selection");
                PaintTextProbe option_paint(state, *option_item, *option_semantic, *appearance, scale, true);
                detail::component_type(option_item->kind).paint(option_paint);
                check(option_paint.drawn.find(option_semantic->options[2].label + appearance->component_presentation().disabled_suffix) != std::u16string::npos,
                      "X04 disabled radio option has a text cue");
                state.pointer_down(option_semantic->id, 2);
                check(state.pointer_up(option_semantic->id, 2).status == Status::disabled, "X04 disabled radio option rejects pointer");
                state.key_down({Key::right});
                check(*radio.draft() == OptionValue{"focus"}, "X04 keyboard skips disabled radio option");
                check(bool(radio.enable_option({"offline"}, true)) && bool(radio.select({"offline"})) && bool(radio.select(*previous)),
                      "X04 radio option recovers");
            }
            if (entry.page == "modal")
                check(bool(form.handles.dialog.settle(false)) && bool(form.handles.reset_v03.invoke()), "X04 close and reset modal");
            if (semantic->kind == Kind::list)
                check(x04::baseline_context(form) && bool(form.handles.tabs.activate({"v02"})) &&
                      bool(form.list->handles.zero.invoke()) && bool(form.list->handles.twenty.invoke()),
                      "X04 restore initial list candidate");
            check(x04::baseline_context(form) && x04::same_model(baseline, x04::snapshot(form)), (message + " baseline model").c_str());
        }
    }
}

void x04_group_conflicts_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); test::FakeMeasure measure;
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baseline_context(form), "X04 restore baseline page and focus");
            auto semantic = x04::describe(entry.handle);
            if (!detail::component_type(semantic->kind).supports_state(detail::ComponentState::conflict) ||
                !x04::group_conflict_supported(*semantic)) continue;
            auto message = std::string("X04 conflict ") + semantic->id;
            check(x04::drive_group_conflict(form, entry), (message + " drive").c_str());
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, semantic->id);
            auto* item = x04::placement(frame, entry.handle);
            semantic = x04::describe(entry.handle);
            check(item && semantic->visible && semantic->source_conflict, (message + " field visibility").c_str());
            PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
            detail::component_type(item->kind).paint(paint);
            check(paint.drawn.find(appearance->component_presentation().conflict_suffix) != std::u16string::npos &&
                  paint.drawn.find(semantic->name) != std::u16string::npos &&
                  (semantic->kind == Kind::checkbox || semantic->value.empty() || paint.drawn.find(semantic->value) != std::u16string::npos) &&
                  (semantic->kind != Kind::checkbox || (semantic->checked == true &&
                   std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { auto a = appearance->palette().accent; return c.r == a.r && c.g == a.g && c.b == a.b; }))),
                  (message + " field paint").c_str());
            auto latest = x04::latest_for(form, entry);
            check(latest.describe()->enabled, (message + " latest enabled").c_str());
            frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                            int(size.second * scale), scale, form.scroll, latest.describe()->id);
            auto* action = frame.find(latest); auto action_semantic = latest.describe();
            check(action && action_semantic->visible, (message + " latest visibility").c_str());
            PaintTextProbe action_paint(detail::access(form.ui), *action, *action_semantic, *appearance, scale, true);
            detail::component_type(action->kind).paint(action_paint);
            check(action_paint.drawn.find(action_semantic->name) != std::u16string::npos, (message + " latest paint").c_str());
            check(bool(latest.invoke()) && !x04::describe(entry.handle)->source_conflict &&
                  bool(form.handles.reset.invoke()) && x04::baseline_context(form), (message + " restore").c_str());
        }
    }
}

void x04_selected_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true);
        test::FakeMeasure measure;
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baseline_context(form), "X04 restore baseline page and focus");
            auto baseline = x04::snapshot(form);
            auto before = x04::describe(entry.handle);
            if (!detail::component_type(before->kind).supports_state(detail::ComponentState::selected)) continue;
            if (entry.page != "shared") check(bool(form.handles.tabs.activate({std::string(entry.page)})), "X04 select page");
            auto message = std::string("X04 selected ") + before->id;
            check(x04::drive_selected(entry), message.c_str());
            auto semantic = x04::describe(entry.handle);
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, semantic->id);
            const auto* item = x04::placement(frame, entry.handle);
            semantic = x04::describe(entry.handle);
            check(item && semantic->visible && x04::selected_semantics(*semantic), message.c_str());
            PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
            detail::component_type(item->kind).paint(paint);
            const auto cue = semantic->kind == Kind::checkbox || semantic->kind == Kind::radio_group
                ? appearance->palette().accent : appearance->palette().selection;
            check(std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) {
                return c.r == cue.r && c.g == cue.g && c.b == cue.b;
            }) && paint.drawn.find(semantic->kind == Kind::checkbox ? semantic->name : semantic->value) != std::u16string::npos,
                message.c_str());
            check(x04::restore_selected(form, entry) && x04::same_model(baseline, x04::snapshot(form)), message.c_str());
        }
    }
}

void x04_hidden_inventory_at_each_size_and_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true);
        test::FakeMeasure measure;
        for (auto page : {"v01", "v02", "v03"}) {
            check(bool(form.handles.tabs.activate({page})), "X04 activate page");
            auto frame = arrange(form.page(), form.ui, *appearance, measure,
                                 int(size.first * scale), int(size.second * scale), scale, form.scroll);
            for (const auto& entry : x04::inventory(form)) {
                if (entry.page == "shared" || entry.page == "modal" || entry.page == page) continue;
                auto semantic = x04::describe(entry.handle);
                auto message = std::string("X04 hidden ") + (semantic ? semantic->id : "missing") + " on " + page;
                check(semantic && !semantic->visible && !x04::placement(frame, entry.handle) &&
                      detail::access(form.ui).available(semantic->id).status == Status::hidden &&
                      x04::hidden_retry(entry.handle), message.c_str());
            }
        }
    }
}

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
        h.region.focus(); draw(dropdown_id);
        check(bool(h.region.open()), "X02 open dropdown after scroll reveal");
        auto dropdown = draw(dropdown_id);
        auto pop = std::find_if(dropdown.overlays.begin(), dropdown.overlays.end(), [](const Placement& p) { return p.popup && !p.tooltip; });
        check(pop != dropdown.overlays.end() && inside(pop->bounds) && !pop->options.empty(), "X02 dropdown stays in window after reveal");
        auto row = pop->options.front();
        auto hit = dropdown.hit(row.x + row.w / 2, row.y + row.h / 2);
        check(hit && hit->popup && hit->id == dropdown_id, "X02 dropdown receives the topmost hit");
        int wheel_before = form.scroll.offset;
        check(!dropdown.wheel(form.ui, row.x + row.w / 2, row.y + row.h / 2, -48) &&
              form.scroll.offset == wheel_before && h.region.describe()->expanded,
              "X02 wheel input inside the dropdown does not move the background or dismiss it");
        host.key_down({Key::down}); host.key_down({Key::down}); host.key_down({Key::escape});
        check(!h.region.describe()->expanded && h.region.describe()->focused && *h.region.draft() == OptionValue{"north"},
              "X02 Escape cancels the candidate and retains focus and value");
        h.region.open(); host.key_down({Key::down}); host.key_down({Key::down}); host.key_down({Key::tab});
        check(!h.region.describe()->expanded && h.volume.describe()->focused && *h.region.draft() == OptionValue{"north"},
              "X02 Tab cancels the candidate and advances focus");
        h.region.focus(); h.region.open(); host.key_down({Key::down}); host.key_down({Key::down});
        auto outside_frame = draw(dropdown_id);
        const auto* tabs = outside_frame.find(h.tabs);
        auto tab = tabs->options.front();
        auto* outside_hit = outside_frame.hit(tab.x + tab.w / 2, tab.y + tab.h / 2);
        check(outside_hit && outside_hit->id == h.tabs.describe()->id && !outside_hit->popup, "X02 outside hit reaches the page tab");
        host.close_popup(); host.pointer_down(outside_hit->id, outside_frame.option_at(*outside_hit, tab.x + tab.w / 2, tab.y + tab.h / 2));
        host.pointer_up(outside_hit->id, outside_frame.option_at(*outside_hit, tab.x + tab.w / 2, tab.y + tab.h / 2));
        check(!h.region.describe()->expanded && h.tabs.describe()->focused && *h.region.draft() == OptionValue{"north"} &&
              *h.tabs.active() == OptionValue{"v01"}, "X02 outside click cancels and still activates its target");

        h.region.focus(); auto scroll_frame = draw(dropdown_id);
        h.region.open(); host.key_down({Key::down}); host.key_down({Key::down});
        check(h.region.describe()->candidate == OptionValue{"south"}, "X02 scroll starts with an unconfirmed candidate");
        int before = form.scroll.offset;
        int destination = before < scroll_frame.maximum ? before + 1 : before - 1;
        check(!scroll_frame.scroll_to(form.ui, form.scroll, before) && h.region.describe()->expanded,
              "X02 a stationary viewport does not cancel the popup");
        check(scroll_frame.scroll_to(form.ui, form.scroll, destination) && !h.region.describe()->expanded &&
              !h.region.describe()->candidate && h.region.describe()->focused && *h.region.draft() == OptionValue{"north"},
              "X02 a moving viewport cancels the dropdown without changing focus or value");
        h.region.open(); host.key_down({Key::down}); host.key_down({Key::down});
        auto wheel_frame = draw();
        auto viewport = wheel_frame.regions.front().clip;
        int wheel_step = form.scroll.offset + 3 <= wheel_frame.maximum ? -1 : 1;
        check(wheel_frame.wheel(form.ui, viewport.x + 1, viewport.y + 1, wheel_step) &&
              !h.region.describe()->expanded && !h.region.describe()->candidate && h.region.describe()->focused &&
              *h.region.draft() == OptionValue{"north"}, "X02 wheel outside the popup cancels through the shared frame path");
        int after_first_wheel = form.scroll.offset;
        check(wheel_frame.wheel(form.ui, viewport.x + 1, viewport.y + 1, wheel_step) &&
              form.scroll.offset == after_first_wheel - wheel_step,
              "X02 consecutive wheel messages accumulate before the next frame");
        auto stale_popup = std::find_if(wheel_frame.overlays.begin(), wheel_frame.overlays.end(),
                                        [](const Placement& p) { return p.popup && !p.tooltip; });
        if (stale_popup != wheel_frame.overlays.end()) {
            int x = stale_popup->bounds.x + stale_popup->bounds.w / 2;
            int y = stale_popup->bounds.y + stale_popup->bounds.h / 2;
            if (viewport.contains(x, y))
                check(wheel_frame.wheel(form.ui, x, y, wheel_step),
                      "X02 a dismissed popup no longer intercepts wheel input before repaint");
        }

        // Choose real offsets from the assembled page's own geometry. If the page cannot
        // expose an edge at this size, don't invent extra overflow to manufacture a sample.
        h.long_text.invoke();
        for (bool partial : {true, false}) {
            auto geometry = draw();
            const auto* anchor = geometry.find(h.region);
            int original_top = anchor->bounds.y + form.scroll.offset;
            int half = std::max(1, anchor->bounds.h / 2);
            int top_edge = original_top + anchor->bounds.h - anchor->clip.y;
            int bottom_edge = original_top - (anchor->clip.y + anchor->clip.h);
            struct Movement { int before, after; };
            std::optional<Movement> movement;
            for (auto candidate : {Movement{top_edge - (partial ? half : 0) - 1, top_edge - (partial ? half : 0)},
                                   Movement{bottom_edge + (partial ? half : 0) + 1, bottom_edge + (partial ? half : 0)}}) {
                if (candidate.before < 0 || candidate.after < 0 || candidate.before > geometry.maximum || candidate.after > geometry.maximum) continue;
                int y = original_top - candidate.before;
                if (y < anchor->clip.y + anchor->clip.h && y + anchor->bounds.h > anchor->clip.y) { movement = candidate; break; }
            }
            if (!movement) {
                std::cout << "X02 dropdown " << (partial ? "partial" : "clipped") << " anchor unreachable at "
                          << size.w << 'x' << size.h << ' ' << int(scale * 100) << "% with this assembled page\n";
                continue;
            }
            geometry.scroll_to(form.ui, form.scroll, movement->before); geometry = draw();
            h.region.open(); host.key_down({Key::down}); host.key_down({Key::down});
            check(h.region.describe()->expanded && h.region.describe()->candidate == OptionValue{"south"},
                  "X02 edge sample opens with an unconfirmed dropdown candidate");
            check(geometry.scroll_to(form.ui, form.scroll, movement->after), "X02 edge sample moves the viewport");
            auto after = draw(); anchor = after.find(h.region);
            auto visible_area = intersect(anchor->bounds, anchor->clip);
            bool correct_edge = partial ? visible_area.h > 0 && visible_area.h < anchor->bounds.h : visible_area.h == 0;
            check(correct_edge && !h.region.describe()->expanded && !h.region.describe()->candidate &&
                  h.region.describe()->focused && *h.region.draft() == OptionValue{"north"},
                  "X02 edge scroll cancels the dropdown and preserves value and focus");
        }
        // A focused source scrolled entirely out cannot show a tooltip while it is clipped.
        h.region.focus(); auto visible_frame = draw(dropdown_id);
        std::uint64_t tip_now = 0;
        host.tooltip_clock([&] { return tip_now; }); host.window_active(true);
        auto region_tip = form.ui.add_tooltip(h.region, u"Region help", 100);
        tip_now = 100; host.tooltip_time(tip_now);
        auto* tip_anchor = visible_frame.find(h.region);
        int top_cutoff = tip_anchor->bounds.y + form.scroll.offset + tip_anchor->bounds.h - tip_anchor->clip.y;
        int bottom_cutoff = tip_anchor->bounds.y + form.scroll.offset - tip_anchor->clip.y - tip_anchor->clip.h;
        int clipped_offset = top_cutoff >= 0 && top_cutoff <= visible_frame.maximum ? top_cutoff :
                             bottom_cutoff >= 0 && bottom_cutoff <= visible_frame.maximum ? bottom_cutoff : -1;
        if (clipped_offset >= 0 && clipped_offset != form.scroll.offset) {
            int previous = form.scroll.offset;
            check(*region_tip.visible() && visible_frame.scroll_to(form.ui, form.scroll, clipped_offset),
                  "X02 start with a visible tooltip and scroll its source completely out");
            auto clipped_frame = draw(); tip_now += 200; host.tooltip_time(tip_now);
            check(!*region_tip.visible() && h.region.describe()->focused,
                  "X02 tooltip stays hidden while its focused source is fully clipped");
            clipped_frame.scroll_to(form.ui, form.scroll, previous); draw();
            tip_now += 99; host.tooltip_time(tip_now);
            check(!*region_tip.visible(), "X02 returning a clipped source starts a new dwell");
            ++tip_now; host.tooltip_time(tip_now);
            check(*region_tip.visible(), "X02 tooltip returns only after the new dwell");
        }
        host.tooltip_clock({});
        h.long_text.invoke();

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
        if (tooltip.maximum > 0) {
            int current = form.scroll.offset;
            int next = current < tooltip.maximum ? current + 1 : current - 1;
            check(tooltip.scroll_to(form.ui, form.scroll, next) && !*h.progress_tip.visible() && h.progress_half.describe()->focused,
                  "X02 viewport movement hides the tooltip without moving focus");
            now += 499; host.tooltip_time(now);
            check(!*h.progress_tip.visible(), "X02 tooltip needs a fresh full dwell after scrolling");
            now += 1; host.tooltip_time(now);
            check(*h.progress_tip.visible(), "X02 tooltip may reappear after the fresh dwell");
        }
        host.key_down({Key::escape});
        check(!*h.progress_tip.visible() && h.progress_half.describe()->focused && form.progress() == 50,
              "X02 tooltip Escape preserves focus and value");
        h.dialog_open.focus(); h.progress_half.focus(); now += 600; host.tooltip_time(now);
        check(*h.progress_tip.visible(), "X02 tooltip can return on a new focus visit");
        h.dialog_open.focus();
        check(!*h.progress_tip.visible() && h.dialog_open.describe()->focused && form.progress() == 50,
              "X02 tooltip closes on focus leave");
        host.tooltip_hover(source_id); host.pointer_down(source_id); host.pointer_up(source_id);
        now += 600; host.tooltip_time(now);
        check(*h.progress_tip.visible(), "X02 tooltip appears on a pointer visit");
        host.tooltip_hover({});
        check(!*h.progress_tip.visible() && h.progress_half.describe()->focused && form.progress() == 50,
              "X02 tooltip pointer leave preserves focus and value");
        auto before_modal = draw();
        check(bool(h.dialog_open.invoke()), "X02 open modal dialog");
        if (before_modal.maximum > 0) {
            int current = form.scroll.offset;
            int next = current < before_modal.maximum ? current + 1 : current - 1;
            auto viewport = before_modal.regions.front().clip;
            int delta = next > current ? -1 : 1;
            check(!before_modal.scroll_to(form.ui, form.scroll, next) &&
                  !before_modal.wheel(form.ui, viewport.x + 1, viewport.y + 1, delta) && form.scroll.offset == current,
                  "X02 a newly opened modal blocks scrolling through the preceding frame");
        }
        auto dialog = draw();
        auto* cancel = dialog.find(h.dialog_cancel);
        auto* dialog_hit = cancel ? dialog.hit(cancel->bounds.x + cancel->bounds.w / 2, cancel->bounds.y + cancel->bounds.h / 2) : nullptr;
        check(dialog.modal && inside(dialog.modal_panel) && cancel && dialog_hit && dialog_hit->id == cancel->id && !*h.progress_tip.visible(),
              "X02 dialog bounds and hit are valid and tooltip closes");
        if (dialog.maximum > 0) {
            int current = form.scroll.offset;
            int next = current < dialog.maximum ? current + 1 : current - 1;
            check(!dialog.scroll_to(form.ui, form.scroll, next) && form.scroll.offset == current,
                  "X02 a modal blocks a movable background viewport");
        } else {
            std::cout << "X02 modal background scroll N/A: no overflow at " << size.w << 'x' << size.h << ' ' << int(scale * 100) << "%\n";
        }
        host.key_down({Key::escape});
        check(!*h.dialog.is_open() && h.dialog_open.describe()->focused && form.progress() == 50 && form.dialog_confirmations() == 0,
              "X02 Escape cancels the dialog and restores focus");
        h.dialog_open.invoke(); h.dialog_cancel.invoke();
        check(!*h.dialog.is_open() && h.dialog_open.describe()->focused && form.progress() == 50 && form.dialog_confirmations() == 0,
              "X02 Cancel restores focus and preserves progress");
        h.dialog_open.invoke(); h.dialog_confirm.invoke();
        check(!*h.dialog.is_open() && h.dialog_open.describe()->focused && form.progress() == 0 && form.dialog_confirmations() == 1,
              "X02 Confirm restores focus and changes progress once");
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

// The same assembled state driver is exercised by the real-host observation.
// A changed source must be identifiable at the field as well as recoverable.
void x04_name_conflict_is_drawn_at_field() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); auto& h = form.handles;
        check(bool(x04::name_conflict.drive(form)) && h.name.describe()->source_conflict && h.latest.describe()->enabled,
              "An external source change retains a conflicting name draft and offers latest");
        test::FakeMeasure measure;
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale), int(size.second * scale), scale, form.scroll, h.name.describe()->id);
        auto* field = frame.find(h.name);
        std::u16string description, caption;
        if (field) {
            for (const auto& line : field->description_lines) description += line.text;
            for (const auto& line : field->label_lines) caption += line.text;
        }
        auto semantic = h.name.describe();
        auto cue = appearance->component_presentation().conflict_suffix;
        bool associated = h.name_label.describe()->target == semantic->id && h.name_label.describe()->role == TextRole::name &&
            h.name_help.describe()->target == semantic->id && h.name_help.describe()->role == TextRole::description &&
            h.name_error.describe()->target == semantic->id && h.name_error.describe()->role == TextRole::error && semantic->labels.size() == 3;
        check(field && semantic->visible && semantic->enabled && !semantic->read_only && !semantic->invalid && associated &&
              semantic->value == u"Draft" && caption.find(semantic->name) != std::u16string::npos &&
              description.find(semantic->description) != std::u16string::npos && !cue.empty() && description.find(cue) != std::u16string::npos,
              "The conflicting name field retains its value and associations and lays out the source-change cue");
        check(bool(x04::name_conflict.restore(form)) && !h.name.describe()->source_conflict && *h.name.draft() == u"Latest",
              "The latest action restores the field to the current source");
    }
}

void x04_name_read_only_size_scale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true); auto& name = form.handles.name;
        check(bool(x04::name_read_only.drive(form)), "X04 drive name read-only through application fixture");
        check(bool(name.focus()), "Read-only name remains focusable");
        test::FakeMeasure measure;
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale), int(size.second * scale), scale, form.scroll, name.describe()->id);
        auto* field = frame.find(name); auto semantic = name.describe();
        std::u16string caption, description;
        if (field) {
            for (const auto& line : field->label_lines) caption += line.text;
            for (const auto& line : field->description_lines) description += line.text;
        }
        auto suffix = appearance->component_presentation().read_only_suffix;
        auto& h = form.handles;
        bool associated = h.name_label.describe()->target == semantic->id && h.name_label.describe()->role == TextRole::name &&
            h.name_help.describe()->target == semantic->id && h.name_help.describe()->role == TextRole::description &&
            h.name_error.describe()->target == semantic->id && h.name_error.describe()->role == TextRole::error && semantic->labels.size() == 3;
        check(field && semantic->visible && semantic->enabled && semantic->read_only && !semantic->invalid && !semantic->source_conflict && associated &&
              semantic->value == *name.draft() && !suffix.empty() && description.find(semantic->description) != std::u16string::npos &&
              caption.find(semantic->name) != std::u16string::npos && caption.find(suffix) != std::u16string::npos &&
              name.set_value(u"rejected").status == Status::read_only,
              "X04 read-only semantic restriction and label cue agree at each core size and scale");
        check(bool(x04::name_read_only.restore(form)) && !name.describe()->read_only,
              "X04 restore name after read-only state");
    }
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
        {"X04 name conflict has a field cue", x04_name_conflict_is_drawn_at_field},
        {"X04 name read-only at each size and scale", x04_name_read_only_size_scale},
        {"X04 executing inventory at each size and scale", x04_executing_inventory_at_each_size_and_scale},
        {"X04 clipped inventory at each size and scale", x04_clipped_inventory_at_each_size_and_scale},
        {"X04 error inventory at each size and scale", x04_error_inventory_at_each_size_and_scale},
        {"X04 candidate inventory at each size and scale", x04_candidate_inventory_at_each_size_and_scale},
        {"X04 search read-only at each size and scale", x04_search_read_only_at_each_size_and_scale},
        {"X04 modal inventory at each size and scale", x04_modal_inventory_at_each_size_and_scale},
        {"X04 tooltip at each size and scale", x04_tooltip_at_each_size_and_scale},
        {"X04 normal inventory at each size and scale", x04_normal_inventory_at_each_size_and_scale},
        {"X04 disabled inventory at each size and scale", x04_disabled_inventory_at_each_size_and_scale},
        {"X04 group conflicts at each size and scale", x04_group_conflicts_at_each_size_and_scale},
        {"X04 selected inventory at each size and scale", x04_selected_inventory_at_each_size_and_scale},
        {"X04 hidden inventory at each size and scale", x04_hidden_inventory_at_each_size_and_scale},
        {"X04 painted text and semantic baseline", visible_text_agrees_with_semantics_on_each_page},
        {"X04 painted state and modal text", painted_states_agree_with_semantics},
        {"X04 hidden pages and clipped targets", hidden_and_clipped_components_are_not_visible_or_admitted},
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
        {"X04 assembled checkbox conflict and latest action", assembled_checkbox_conflict_offers_latest},
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
