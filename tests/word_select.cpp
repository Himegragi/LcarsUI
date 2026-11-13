#include "core_internal.hpp"
// #83 word and paragraph selection: double click, triple click, drag by unit, Ctrl+arrow and Ctrl+Backspace/Delete,
// through the public text handle, the pointer entry the host uses and the semantic layer. Expected ranges are worked
// out by hand in each case. A fake dictionary breaker with fixed rules stands in for ICU, whose real dictionary is
// only used by the few rule-independent cases run with --icu (ctest word_select_icu, skipped without icu.dll).
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "text_unit_expectations.hpp"
#include "word_break.hpp"
#include <cstring>
#include <iostream>
using namespace lcars;
using test::check;

namespace {
KeyPress press(Key key, bool shift = false, bool control = false) { return {.key = key, .shift = shift, .ctrl = control}; }

// A dictionary stand-in with fixed rules: the script_run words, except that a run of Chinese characters
// is cut into pieces of two. 設定管理示範 -> 設定|管理|示範; the last piece may be one character.
struct PairBreaker final : detail::WordBreaker {
    bool boundaries(std::u16string_view text, std::vector<unsigned>& out) const override {
        std::vector<unsigned> runs;
        if (!detail::script_run_breaker().boundaries(text, runs)) return false;
        auto han = [&](unsigned i) { return text[i] >= 0x4e00 && text[i] <= 0x9fff; };
        for (std::size_t k = 0; k < runs.size(); ++k) {
            out.push_back(runs[k]);
            const unsigned from = runs[k], to = k + 1 < runs.size() ? runs[k + 1] : static_cast<unsigned>(text.size());
            if (from < to && han(from)) for (unsigned at = from + 2; at < to; at += 2) out.push_back(at);
        }
        return true;
    }
};
// A dictionary that is not there: the library could not be loaded.
struct FailingBreaker final : detail::WordBreaker {
    mutable int calls = 0;
    bool boundaries(std::u16string_view, std::vector<unsigned>&) const override { ++calls; return false; }
};

struct Field {
    Ui ui; TextHandle handle;
    explicit Field(std::u16string value, bool multiline = false, WordSegmentation mode = WordSegmentation::dictionary,
                   std::shared_ptr<const detail::WordBreaker> breaker = std::make_shared<PairBreaker>()) {
        ComponentConfig control{.id = "t", .kind = ComponentKind::text, .value = value, .multiline = multiline, .visible_lines = 4};
        auto model = std::make_shared<std::u16string>(std::move(value));
        auto query = ui.add_text(std::move(control));
        check(bool(query), "Create the text field");
        handle = *query;
        check(bool(handle.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
        check(bool(handle.connect_confirmed([](const Change<TextHandle>&) { return Result{}; })), "Connect a write");
        host().set_word_breaker(std::move(breaker));
        check(bool(handle.set_word_segmentation(mode)), "Choose the segmentation");
    }
    detail::UiState& host() { return detail::access(ui); }
    Selection selection() { return host().edit("t")->selection; }
    std::u16string text() { return host().text("t"); }
    // The selected range after a double click on the character starting at `offset`.
    TextSpan double_clicked(unsigned offset) {
        check(bool(handle.double_click(offset)), "Double click");
        auto s = selection();
        return {std::min(s.anchor, s.caret), std::max(s.anchor, s.caret)};
    }
};

void double_click_selects_the_word_and_the_space_after_it() {
    // 設定|管理| |abc|,| |12
    Field f(u"設定管理 abc, 12");
    check(f.double_clicked(1) == TextSpan{0, 2}, "A word followed by another word takes no extra space");
    check(f.double_clicked(2) == TextSpan{2, 5}, "管理 takes the space after it");
    check(f.double_clicked(6) == TextSpan{5, 8}, "abc stops at the comma, which is another word");
    check(f.double_clicked(11) == TextSpan{10, 12}, "The last word has no space to take");
}

void punctuation_and_spaces_stand_alone_and_the_end_selects_the_last_word() {
    Field f(u"abc,  def");  // abc|,|  |def
    check(f.double_clicked(3) == TextSpan{3, 4}, "A punctuation mark is selected alone, without the spaces after it");
    check(f.double_clicked(4) == TextSpan{4, 6} && f.double_clicked(5) == TextSpan{4, 6}, "A run of spaces is selected as one");
    check(f.double_clicked(9) == TextSpan{6, 9}, "Past the end of the text the last word is selected");
    Field trailing(u"abc def  ");
    check(trailing.double_clicked(9) == TextSpan{4, 9}, "Past trailing spaces the last word is selected with them");
    Field closing(u"abc.");
    check(closing.double_clicked(4) == TextSpan{3, 4}, "A last word that is a punctuation mark is selected alone");
    Field blank(u"   ");
    check(blank.double_clicked(1) == TextSpan{0, 3}, "A text of spaces selects the run");
    Field empty(u"");
    check(empty.double_clicked(0) == TextSpan{0, 0}, "An empty field selects nothing");
    check(f.handle.double_click(10).status == Status::invalid_value, "An offset past the end is invalid");
}

void triple_click_selects_everything_in_a_single_line_field_and_a_paragraph_in_a_multi_line_one() {
    Field single(u"設定管理 abc");
    check(bool(single.handle.triple_click(1)) && single.selection().anchor == 0 && single.selection().caret == 8, "Single line: everything");
    check(bool(single.handle.triple_click(8)) && single.selection().anchor == 0 && single.selection().caret == 8, "Single line: past the end too");
    // a0 b1 ' '2 c3 d4 LF5 e6 f7 ' '8 g9 h10 LF11 LF12 i13 j14
    Field multi(u"ab cd\nef gh\n\nij", true);
    auto triple = [&](unsigned at) {
        check(bool(multi.handle.triple_click(at)), "Triple click");
        return TextSpan{multi.selection().anchor, multi.selection().caret};
    };
    check(triple(7) == TextSpan{6, 11}, "A paragraph does not include its line feed");
    check(triple(5) == TextSpan{0, 5}, "A click on the line feed belongs to the paragraph it ends");
    check(triple(11) == TextSpan{6, 11}, "Likewise for the second line feed");
    check(triple(12) == TextSpan{12, 12}, "An empty paragraph selects nothing but takes the caret there");
    check(triple(14) == TextSpan{13, 15} && triple(15) == TextSpan{13, 15}, "The last paragraph, and past the end of it");
    check(*multi.handle.paragraph_at(8) == TextSpan{6, 11}, "paragraph_at reports the same range");
}

void double_click_in_a_multi_line_field_stays_in_its_paragraph() {
    Field f(u"ab cd\nef gh\n\nij", true);
    check(f.double_clicked(5) == TextSpan{3, 5}, "On the line feed the last word of that line is selected");
    check(f.double_clicked(12) == TextSpan{12, 12}, "On an empty line nothing is selected");
    check(f.double_clicked(15) == TextSpan{13, 15}, "Past the end the last word is selected");
    check(f.double_clicked(6) == TextSpan{6, 9}, "ef takes its space but nothing from the next line");
    check(f.double_clicked(9) == TextSpan{9, 11}, "gh is the last word of its line and takes no line feed");
}

// LcarsUI 設定管理示範（第 12 版），OK。 with every index worked out:
// LcarsUI 0..6, space 7, 設8 定9 管10 理11 示12 範13, （14, 第15, space 16, 1 17, 2 18, space 19, 版20, ）21, ，22, O23, K24, 。25
constexpr const char16_t* mixed = u"LcarsUI 設定管理示範（第 12 版），OK。";
void the_three_segmentations_split_mixed_text_differently_and_switch_at_once() {
    Field f(mixed, false, WordSegmentation::per_character);
    check(f.double_clicked(2) == TextSpan{0, 8}, "Latin letters are one word and take the space");
    check(f.double_clicked(8) == TextSpan{8, 9} && f.double_clicked(12) == TextSpan{12, 13}, "Per character: each Chinese character is a word");
    check(f.double_clicked(14) == TextSpan{14, 15} && f.double_clicked(22) == TextSpan{22, 23}, "Each punctuation mark is a word");
    check(f.double_clicked(15) == TextSpan{15, 17} && f.double_clicked(17) == TextSpan{17, 20}, "第 and 12 each take the space after them");
    check(f.double_clicked(23) == TextSpan{23, 25} && f.double_clicked(7) == TextSpan{7, 8}, "OK is one word; a single space stands alone");
    check(bool(f.handle.set_word_segmentation(WordSegmentation::script_run)), "Switch while the field is in use");
    check(f.double_clicked(8) == TextSpan{8, 14} && f.double_clicked(13) == TextSpan{8, 14}, "Script run: the six characters are one word");
    check(f.double_clicked(2) == TextSpan{0, 8}, "Latin and digits behave as before");
    check(bool(f.handle.set_word_segmentation(WordSegmentation::dictionary)), "Switch to the dictionary");
    check(f.double_clicked(8) == TextSpan{8, 10} && f.double_clicked(12) == TextSpan{12, 14}, "Dictionary (fixed pairs): 設定|管理|示範");
    check(*f.handle.word_segmentation() == WordSegmentation::dictionary && *f.handle.word_segmentation_in_use() == WordSegmentation::dictionary,
          "The mode as set and the mode in use agree while the dictionary works");
    check(*f.handle.word_at(9) == TextSpan{8, 10}, "word_at reports what the double click selected");
}

void a_failing_dictionary_falls_back_to_per_character_without_interrupting() {
    auto broken = std::make_shared<FailingBreaker>();
    Field f(mixed, false, WordSegmentation::dictionary, broken);
    check(f.double_clicked(9) == TextSpan{9, 10}, "A failing dictionary segments per character");
    check(*f.handle.word_segmentation() == WordSegmentation::dictionary, "The mode as set stays the dictionary");
    check(*f.handle.word_segmentation_in_use() == WordSegmentation::per_character, "The mode in use reports the fallback");
    check(bool(f.handle.set_word_segmentation(WordSegmentation::script_run)) && *f.handle.word_segmentation_in_use() == WordSegmentation::script_run,
          "Another mode is not affected by the failure");
    f.handle.set_word_segmentation(WordSegmentation::per_character);
    const int calls = broken->calls;
    f.handle.double_click(3);
    check(broken->calls == calls, "The rule modes never ask the dictionary");
}

void read_only_fields_select_but_disabled_ones_do_nothing() {
    Field f(u"ab cd ef");
    check(bool(f.handle.read_only(true)), "Make it read-only");
    check(f.double_clicked(3) == TextSpan{3, 6}, "A read-only field still selects a word");
    check(bool(f.handle.triple_click(1)) && f.selection().caret == 8, "And a paragraph");
    check(bool(f.handle.enable(false)), "Disable it");
    check(f.handle.double_click(3).status == Status::disabled && f.handle.select_word(3).status == Status::disabled, "A disabled field refuses");
    check(f.handle.triple_click(1).status == Status::disabled && f.handle.select_paragraph(1).status == Status::disabled, "Also a triple click");
}

void composing_refuses_word_selection_like_a_single_click_does() {
    Field f(u"abc def");
    auto& host = f.host();
    host.focus("t");
    host.select("t", 3, 3);
    check(bool(host.composition_update(u"你", 1)), "Start composing");
    check(f.handle.double_click(5).status == Status::busy && f.handle.triple_click(5).status == Status::busy && f.handle.select_word(5).status == Status::busy,
          "Word and paragraph selection wait for the input method, as select() does");
    check(host.select("t", 0, 1).status == Status::busy, "The single-click path reports the same status");
    check(host.key_down(press(Key::right, false, true)).status == Status::busy && host.key_down(press(Key::backspace, false, true)).status == Status::busy,
          "Ctrl+arrow and Ctrl+Backspace belong to the input method");
    check(f.text() == u"abc def" && f.selection().caret == 3, "Nothing changed meanwhile");
    check(bool(f.handle.word_at(5)) && *f.handle.word_at(5) == TextSpan{4, 7}, "Queries stay available");
    host.composition_cancel();
    check(bool(f.handle.double_click(5)) && f.selection().anchor == 4 && f.selection().caret == 7, "Afterwards it works again");
}

void keyboard_moves_by_the_same_word_stops() {
    // a0 b1 c2 ' '3 ' '4 d5 e6 f7 ,8 ' '9 設10 定11 管12 理13: stops 0 5 8 10 12 14
    Field f(u"abc  def, 設定管理");
    auto& host = f.host();
    host.focus("t");
    host.select("t", 0, 0);
    std::vector<unsigned> seen;
    for (int i = 0; i < 7; ++i) { host.key_down(press(Key::right, false, true)); seen.push_back(f.selection().caret); }
    check(seen == std::vector<unsigned>{5, 8, 10, 12, 14, 14, 14}, "Ctrl+Right skips spaces, stops at every punctuation mark and at the end");
    seen.clear();
    for (int i = 0; i < 7; ++i) { host.key_down(press(Key::left, false, true)); seen.push_back(f.selection().caret); }
    check(seen == std::vector<unsigned>{12, 10, 8, 5, 0, 0, 0}, "Ctrl+Left goes back to the start of each word");
    host.select("t", 6, 6);
    host.key_down(press(Key::right, false, true));
    check(f.selection().caret == 8, "From the middle of a word Ctrl+Right goes to the next stop");
    host.select("t", 6, 6);
    host.key_down(press(Key::left, false, true));
    check(f.selection().caret == 5, "From the middle of a word Ctrl+Left goes to its start");
    check(*f.handle.next_word_stop(0) == 5 && *f.handle.previous_word_stop(14) == 12 && *f.handle.next_word_stop(14) == 14 && *f.handle.previous_word_stop(0) == 0,
          "The stop queries answer the same");
    host.select("t", 0, 0);
    host.key_down(press(Key::right, true, true));
    check(f.selection().anchor == 0 && f.selection().caret == 5, "Ctrl+Shift+Right extends");
    host.key_down(press(Key::right, true, true));
    check(f.selection().anchor == 0 && f.selection().caret == 8, "and keeps extending");
    host.key_down(press(Key::left, true, true));
    check(f.selection().anchor == 0 && f.selection().caret == 5, "Ctrl+Shift+Left shrinks");
    host.select("t", 3, 6);
    host.key_down(press(Key::right, false, true));
    check(f.selection().anchor == 8 && f.selection().caret == 8, "Without Shift a selection collapses to the stop after its caret");
}

void a_line_feed_is_a_stop_in_a_multi_line_field() {
    Field f(u"ab\ncd ef", true);  // ab|LF|cd| |ef : stops 0 2 3 6 8
    auto& host = f.host();
    host.focus("t");
    host.select("t", 0, 0);
    std::vector<unsigned> seen;
    for (int i = 0; i < 5; ++i) { host.key_down(press(Key::right, false, true)); seen.push_back(f.selection().caret); }
    check(seen == std::vector<unsigned>{2, 3, 6, 8, 8}, "Ctrl+Right stops before and after each line feed");
    seen.clear();
    for (int i = 0; i < 5; ++i) { host.key_down(press(Key::left, false, true)); seen.push_back(f.selection().caret); }
    check(seen == std::vector<unsigned>{6, 3, 2, 0, 0}, "Ctrl+Left stops the same places");
}

void ctrl_backspace_and_delete_remove_exactly_up_to_the_stop() {
    Field f(u"abc  def, 設定管理");
    auto& host = f.host();
    host.focus("t");
    host.select("t", 14, 14);
    check(bool(host.key_down(press(Key::backspace, false, true))) && f.text() == u"abc  def, 設定", "Ctrl+Backspace removes the word before the caret");
    host.select("t", 8, 8);
    check(*f.handle.previous_word_stop(8) == 5, "The stop before 8 is 5");
    host.key_down(press(Key::backspace, false, true));
    check(f.text() == u"abc  , 設定" && f.selection().caret == 5, "It removed exactly up to that stop");
    check(*f.handle.next_word_stop(5) == 7, "The stop after 5 is 7, past the comma and the space");
    host.key_down(press(Key::del, false, true));
    check(f.text() == u"abc  設定" && f.selection().caret == 5, "Ctrl+Delete removed exactly up to that stop");
    check(bool(host.undo("t")) && f.text() == u"abc  , 設定", "One Ctrl+Delete is one undo step");
    check(bool(host.undo("t")) && f.text() == u"abc  def, 設定", "One Ctrl+Backspace is one undo step");
    host.select("t", 2, 7);
    host.key_down(press(Key::backspace, false, true));
    check(f.text() == u"abf, 設定" && f.selection().caret == 2, "With a selection only the selection is deleted");
    host.select("t", 0, 3);
    host.key_down(press(Key::del, false, true));
    check(f.text() == u", 設定" && f.selection().caret == 0, "Ctrl+Delete with a selection deletes just that too");
    Field g(u"abc");
    g.host().focus("t");
    g.host().select("t", 0, 0);
    check(bool(g.host().key_down(press(Key::backspace, false, true))) && g.text() == u"abc" && g.host().undo("t").status == Status::empty_history,
          "Ctrl+Backspace at the start changes nothing and records nothing");
    g.host().select("t", 3, 3);
    check(bool(g.host().key_down(press(Key::del, false, true))) && g.text() == u"abc" && g.host().undo("t").status == Status::empty_history,
          "Ctrl+Delete at the end changes nothing and records nothing");
}

void read_only_and_disabled_fields_keep_their_text_under_ctrl_backspace() {
    Field f(u"abc def");
    auto& host = f.host();
    host.focus("t");
    host.select("t", 7, 7);
    f.handle.read_only(true);
    check(host.key_down(press(Key::backspace, false, true)).status == Status::read_only && f.text() == u"abc def", "Read-only: nothing changes and the status says so");
    check(host.key_down(press(Key::del, false, true)).status == Status::read_only && f.text() == u"abc def", "Also Ctrl+Delete");
    host.key_down(press(Key::left, false, true));
    check(f.selection().caret == 4, "Read-only fields still move by words");
    host.key_down(press(Key::left, true, true));
    check(f.selection().anchor == 4 && f.selection().caret == 0, "and extend by words");
    f.handle.enable(false);
    host.key_down(press(Key::right, false, true));
    check(f.selection().caret == 0 && f.text() == u"abc def", "A disabled field has no focus to move");
}

constexpr int box = 200;
struct Laid {
    test::FakeMeasure measure; ScrollState scroll; Frame frame; Field& field;
    explicit Laid(Field& f, int width = box) : field(f) { relayout(width); }
    void relayout(int width = box) { frame = arrange(layout::control(field.handle), field.ui, web_appearance(), measure, width, 400, 1.f, scroll); }
    const Placement& placed() const { return *frame.find(field.handle); }
    // The pointer position in the middle of the character starting at `offset`.
    int x_of(unsigned offset) const { return (placed().caret_x(offset) + placed().caret_x(offset + 1)) / 2; }
    int y() const { return placed().bounds.y + placed().field.editor.y + 2; }
};

void dragging_after_a_double_click_grows_by_words_and_keeps_the_first_word() {
    Field f(u"aa bb cc dd");  // aa 0-2, space 2, bb 3-5, space 5, cc 6-8, space 8, dd 9-11
    f.host().focus("t");
    Laid laid(f);
    check(bool(f.handle.double_click(4)) && f.selection().anchor == 3 && f.selection().caret == 6, "bb and its space");
    check(laid.frame.select_to(f.ui, f.handle, laid.x_of(9), laid.y()) && f.selection().anchor == 3 && f.selection().caret == 11, "Dragging into dd selects through it");
    check(laid.frame.select_to(f.ui, f.handle, laid.x_of(7), laid.y()) && f.selection().anchor == 3 && f.selection().caret == 9, "Back into cc ends after cc and its space");
    check(laid.frame.select_to(f.ui, f.handle, laid.x_of(4), laid.y()) && f.selection().anchor == 3 && f.selection().caret == 6, "Back inside the first word it stays selected");
    check(laid.frame.select_to(f.ui, f.handle, laid.x_of(0), laid.y()) && f.selection().anchor == 6 && f.selection().caret == 0, "Before it, the selection starts at the new word and the first word stays");
    check(!laid.frame.select_to(f.ui, f.handle, laid.x_of(0), laid.y()), "Dragging to the same place changes nothing");
}

void dragging_after_a_triple_click_grows_by_paragraphs() {
    Field f(u"one two\nthree four\nfive", true);  // paragraphs 0-7, 8-18, 19-23
    f.host().focus("t");
    Laid laid(f);
    check(bool(f.handle.triple_click(10)) && f.selection().anchor == 8 && f.selection().caret == 18, "The middle paragraph");
    const auto& p = laid.placed();
    const int line_h = p.view.line_height, top = p.view.text.y;
    check(laid.frame.select_to(f.ui, f.handle, p.view.text.x + 8, top + 2 * line_h + 2) && f.selection().anchor == 8 && f.selection().caret == 23, "Into the last paragraph");
    check(laid.frame.select_to(f.ui, f.handle, p.view.text.x + 8, top + 2) && f.selection().anchor == 18 && f.selection().caret == 0, "Into the first paragraph");
}

void a_selection_change_ends_the_unit_drag() {
    Field f(u"aa bb cc dd");
    f.host().focus("t");
    Laid laid(f);
    f.handle.double_click(4);
    check(bool(f.handle.select(3, 5)), "Select by program");
    check(laid.frame.select_to(f.ui, f.handle, laid.x_of(9), laid.y()) && f.selection().anchor == 3 && (f.selection().caret == 9 || f.selection().caret == 10),
          "A drag after an unrelated selection moves by character from its anchor");
    f.handle.double_click(4);
    f.host().end_text_drag();
    laid.frame.select_to(f.ui, f.handle, laid.x_of(9), laid.y());
    check(f.selection().anchor == 3 && f.selection().caret != 11, "After the button is released a stray drag is by character");
}

void clicks_count_by_time_distance_and_target() {
    Field f(u"aa bb cc");
    auto second = *f.ui.add_text({.id = "u", .kind = ComponentKind::text, .value = u"aa bb cc"});
    auto model = std::make_shared<std::u16string>(u"aa bb cc");
    second.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    second.connect_confirmed([](const Change<TextHandle>&) { return Result{}; });
    auto& host = f.host();
    std::uint64_t now = 0;
    host.set_click_clock([&] { return now; });
    host.set_click_settings({500, 4, 4});
    auto press_at = [&](std::uint64_t time, int x, int y, std::string_view id = "t") {
        now = time;
        host.text_pointer_down(id, {1, false}, 4, x, y, false);
        auto s = host.edit(id)->selection;
        return TextSpan{std::min(s.anchor, s.caret), std::max(s.anchor, s.caret)};
    };
    check(press_at(0, 10, 10) == TextSpan{1, 1}, "The first press places the caret at the nearest stop");
    check(press_at(100, 10, 10) == TextSpan{3, 6}, "A second press in time and place selects the word under the pointer");
    check(press_at(200, 11, 9) == TextSpan{0, 8}, "A third selects the paragraph");
    check(press_at(300, 10, 10) == TextSpan{1, 1}, "A fourth goes back to a single click");
    check(press_at(350, 10, 10) == TextSpan{3, 6}, "and counting starts over from it");
    check(press_at(1000, 10, 10) == TextSpan{1, 1}, "Too long after the last press is a single click");
    check(press_at(1100, 13, 10) == TextSpan{1, 1}, "Three pixels away leaves a four pixel wide box centered on the first press");
    check(press_at(1200, 14, 11) == TextSpan{3, 6}, "One pixel from the new first press is inside the box");
    check(press_at(1650, 13, 10) == TextSpan{0, 8}, "The third press is judged against the second press's time, not the first's");
    check(press_at(1700, 13, 10) == TextSpan{1, 1}, "Four presses: back to one");
    check(press_at(1750, 13, 13) == TextSpan{1, 1}, "Three pixels away vertically leaves the box too");
    check(press_at(1800, 13, 12) == TextSpan{3, 6}, "One pixel vertically is inside it");
    now = 1850;
    host.text_pointer_down("u", {1, false}, 4, 13, 12, false);
    check(host.edit("u")->selection.anchor == 1 && host.edit("u")->selection.caret == 1, "A press on another field starts a new run");
    // Without a clock there is no multi-click.
    Field plain(u"aa bb cc");
    plain.host().text_pointer_down("t", {1, false}, 4, 10, 10, false);
    plain.host().text_pointer_down("t", {1, false}, 4, 10, 10, false);
    check(plain.selection().anchor == 1 && plain.selection().caret == 1, "A host that supplies no clock never reports a double click");
    // Shift extends from the anchor on a single click.
    now = 5000; host.text_pointer_down("t", {1, false}, 4, 50, 50, false);
    now = 6000; host.text_pointer_down("t", {7, false}, 4, 90, 90, true);
    check(f.selection().anchor == 1 && f.selection().caret == 7, "Shift+click extends the selection");
}

// The UIA word and paragraph units are the double-click words and the triple-click paragraphs: one predicate, shared with
// the UIA seam (tests/uia_demo_client.cpp UA11) through tests/text_unit_expectations.hpp.
bool bounds_start_and_end(const std::vector<unsigned>& bounds, std::size_t size) { return !bounds.empty() && bounds.front() == 0 && bounds.back() == size; }
void uia_units_are_the_double_click_words_and_paragraphs_in_every_mode() {
    for (auto mode : {WordSegmentation::dictionary, WordSegmentation::per_character, WordSegmentation::script_run}) {
        Field f(test::units::text, true, mode);
        const std::u16string text = test::units::text;
        auto pieces = [&](const std::vector<unsigned>& bounds) {
            std::vector<std::u16string> out;
            for (std::size_t i = 0; i + 1 < bounds.size(); ++i) out.push_back(text.substr(bounds[i], bounds[i + 1] - bounds[i]));
            return out;
        };
        check(bounds_start_and_end(f.host().word_unit_boundaries("t"), text.size()) && pieces(f.host().word_unit_boundaries("t")) == test::units::words(),
              "The word units are the shared words");
        check(bounds_start_and_end(f.host().paragraph_boundaries("t"), text.size()) && pieces(f.host().paragraph_boundaries("t")) == test::units::paragraphs(),
              "The paragraph units are the shared paragraphs");
        const auto bounds = f.host().word_unit_boundaries("t");
        for (unsigned at = 0; at < text.size(); ++at) {
            if (text[at] == u'\n') continue;  // a click on a line feed means the last word before it, not the line feed
            const auto unit = static_cast<std::size_t>(std::upper_bound(bounds.begin(), bounds.end(), at) - bounds.begin() - 1);
            TextSpan expected{bounds[unit], bounds[unit + 1]};
            if (text[at] == u' ') {  // a click on spaces selects just the run of spaces, even where a unit lets the previous word keep them
                expected = {at, at + 1};
                while (expected.begin > 0 && text[expected.begin - 1] == u' ') --expected.begin;
                while (expected.end < text.size() && text[expected.end] == u' ') ++expected.end;
            }
            check(*f.handle.word_at(at) == expected, "A double click selects its unit, or the run of spaces when it lands on spaces");
        }
        check(f.host().word_unit_boundaries("missing").empty(), "Nothing for an unknown field");
    }
}

// ---- The semantic layer answers like the typed handle --------------------------------------------------
bool has(const SemanticNode& node, Verb verb) { return std::find(node.verbs.begin(), node.verbs.end(), verb) != node.verbs.end(); }

void the_semantic_layer_reads_selects_and_queries_like_the_typed_handle() {
    Field f(mixed, false, WordSegmentation::dictionary);
    auto& tree = f.ui.semantics();
    auto node = tree.get({"t"});
    check(bool(node) && node->word_segmentation == WordSegmentation::dictionary && node->word_segmentation_in_use == WordSegmentation::dictionary,
          "The node carries the mode as set and the mode in use");
    for (auto verb : {Verb::set_word_segmentation, Verb::select_word, Verb::select_paragraph, Verb::double_click, Verb::triple_click})
        check(has(*node, verb), "A text field offers every word verb");
    check(bool(tree.perform({"t"}, Verb::select_word, 12)) && f.selection().anchor == 12 && f.selection().caret == 14, "select_word selects 示範 (fixed pairs)");
    check(f.handle.select_word(12) && f.selection().anchor == 12 && f.selection().caret == 14, "The typed select_word does the same");
    check(bool(tree.perform({"t"}, Verb::set_word_segmentation, WordSegmentation::script_run)) && *f.handle.word_segmentation() == WordSegmentation::script_run,
          "set_word_segmentation changes the field the handle sees");
    check(tree.get({"t"})->word_segmentation == WordSegmentation::script_run, "and the node");
    check(bool(tree.perform({"t"}, Verb::select_word, 12)) && f.selection().anchor == 8 && f.selection().caret == 14, "Now the whole run");
    check(*tree.text_unit("t", TextUnitKind::word, 12) == *f.handle.word_at(12) && *tree.text_unit("t", TextUnitKind::paragraph, 3) == *f.handle.paragraph_at(3) &&
          *tree.text_unit("t", TextUnitKind::paragraph, 3) == TextSpan{0, 26}, "text_unit equals word_at and paragraph_at");
    check(*tree.word_stop("t", 0, true) == *f.handle.next_word_stop(0) && *tree.word_stop("t", 14, false) == *f.handle.previous_word_stop(14), "word_stop equals the stop queries");
    check(bool(tree.perform({"t"}, Verb::double_click, 17)) && f.selection().anchor == 17 && f.selection().caret == 20, "double_click selects the word (12 and its space)");
    check(bool(tree.perform({"t"}, Verb::triple_click, 17)) && f.selection().anchor == 0 && f.selection().caret == 26, "triple_click selects the paragraph");
    check(tree.perform({"t"}, Verb::select_word, 27).status == Status::invalid_value && f.handle.select_word(27).status == Status::invalid_value, "An offset past the end is invalid in both");
    check(tree.perform({"t"}, Verb::select_word, -1).status == Status::invalid_value && tree.perform({"t"}, Verb::select_word, std::u16string(u"x")).status == Status::invalid_value,
          "A wrong argument is invalid_value");
    check(tree.perform({"t"}, Verb::set_word_segmentation, 3).status == Status::invalid_value, "So is a mode that is not a WordSegmentation");
    check(tree.text_unit("t", TextUnitKind::word, 27).status == Status::invalid_value && tree.word_stop("t", 27, true).status == Status::invalid_value, "Queries agree on a bad offset");
}

void the_semantic_layer_reports_a_dictionary_fallback_and_the_same_admission() {
    Field f(mixed, false, WordSegmentation::dictionary, std::make_shared<FailingBreaker>());
    auto& tree = f.ui.semantics();
    auto node = tree.get({"t"});
    check(node->word_segmentation == WordSegmentation::dictionary && node->word_segmentation_in_use == WordSegmentation::per_character, "The node reports the fallback");
    check(bool(tree.perform({"t"}, Verb::select_word, 9)) && f.selection().anchor == 9 && f.selection().caret == 10, "and operates per character");
    f.handle.read_only(true);
    check(bool(tree.perform({"t"}, Verb::select_word, 9)) && bool(tree.perform({"t"}, Verb::triple_click, 9)), "A read-only field still selects through the layer");
    f.handle.enable(false);
    node = tree.get({"t"});
    check(!has(*node, Verb::select_word) && !has(*node, Verb::double_click), "A disabled field offers no select verbs");
    check(tree.perform({"t"}, Verb::select_word, 9).status == Status::disabled && f.handle.select_word(9).status == Status::disabled, "Both refuse with the same status");
    check(tree.perform({"t"}, Verb::triple_click, 9).status == Status::disabled && f.handle.triple_click(9).status == Status::disabled, "Also for triple click");
    check(bool(tree.text_unit("t", TextUnitKind::word, 9)) && bool(f.handle.word_at(9)), "Queries still work on a disabled field, as for the handle");
    auto button = *f.ui.add_button({.id = "b", .kind = ComponentKind::button, .label = u"OK"});
    (void)button;
    check(!has(*tree.get({"b"}), Verb::select_word) && tree.text_unit("b", TextUnitKind::word, 0).status == Status::not_supported && tree.word_stop("b", 0, true).status == Status::not_supported,
          "Other controls have no word verbs or queries");
}

void the_semantic_layer_announces_a_segmentation_change() {
    Field f(u"aa bb");
    auto& tree = f.ui.semantics();
    std::vector<SemanticEvent> seen;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { seen.push_back(e); });
    f.handle.set_word_segmentation(WordSegmentation::script_run);
    tree.dispatch_events();
    check(std::any_of(seen.begin(), seen.end(), [](const SemanticEvent& e) { return e.kind == SemanticEventKind::property_changed && e.property == "word_segmentation"; }),
          "A property_changed event names word_segmentation");
    seen.clear();
    f.handle.double_click(0);
    tree.dispatch_events();
    check(std::any_of(seen.begin(), seen.end(), [](const SemanticEvent& e) { return e.kind == SemanticEventKind::text_selection_changed; }), "A double click raises the usual selection event");
}

// ---- Real ICU (only with --icu): rules no dictionary update can change ------------------------------
bool icu_present() { std::vector<unsigned> probe; return detail::system_icu_breaker().boundaries(u"a", probe); }

void the_system_dictionary_always_breaks_between_scripts_and_around_punctuation() {
    // 中0 文1 a2 b3 c4 ' '5 1 6 2 7 3 8 ,9 中10
    const std::u16string text = u"中文abc 123,中";
    std::vector<unsigned> cuts;
    check(detail::system_icu_breaker().boundaries(text, cuts), "ICU segments the text");
    auto cut = [&](unsigned at) { return std::find(cuts.begin(), cuts.end(), at) != cuts.end(); };
    check(cut(2) && cut(5) && cut(6) && cut(9) && cut(10), "Chinese/Latin, Latin/space, space/digit, digit/comma and comma/Chinese always break");
    check(!cut(3) && !cut(4) && !cut(7), "Letters and digits of one run never break");
    Field f(text, false, WordSegmentation::dictionary, nullptr);
    check(f.double_clicked(3) == TextSpan{2, 6}, "Through a field: abc and its space");
    check(f.double_clicked(9) == TextSpan{9, 10}, "and the comma alone");
    check(*f.handle.word_segmentation_in_use() == WordSegmentation::dictionary, "The dictionary is what is in use");
    check(f.double_clicked(0) != TextSpan{0, 0} && f.double_clicked(0).end <= 2, "A Chinese word never reaches into the Latin letters");
}
}

// ---- #79 / #80 / #81 interaction: clicks and Ctrl+word keys go through the same line model as the caret.
struct Pt { int x, y; };
struct Pointer {
    std::uint64_t now = 0;
    // The middle of the character starting at `offset`, in frame coordinates.
    static Pt middle(const Placement& p, unsigned offset) {
        const auto rect = p.caret_rect(offset);
        return {(p.caret_x(offset) + p.caret_x(offset + 1)) / 2, rect.y + rect.h / 2};
    }
    // Presses the primary button `count` times in quick succession there, the way the host does.
    void press(Ui& ui, const Frame& frame, std::string_view id, Pt at, int count) {
        auto& host = detail::access(ui);
        host.set_click_clock([this] { return now; });
        for (int i = 0; i < count; ++i) {
            now += 50;
            host.text_pointer_down(id, detail::placement_text_hit(frame, id, at.x, at.y), detail::placement_text_cell(frame, id, at.x, at.y), at.x, at.y, false);
        }
        host.end_text_drag();
        now += 1000;
    }
};
TextSpan selected(Ui& ui, std::string_view id) {
    auto s = detail::access(ui).edit(id)->selection;
    return {std::min(s.anchor, s.caret), std::max(s.anchor, s.caret)};
}

void double_click_on_a_centered_line_selects_the_word_under_the_pointer() {
    // Centered (#80): every line starts at its own x, so a pointer x must be taken back through the line's shift.
    // Lines: "aa bb cc" 0-8 | "xxxxxx yyyy" 9-20 | "z" 21.
    const std::u16string text = u"aa bb cc\nxxxxxx yyyy\nz";
    for (const bool multiline : {true, false}) {
        Ui ui;
        auto model = std::make_shared<std::u16string>(multiline ? text : u"aa bb cc");
        auto handle = *ui.add_text({.id = "c", .kind = ComponentKind::text, .value = *model, .multiline = multiline, .visible_lines = 4, .text_align = TextAlign::center});
        check(bool(handle.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})) && bool(handle.connect_confirmed([](const Change<TextHandle>&) { return Result{}; })), "Connect the field");
        check(bool(handle.set_word_segmentation(WordSegmentation::script_run)), "Words without a dictionary");
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::control(handle), ui, web_appearance(), measure, 200, 400, 1.f, scroll);
        const auto& p = *frame.find(handle);
        if (multiline) check(p.view.lines[0].shift > 0 && p.view.lines[0].shift != p.view.lines[1].shift, "The lines of a centered field start at different x");
        else check(p.shift > 0, "A short centered single line starts right of the left edge");
        Pointer pointer;
        pointer.press(ui, frame, "c", Pointer::middle(p, 4), 2);
        check(selected(ui, "c") == TextSpan{3, 6}, "Double click on the b of bb selects bb and its space");
        pointer.press(ui, frame, "c", Pointer::middle(p, 7), 2);
        check(selected(ui, "c") == TextSpan{6, 8}, "Double click on the last c selects cc, which has no space after it");
        if (multiline) {
            pointer.press(ui, frame, "c", Pointer::middle(p, 17), 2);
            check(selected(ui, "c") == TextSpan{16, 20}, "Double click on a y in the centered second line selects yyyy");
            pointer.press(ui, frame, "c", Pointer::middle(p, 10), 2);
            check(selected(ui, "c") == TextSpan{9, 16}, "Double click in xxxxxx selects it with the space after it");
            pointer.press(ui, frame, "c", Pointer::middle(p, 10), 3);
            check(selected(ui, "c") == TextSpan{9, 20}, "Triple click selects the centered paragraph without its line feed");
        }
    }
}

TextHandle rich_field(Ui& ui, std::shared_ptr<RichText> model, TextAlign align = TextAlign::left) {
    auto handle = *ui.add_text({.id = "r", .kind = ComponentKind::text, .value = model->text, .multiline = true, .visible_lines = 4, .text_align = align, .rich = true});
    check(bool(handle.connect_rich_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a rich source");
    check(bool(handle.connect_rich_immediate([model](const Change<TextHandle, RichText>& change) { *model = change.value; return Result{}; })), "Connect a rich write");
    check(bool(handle.set_word_segmentation(WordSegmentation::script_run)), "Words without a dictionary");
    return handle;
}
StyleSpan bold_span(unsigned start, unsigned length) { return {.start = start, .length = length, .bold = true}; }
StyleSpan italic_span(unsigned start, unsigned length) { return {.start = start, .length = length, .italic = true}; }

void rich_text_words_select_through_the_styled_runs_and_keep_the_styles() {
    // "alpha bravo charlie": the bold run makes bravo wider (FakeMeasure bold = 5/4), so the pointer x only
    // finds the right character when it follows the pieces. Centered too, to add the alignment shift.
    for (const auto align : {TextAlign::left, TextAlign::center}) {
        Ui ui;
        auto model = std::make_shared<RichText>(RichText{u"alpha bravo charlie", {bold_span(6, 5)}});
        auto handle = rich_field(ui, model, align);
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::control(handle), ui, web_appearance(), measure, 200, 400, 1.f, scroll);
        const auto& p = *frame.find(handle);
        check(p.view.lines[0].pieces.size() == 3, "The line has a plain, a bold and a plain piece");
        Pointer pointer;
        pointer.press(ui, frame, "r", Pointer::middle(p, 9), 2);
        check(selected(ui, "r") == TextSpan{6, 12}, "Double click on the o of bravo selects bravo and its space");
        pointer.press(ui, frame, "r", Pointer::middle(p, 14), 2);
        check(selected(ui, "r") == TextSpan{12, 19}, "Double click right of the bold run selects charlie");
        pointer.press(ui, frame, "r", Pointer::middle(p, 1), 3);
        check(selected(ui, "r") == TextSpan{0, 19}, "Triple click selects the whole paragraph");
        check(handle.rich_value()->spans == std::vector<StyleSpan>{bold_span(6, 5)} && handle.rich_value()->text == u"alpha bravo charlie", "Selecting by word changes neither text nor styles");
    }
    Ui ui;
    auto model = std::make_shared<RichText>(RichText{u"alpha bravo charlie", {bold_span(6, 5)}});
    auto handle = rich_field(ui, model);
    check(*handle.word_at(8) == TextSpan{6, 12} && *handle.paragraph_at(8) == TextSpan{0, 19}, "word_at and paragraph_at work on a rich field");
    check(bool(handle.select_word(8)) && selected(ui, "r") == TextSpan{6, 12}, "select_word selects the bold word");
}

void ctrl_backspace_over_a_styled_word_keeps_the_other_styles_and_undo_restores_them() {
    auto model = std::make_shared<RichText>(RichText{u"xx abcdef yy", {bold_span(0, 6), italic_span(10, 2)}});  // bold "xx abc", italic "yy"
    Ui ui;
    auto handle = rich_field(ui, model);
    auto& host = detail::access(ui);
    host.focus("r");
    host.select("r", 9, 9);
    const std::vector<StyleSpan> before{bold_span(0, 6), italic_span(10, 2)};
    check(bool(host.key_down(press(Key::backspace, false, true))) && handle.rich_value()->text == u"xx  yy", "Ctrl+Backspace removes the word abcdef, half of it bold");
    check(handle.rich_value()->spans == std::vector<StyleSpan>{bold_span(0, 3), italic_span(4, 2)}, "The bold run is cut at the deletion and the italic run moves with its text");
    check(selected(ui, "r") == TextSpan{3, 3}, "The caret sits where the word began");
    check(bool(host.undo("r")) && handle.rich_value()->text == u"xx abcdef yy" && handle.rich_value()->spans == before, "One undo restores the text and every style");
    host.select("r", 3, 3);
    check(bool(host.key_down(press(Key::del, false, true))) && handle.rich_value()->text == u"xx yy" && handle.rich_value()->spans == std::vector<StyleSpan>{bold_span(0, 3), italic_span(3, 2)},
          "Ctrl+Delete over the same word keeps the styles around it");
    check(bool(host.undo("r")) && handle.rich_value()->spans == before, "and undo restores them");
    host.select("r", 12, 12);
    host.key_down(press(Key::left, true, true));
    check(selected(ui, "r") == TextSpan{10, 12} && handle.rich_value()->spans == before, "Ctrl+Shift+Left selects the word before the caret without touching styles");
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--icu") == 0) {
        if (!icu_present()) {
            std::cout << "SKIPPED: icu.dll is not available on this machine; the real ICU cases did not run\n";
            return 77;
        }
        static const test::Case icu_cases[] = {
            {"the_system_dictionary_always_breaks_between_scripts_and_around_punctuation", the_system_dictionary_always_breaks_between_scripts_and_around_punctuation},
        };
        return test::run("word_select_icu", icu_cases);
    }
    static const test::Case cases[] = {
        {"double_click_selects_the_word_and_the_space_after_it", double_click_selects_the_word_and_the_space_after_it},
        {"punctuation_and_spaces_stand_alone_and_the_end_selects_the_last_word", punctuation_and_spaces_stand_alone_and_the_end_selects_the_last_word},
        {"triple_click_selects_everything_in_a_single_line_field_and_a_paragraph_in_a_multi_line_one", triple_click_selects_everything_in_a_single_line_field_and_a_paragraph_in_a_multi_line_one},
        {"double_click_in_a_multi_line_field_stays_in_its_paragraph", double_click_in_a_multi_line_field_stays_in_its_paragraph},
        {"the_three_segmentations_split_mixed_text_differently_and_switch_at_once", the_three_segmentations_split_mixed_text_differently_and_switch_at_once},
        {"a_failing_dictionary_falls_back_to_per_character_without_interrupting", a_failing_dictionary_falls_back_to_per_character_without_interrupting},
        {"read_only_fields_select_but_disabled_ones_do_nothing", read_only_fields_select_but_disabled_ones_do_nothing},
        {"composing_refuses_word_selection_like_a_single_click_does", composing_refuses_word_selection_like_a_single_click_does},
        {"keyboard_moves_by_the_same_word_stops", keyboard_moves_by_the_same_word_stops},
        {"a_line_feed_is_a_stop_in_a_multi_line_field", a_line_feed_is_a_stop_in_a_multi_line_field},
        {"ctrl_backspace_and_delete_remove_exactly_up_to_the_stop", ctrl_backspace_and_delete_remove_exactly_up_to_the_stop},
        {"read_only_and_disabled_fields_keep_their_text_under_ctrl_backspace", read_only_and_disabled_fields_keep_their_text_under_ctrl_backspace},
        {"dragging_after_a_double_click_grows_by_words_and_keeps_the_first_word", dragging_after_a_double_click_grows_by_words_and_keeps_the_first_word},
        {"dragging_after_a_triple_click_grows_by_paragraphs", dragging_after_a_triple_click_grows_by_paragraphs},
        {"a_selection_change_ends_the_unit_drag", a_selection_change_ends_the_unit_drag},
        {"clicks_count_by_time_distance_and_target", clicks_count_by_time_distance_and_target},
        {"uia_units_are_the_double_click_words_and_paragraphs_in_every_mode", uia_units_are_the_double_click_words_and_paragraphs_in_every_mode},
        {"the_semantic_layer_reads_selects_and_queries_like_the_typed_handle", the_semantic_layer_reads_selects_and_queries_like_the_typed_handle},
        {"the_semantic_layer_reports_a_dictionary_fallback_and_the_same_admission", the_semantic_layer_reports_a_dictionary_fallback_and_the_same_admission},
        {"the_semantic_layer_announces_a_segmentation_change", the_semantic_layer_announces_a_segmentation_change},
        {"double_click_on_a_centered_line_selects_the_word_under_the_pointer", double_click_on_a_centered_line_selects_the_word_under_the_pointer},
        {"rich_text_words_select_through_the_styled_runs_and_keep_the_styles", rich_text_words_select_through_the_styled_runs_and_keep_the_styles},
        {"ctrl_backspace_over_a_styled_word_keeps_the_other_styles_and_undo_restores_them", ctrl_backspace_over_a_styled_word_keeps_the_other_styles_and_undo_restores_them},
    };
    return test::run("word_select", cases);
}
