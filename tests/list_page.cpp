#include "list_page.hpp"
#include "core_internal.hpp"
#include "check.hpp"
using namespace lcars;
using test::check;

void duplicate_names_survive_sorting() {
    list_page::Page page;
    check(bool(page.handles.list.select({"T006"})), "Select T006");
    check(bool(page.handles.sort.invoke()), "Reverse the order");
    check(page.selection() == OptionValue{"T006"}, "The second shared-name identity survives sorting");
    check(page.handles.sort.invoke() && page.selection() == OptionValue{"T006"}, "Restoring order keeps identity");
}
void deletion_recovers_cursor_then_reentry_starts_at_first() {
    list_page::Page page; auto& list = page.handles.list;
    list.focus(); list.select({"T003"});
    check(bool(page.handles.erase.invoke()), "Delete the selected item");
    check(!page.selection() && list.describe()->candidate == OptionValue{"T004"}, "Deletion clears selection and focuses T004");
    check(page.handles.erase.invoke().status == Status::disabled, "Deleting again is disabled");
    page.handles.search.focus(); list.focus();
    check(!page.selection() && list.describe()->candidate == OptionValue{"T001"}, "Reentry without selection starts at the first item");
}
void filtering_clears_selection_without_restoring_it() {
    list_page::Page page; auto& list = page.handles.list;
    list.focus(); list.select({"T003"});
    check(bool(page.handles.search.set_value(u"範本 20")), "Filter by name");
    check(!page.selection() && list.describe()->options.size() == 1 && list.describe()->options[0].value == OptionValue{"T020"}, "Filtering clears the hidden selection");
    check(bool(page.handles.search.set_value(u"")) && !page.selection(), "Clearing search does not restore it");
}
void disabling_a_selected_row_clears_it() {
    list_page::Page page; auto& list = page.handles.list;
    list.focus(); list.select({"T008"});
    check(bool(page.handles.disable_eight.invoke()), "Disable T008");
    check(!page.selection() && list.describe()->candidate == OptionValue{"T009"}, "Disabled selection clears and cursor advances");
    check(list.select({"T008"}).status == Status::disabled, "Disabled row cannot be selected");
    list.select({"T007"}); detail::access(page.ui).key_down({.key = Key::down});
    check(page.selection() == OptionValue{"T009"}, "Down skips disabled T008");
}
void empty_single_and_keyboard_endpoints() {
    list_page::Page page; auto& list = page.handles.list; auto& host = detail::access(page.ui);
    page.handles.zero.invoke(); list.focus();
    host.key_down({.key = Key::down}); host.key_down({.key = Key::space});
    check(list.describe()->focused && !list.describe()->candidate && !page.selection(), "Empty list accepts focus and never selects");
    host.key_down({.key = Key::tab});
    check(!list.describe()->focused, "Tab exits the empty list");
    page.handles.one.invoke(); list.focus(); host.key_down({.key = Key::space});
    check(page.selection() == OptionValue{"T001"}, "Space selects the only row");
    page.handles.twenty.invoke(); list.focus(); list.select({"T003"}); host.key_down({.key = Key::down});
    bool down = page.selection() == OptionValue{"T004"}; host.key_down({.key = Key::end});
    bool end = page.selection() == OptionValue{"T020"}; host.key_down({.key = Key::home});
    check(down && end && page.selection() == OptionValue{"T001"}, "Down, End and Home select the expected identities");
}
void filtering_uses_old_order_for_cursor_recovery() {
    list_page::Page page; page.handles.hundred.invoke(); auto& list = page.handles.list;
    list.focus(); list.select({"T009"}); page.handles.search.set_value(u"範本 1");
    check(!page.selection() && list.describe()->candidate == OptionValue{"T010"}, "T010 is the next surviving item after filtering T009");
}
int main() {
    const test::Case cases[] = {
        {"L03 duplicate names and sorting", duplicate_names_survive_sorting},
        {"L05 R03 R10 deletion and reentry", deletion_recovers_cursor_then_reentry_starts_at_first},
        {"L04 filtering clears selection", filtering_clears_selection_without_restoring_it},
        {"R09 N08 disabling selected row", disabling_a_selected_row_clears_it},
        {"L01 L02 empty single and keyboard endpoints", empty_single_and_keyboard_endpoints},
        {"R11 filtering uses old order", filtering_uses_old_order_for_cursor_recovery},
    };
    return test::run("list_page", cases);
}
