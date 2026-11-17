#include "list_page.hpp"
#include "core_internal.hpp"
#include "check.hpp"
using namespace isoduo;
using test::check;

void duplicateNamesSurviveSorting() {
    listPage::Page page;
    check(bool(page.handles.list.select({"T006"})), "Select T006");
    check(bool(page.handles.sort.invoke()), "Reverse the order");
    check(page.selection() == OptionValue{"T006"}, "The second shared-name identity survives sorting");
    check(page.handles.sort.invoke() && page.selection() == OptionValue{"T006"}, "Restoring order keeps identity");
}
void deletionRecoversCursorThenReentryStartsAtFirst() {
    listPage::Page page; auto& list = page.handles.list;
    list.focus(); list.select({"T003"});
    check(bool(page.handles.erase.invoke()), "Delete the selected item");
    check(!page.selection() && list.describe()->candidate == OptionValue{"T004"}, "Deletion clears selection and focuses T004");
    check(page.handles.erase.invoke().status == Status::disabled, "Deleting again is disabled");
    page.handles.search.focus(); list.focus();
    check(!page.selection() && list.describe()->candidate == OptionValue{"T001"}, "Reentry without selection starts at the first item");
}
void filteringClearsSelectionWithoutRestoringIt() {
    listPage::Page page; auto& list = page.handles.list;
    list.focus(); list.select({"T003"});
    check(bool(page.handles.search.setValue(u"範本 20")), "Filter by name");
    check(!page.selection() && list.describe()->items.size() == 1 && list.describe()->items[0].value == OptionValue{"T020"}, "Filtering clears the hidden selection");
    check(bool(page.handles.search.setValue(u"")) && !page.selection(), "Clearing search does not restore it");
}
void sampleResetClearsImmediateSearch() {
    listPage::Page page; auto& search = page.handles.search;
    check(page.searchConnection().owns(search), "Search is attached to the immediate-only connection");
    check(bool(search.setValue(u"20")) && !search.describe()->sourceConflict, "Search writes through immediately");
    check(bool(page.handles.twenty.invoke()) && search.describe()->value.empty() &&
          !search.describe()->sourceConflict && page.handles.list.describe()->items.size() == 20,
          "Sample reset clears search and restores the list without a conflict");
    check(search.undo().status == Status::emptyHistory, "Sample reset clears the old search edit history");
}

void disablingASelectedRowClearsIt() {
    listPage::Page page; auto& list = page.handles.list;
    list.focus(); list.select({"T008"});
    check(bool(page.handles.disableEight.invoke()), "Disable T008");
    check(!page.selection() && list.describe()->candidate == OptionValue{"T009"}, "Disabled selection clears and cursor advances");
    check(list.select({"T008"}).status == Status::disabled, "Disabled row cannot be selected");
    list.select({"T007"}); detail::access(page.ui).keyDown({.key = Key::down});
    check(page.selection() == OptionValue{"T009"}, "Down skips disabled T008");
}
void emptySingleAndKeyboardEndpoints() {
    listPage::Page page; auto& list = page.handles.list; auto& host = detail::access(page.ui);
    page.handles.zero.invoke(); list.focus();
    host.keyDown({.key = Key::down}); host.keyDown({.key = Key::space});
    check(list.describe()->focused && !list.describe()->candidate && !page.selection(), "Empty list accepts focus and never selects");
    host.keyDown({.key = Key::tab});
    check(!list.describe()->focused, "Tab exits the empty list");
    page.handles.one.invoke(); list.focus(); host.keyDown({.key = Key::space});
    check(page.selection() == OptionValue{"T001"}, "Space selects the only row");
    page.handles.twenty.invoke(); list.focus(); list.select({"T003"}); host.keyDown({.key = Key::down});
    bool down = page.selection() == OptionValue{"T004"}; host.keyDown({.key = Key::end});
    bool end = page.selection() == OptionValue{"T020"}; host.keyDown({.key = Key::home});
    check(down && end && page.selection() == OptionValue{"T001"}, "Down, End and Home select the expected identities");
}
void filteringUsesOldOrderForCursorRecovery() {
    listPage::Page page; page.handles.hundred.invoke(); auto& list = page.handles.list;
    list.focus(); list.select({"T009"}); page.handles.search.setValue(u"範本 1");
    check(!page.selection() && list.describe()->candidate == OptionValue{"T010"}, "T010 is the next surviving item after filtering T009");
}
int main() {
    const test::Case cases[] = {
        {"L03 duplicate names and sorting", duplicateNamesSurviveSorting},
        {"L05 R03 R10 deletion and reentry", deletionRecoversCursorThenReentryStartsAtFirst},
        {"L04 filtering clears selection", filteringClearsSelectionWithoutRestoringIt},
        {"Sample reset clears immediate search", sampleResetClearsImmediateSearch},
        {"R09 N08 disabling selected row", disablingASelectedRowClearsIt},
        {"L01 L02 empty single and keyboard endpoints", emptySingleAndKeyboardEndpoints},
        {"R11 filtering uses old order", filteringUsesOldOrderForCursorRecovery},
    };
    return test::run("list_page", cases);
}
