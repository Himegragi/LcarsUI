#include "core_internal.hpp"
// #79 multi-line text fields through the public core and layout seams, with a deterministic
// fake text backend. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
using namespace lcars;
using test::check;
using test::FakeMeasure;

namespace {
struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override {
        if (!content) return {Status::not_supported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, content};
    }
};
KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
KeyPress press(Key key, bool shift = false, bool control = false) { return {.key = key, .shift = shift, .ctrl = control}; }

// Every field gets an explicit write timing, as an application would.
TextHandle connect(Ui& ui, ComponentConfig control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto query = ui.add_text(std::move(control));
    check(bool(query), "Create the text field");
    auto field = *query;
    check(bool(field.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connect_confirmed([](const Change<TextHandle>&) { return Result{}; })), "Connect a write");
    return field;
}
ComponentConfig multi(std::string id, std::u16string value = {}, unsigned lines = 3) {
    return {.id = std::move(id), .value = std::move(value), .multiline = true, .visible_lines = lines};
}
std::u16string value_of(Ui& ui, std::string_view id) { return detail::access(ui).text(id); }

void multiline_values_allow_line_feed_and_reject_carriage_return() {
    Ui ui;
    check(bool(ui.add_text(multi("ok", u"a\nb"))), "A multi-line field may start with a line feed");
    check(ui.add_text(multi("cr", u"a\rb")).status == Status::invalid_value, "A carriage return is an invalid initial value");
    check(ui.add_text(multi("tab", u"a\tb")).status == Status::invalid_value, "Other control characters stay invalid");
    check(ui.add_text({.id = "single", .value = u"a\nb"}).status == Status::invalid_value,
          "A single-line field still rejects a line feed");
    check(ui.add_text(multi("zero", u"", 0)).status == Status::invalid_value, "A multi-line field needs at least one visible line");
    check(valid_multi_line(u"a\nb") && !valid_multi_line(u"a\r\nb") && !valid_multi_line(u"a\tb") && !valid_multi_line(u"ab", 1),
          "valid_multi_line allows only LF, within the length");
    check(!valid_single_line(u"a\nb"), "valid_single_line is unchanged");
}

void semantic_and_typed_set_value_share_the_line_feed_rule() {
    Ui ui;
    auto field = connect(ui, multi("note"));
    check(bool(field.set_value(u"one\ntwo")) && value_of(ui, "note") == u"one\ntwo", "Typed set_value accepts a line feed");
    check(field.set_value(u"x\ry").status == Status::invalid_value && value_of(ui, "note") == u"one\ntwo",
          "Typed set_value rejects CR and leaves the field unchanged");
    auto& tree = ui.semantics();
    check(bool(tree.perform({"note"}, Verb::set_value, std::u16string(u"semantic\nvalue"))) && field.draft().value.value() == u"semantic\nvalue",
          "Semantic set_value accepts a line feed");
    check(tree.perform({"note"}, Verb::set_value, std::u16string(u"bad\r")).status == Status::invalid_value && value_of(ui, "note") == u"semantic\nvalue",
          "Semantic set_value rejects CR with the same status");
    check(tree.get({"note"})->value == u"semantic\nvalue", "The semantic value includes the line feed");
}

void paste_normalizes_every_line_break_to_one_line_feed() {
    Ui ui;
    auto field = connect(ui, multi("note"));
    MemoryClipboard clipboard;
    auto& host = detail::access(ui);
    host.set_clipboard(&clipboard); host.focus("note");
    clipboard.content = u"a\r\nb\rc\nd";
    host.key_down(ctrl(u'V'));
    check(value_of(ui, "note") == u"a\nb\nc\nd", "CRLF, lone CR and lone LF each paste as one LF");
    host.key_down(ctrl(u'Z'));
    check(value_of(ui, "note").empty(), "One paste is one undo entry");
    host.key_down(ctrl(u'Y'));
    check(value_of(ui, "note") == u"a\nb\nc\nd", "Redo restores the paste");
    check(paste_as_multi_line(u"x\r\n\r\ny") == u"x\n\ny", "paste_as_multi_line counts CRLF once");
    check(paste_as_single_line(u"x\r\ny") == u"x y", "paste_as_single_line is unchanged");
    host.select("note", 0, 3);
    host.key_down(ctrl(u'C'));
    check(clipboard.content == u"a\r\nb", "Copy writes line breaks as CRLF");
    check(value_of(ui, "note") == u"a\nb\nc\nd", "Copy does not change the value");
    host.key_down(ctrl(u'X'));
    check(clipboard.content == u"a\r\nb" && value_of(ui, "note") == u"\nc\nd", "Cut also writes CRLF");
}

void single_line_paste_still_turns_breaks_into_spaces() {
    Ui ui;
    auto field = connect(ui, {.id = "name", .value = u"x"});
    MemoryClipboard clipboard;
    auto& host = detail::access(ui);
    host.set_clipboard(&clipboard); host.focus("name");
    clipboard.content = u"a\r\nb\rc\nd";
    host.key_down(ctrl(u'A'));
    host.key_down(ctrl(u'V'));
    check(value_of(ui, "name") == u"a b c d", "A single-line paste converts breaks to spaces");
    host.select("name", 0, 7);
    host.key_down(ctrl(u'C'));
    check(clipboard.content == u"a b c d", "A single-line copy is not converted");
}

void a_line_feed_counts_as_one_toward_max_length() {
    Ui ui;
    auto field = connect(ui, {.id = "note", .max_length = 5, .multiline = true, .visible_lines = 2});
    check(bool(field.set_value(u"ab\ncd")), "Five characters including a line feed fit a limit of five");
    check(field.set_value(u"ab\ncde").status == Status::invalid_value && value_of(ui, "note") == u"ab\ncd", "Six characters are rejected whole");
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 5, 5);
    check(host.paste("note", u"\r\n").status == Status::invalid_value && value_of(ui, "note") == u"ab\ncd",
          "A pasted CRLF counts once and is rejected at the limit");
    host.select("note", 2, 3);
    check(bool(host.paste("note", u"\r\n")) && value_of(ui, "note") == u"ab\ncd", "Replacing the line feed with CRLF stays within the limit");
}

void a_source_value_with_carriage_return_is_ignored() {
    Ui ui;
    auto model = std::make_shared<std::u16string>(u"a\nb");
    std::function<void()> notify;
    auto field = *ui.add_text(multi("note", u"a\nb"));
    check(bool(field.connect_source({[model] { return *model; }, [&](std::function<void()> cb) { notify = cb; return [] {}; }})), "Connect a source");
    *model = u"c\rd"; notify();
    check(value_of(ui, "note") == u"a\nb", "An invalid source value is ignored like any invalid value");
    *model = u"c\nd"; notify();
    check(value_of(ui, "note") == u"c\nd", "A valid multi-line source value is adopted");
}

// A form with a multi-line field, a single-line field and three shortcut targets.
struct Form {
    Ui ui;
    TextHandle note, name;
    ButtonHandle plain_enter, ctrl_enter, scoped_enter;
    int plain = 0, with_ctrl = 0, scoped = 0;
    Form() {
        note = connect(ui, multi("note"));
        name = connect(ui, {.id = "name", .value = u"x"});
        plain_enter = *ui.add_button({.id = "plain", .label = u"OK"});
        ctrl_enter = *ui.add_button({.id = "ctrl", .label = u"Apply"});
        scoped_enter = *ui.add_button({.id = "scoped", .label = u"Edit"});
        check(bool(plain_enter.bind([this] { ++plain; return Result{}; })), "Bind the global Enter action");
        check(bool(ctrl_enter.bind([this] { ++with_ctrl; return Result{}; })), "Bind the Ctrl+Enter action");
        check(bool(scoped_enter.bind([this] { ++scoped; return Result{}; })), "Bind the scoped Enter action");
        check(bool(ui.add_shortcut({.key = Key::enter}, plain_enter)), "Global Enter shortcut");
        check(bool(ui.add_shortcut({.key = Key::enter, .ctrl = true}, ctrl_enter)), "Ctrl+Enter shortcut");
        check(bool(ui.add_shortcut({.key = Key::enter}, scoped_enter, note)), "Enter shortcut scoped to the multi-line field");
        check(bool(ui.add_shortcut({.key = Key::enter}, scoped_enter, name)), "Enter shortcut scoped to the single-line field");
    }
    detail::UiState& host() { return detail::access(ui); }
};

void enter_inserts_a_line_feed_and_no_enter_shortcut_fires() {
    Form form; auto& host = form.host();
    host.focus("note");
    check(bool(host.key_down(press(Key::enter))) && value_of(form.ui, "note") == u"\n", "Enter inserts a line feed");
    host.text_input(u"a");
    check(bool(host.key_down(press(Key::enter, true))) && value_of(form.ui, "note") == u"\na\n", "Shift+Enter inserts a line feed too");
    check(form.plain == 0 && form.scoped == 0 && form.with_ctrl == 0, "Neither the global nor the scoped Enter shortcut fired");
    host.select("note", 1, 1);
    host.key_down(press(Key::enter));
    check(value_of(form.ui, "note") == u"\n\na\n", "Enter inserts at the caret");
    check(host.edit("note")->selection.caret == 2 && host.edit("note")->selection.anchor == 2, "The caret follows the inserted break");
    host.select("note", 0, 3);
    host.key_down(press(Key::enter));
    check(value_of(form.ui, "note") == u"\n\n", "Enter replaces the selection (three of four characters) with one break");
}

void ctrl_enter_reaches_the_shortcut_and_tab_moves_focus() {
    Form form; auto& host = form.host();
    host.focus("note");
    host.key_down(press(Key::enter, false, true));
    check(form.with_ctrl == 1 && form.plain == 0 && value_of(form.ui, "note").empty(), "Ctrl+Enter triggers its shortcut and inserts nothing");
    host.key_down(press(Key::tab));
    check(host.focused() == "name" && value_of(form.ui, "note").empty(), "Tab moves the focus and inserts no tab character");
    host.key_down(press(Key::tab, true));
    check(host.focused() == "note", "Shift+Tab moves the focus back");
}

void each_enter_is_one_undo_entry() {
    Form form; auto& host = form.host();
    host.focus("note");
    host.text_input(u"a"); host.key_down(press(Key::enter)); host.text_input(u"b");
    check(value_of(form.ui, "note") == u"a\nb", "Typed text with a break");
    host.key_down(ctrl(u'Z'));
    check(value_of(form.ui, "note") == u"a\n", "Undo removes the last typed character");
    host.key_down(ctrl(u'Z'));
    check(value_of(form.ui, "note") == u"a", "Undo removes the break by itself");
    host.key_down(ctrl(u'Z'));
    check(value_of(form.ui, "note").empty(), "Undo removes the first character");
    host.key_down(ctrl(u'Y'));
    host.key_down(ctrl(u'Y'));
    check(value_of(form.ui, "note") == u"a\n", "Redo restores the break as one entry");
}

void enter_during_composition_does_not_break_the_line() {
    Form form; auto& host = form.host();
    host.focus("note");
    host.text_input(u"a");
    check(bool(host.composition_update(u"ㄅㄉ", 2)), "Start composing");
    auto result = host.key_down(press(Key::enter));
    check(result.status == Status::busy && host.edit("note")->composing, "Enter during composition is left to the input method");
    check(value_of(form.ui, "note") == u"a" && form.plain == 0 && form.scoped == 0, "No break and no shortcut");
    check(bool(host.composition_commit(u"你")) && value_of(form.ui, "note") == u"a你", "The input method commits the chosen text");
    check(bool(host.key_down(press(Key::enter))) && value_of(form.ui, "note") == u"a你\n", "Enter breaks the line again after the composition");
}

void single_line_enter_still_reaches_its_shortcut() {
    Form form; auto& host = form.host();
    host.focus("name");
    host.key_down(press(Key::enter));
    check(form.scoped == 1 && form.plain == 0 && value_of(form.ui, "name") == u"x", "A single-line field leaves Enter to its shortcut");
}

void read_only_multiline_field_refuses_enter() {
    Form form; auto& host = form.host();
    host.focus("note");
    form.note.read_only(true);
    check(host.key_down(press(Key::enter)).status == Status::read_only && value_of(form.ui, "note").empty() && form.scoped == 0,
          "A read-only multi-line field neither edits nor lets Enter through");
}

// ---- Layout -------------------------------------------------------------------------------
// FakeMeasure at 16 px text: an ASCII character is 8 px wide and a line is 24 px tall. A 200 px
// field has a 176 px editor, a 14 px scrollbar reserve and a 2 px caret slack, so a line holds
// (200 - 24 - 14 - 2) / 8 = 20 characters.
constexpr int field_width = 200;
struct Laid {
    FakeMeasure measure; ScrollState scroll; Frame frame;
    const Placement* field = nullptr;
    Laid(Ui& ui, const TextHandle& handle, int width = field_width) {
        frame = arrange(layout::control(handle), ui, web_appearance(), measure, width, 400, 1.f, scroll);
        field = frame.find(handle);
        check(field != nullptr && field->multiline, "The field is placed as a multi-line field");
    }
};
std::vector<std::pair<unsigned, unsigned>> spans(const Placement& p) {
    std::vector<std::pair<unsigned, unsigned>> out;
    for (const auto& line : p.view.lines) out.push_back({line.begin, line.end});
    return out;
}
using Spans = std::vector<std::pair<unsigned, unsigned>>;

void a_long_line_wraps_at_the_edge_and_the_height_stays_fixed() {
    Ui ui;
    auto field = connect(ui, multi("note", std::u16string(45, u'a')));
    Laid laid(ui, field);
    check(spans(*laid.field) == Spans{{0, 20}, {20, 40}, {40, 45}}, "45 characters wrap into 20 + 20 + 5");
    check(laid.field->view.lines[0].soft && laid.field->view.lines[1].soft && !laid.field->view.lines[2].soft, "Only wrapped pieces continue into the next line");
    check(laid.field->field.editor.h == 3 * 24 && laid.field->view.visible == 3 && laid.field->view.line_height == 24, "The editor is exactly three lines tall");
    Ui other;
    auto longer = connect(other, multi("note", std::u16string(400, u'a')));
    Laid tall(other, longer);
    check(tall.field->bounds.h == laid.field->bounds.h, "More content never lengthens the field");
    check(tall.field->view.lines.size() == 20 && tall.field->view.first == 0, "A long text keeps every line and shows the first ones");
}

void hard_breaks_make_lines_and_wrapping_prefers_spaces() {
    Ui ui;
    auto field = connect(ui, multi("note", u"a\n\nb\n"));
    Laid laid(ui, field);
    check(spans(*laid.field) == Spans{{0, 1}, {2, 2}, {3, 4}, {5, 5}}, "A, an empty line, B and a trailing empty line");
    check(!laid.field->view.lines[0].soft && !laid.field->view.lines[3].soft, "Hard lines are not soft");
    Ui words;
    auto sentence = connect(words, multi("note", u"hello world again and more"));
    Laid wrapped(words, sentence);
    check(spans(*wrapped.field) == Spans{{0, 18}, {18, 26}}, "Latin text breaks after the last space that fits (\"hello world again \"), like labels");
}

void the_scrollbar_width_is_always_reserved() {
    Ui three;
    auto first = connect(three, multi("note", std::u16string(20, u'x') + u"\nb\nc"));
    Laid before(three, first);
    Ui four;
    auto second = connect(four, multi("note", std::u16string(20, u'x') + u"\nb\nc\nd"));
    Laid after(four, second);
    check(spans(*before.field) == Spans{{0, 20}, {21, 22}, {23, 24}}, "Three lines fit; the first line holds 20 characters exactly");
    check(before.field->view.lines.size() == 3 && after.field->view.lines.size() == 4, "The fourth line appears");
    for (std::size_t i = 0; i < 3; ++i)
        check(before.field->view.lines[i].begin == after.field->view.lines[i].begin && before.field->view.lines[i].end == after.field->view.lines[i].end,
              "Adding a line does not re-wrap the earlier lines");
    check(before.field->view.thumb.h == 0 && after.field->view.thumb.h > 0, "The thumb exists only when the content overflows");
    check(before.field->view.track.w == after.field->view.track.w && before.field->view.track.w == 10 &&
          before.field->view.text.w == after.field->view.text.w && before.field->view.text.w == 176 - 14,
          "The scrollbar track is 10 px and the text width does not change");
}

void pointer_positions_map_to_line_and_offset() {
    Ui ui;
    auto field = connect(ui, multi("note", u"abc\ndef\nghi"));
    Laid laid(ui, field);
    const auto& view = laid.field->view;
    auto at = [&](int column, int row) { return laid.frame.text_offset(field, view.text.x + 8 * column + 1, view.text.y + 24 * row + 3); };
    check(at(0, 0) == 0 && at(2, 0) == 2 && at(3, 0) == 3, "First line: columns map to offsets");
    check(at(2, 1) == 6 && at(1, 2) == 9, "Second and third lines add their start offsets");
    check(at(9, 0) == 3 && at(9, 1) == 7, "Past the end of a line is that line's end, before its break");
    check(laid.frame.text_offset(field, view.text.x, view.text.y + 24 * 5) == 8, "Below the last line is that line at the same column");
    check(laid.frame.text_offset(field, view.text.x, view.text.y - 40) == 0, "Above the first line is the start");
    check(laid.field->caret_x(5) == view.text.x + 8 && laid.field->caret_y(5) == view.text.y + 24, "Caret position of offset 5: column 1, second line");
}

void a_caret_on_a_wrap_boundary_belongs_to_the_next_line_unless_upstream() {
    Ui ui;
    auto field = connect(ui, multi("note", std::u16string(30, u'a')));
    Laid laid(ui, field);
    const auto& view = laid.field->view;
    check(view.line_of(20) == 1 && laid.field->caret_y(20) == view.text.y + 24 && laid.field->caret_x(20) == view.text.x,
          "A caret at the wrap point is drawn at the start of the next line");
    check(view.line_of(19) == 0 && view.line_of(0) == 0 && view.line_of(30) == 1, "Other offsets follow their lines");
    auto upstream = view;
    upstream.upstream = true;
    check(upstream.line_of(20) == 0 && upstream.line_of(21) == 1, "Upstream puts the wrap-point caret on the line above");
}

// ---- Cursor movement ----------------------------------------------------------------------
// The host arranges after every input; so does this fixture, which is what keeps core's copy of the lines fresh.
struct Editor {
    Ui ui; TextHandle field; FakeMeasure measure; ScrollState scroll; Frame frame;
    explicit Editor(std::u16string value, unsigned lines = 3, int width = field_width) : width_(width) {
        field = connect(ui, multi("note", std::move(value), lines));
        host().focus("note");
        layout();
    }
    detail::UiState& host() { return detail::access(ui); }
    void layout() { frame = arrange(layout::control(field), ui, web_appearance(), measure, width_, 400, 1.f, scroll); }
    const Placement& placed() const { return *frame.find(field); }
    void key(Key k, bool shift = false, bool control = false) { host().key_down(press(k, shift, control)); layout(); }
    void put(unsigned anchor, unsigned caret) { check(bool(host().select("note", anchor, caret)), "Select"); layout(); }
    unsigned caret() { return host().edit("note")->selection.caret; }
    unsigned anchor() { return host().edit("note")->selection.anchor; }
    unsigned first() const { return placed().view.first; }
    int width_;
};

void vertical_keys_move_by_display_line_and_keep_the_column() {
    Editor e(u"abcdef\nxy\nabcdef");
    e.put(4, 4);
    e.key(Key::down);
    check(e.caret() == 9 && e.anchor() == 9, "Down into a shorter line lands at its end");
    e.key(Key::down);
    check(e.caret() == 14, "Down again returns to the remembered column");
    e.key(Key::up);
    check(e.caret() == 9, "Up goes back to the short line");
    e.key(Key::up);
    check(e.caret() == 4, "Up restores the original column");
    e.key(Key::up);
    check(e.caret() == 4, "Up on the first line does not move");
    e.put(16, 16);
    e.key(Key::down);
    check(e.caret() == 16, "Down on the last line does not move");
}

void vertical_keys_follow_wrapped_lines() {
    Editor e(std::u16string(30, u'a'));
    e.put(5, 5);
    e.key(Key::down);
    check(e.caret() == 25, "Down moves into the wrapped piece, same column");
    e.key(Key::up);
    check(e.caret() == 5, "Up moves back");
    e.key(Key::home);
    check(e.caret() == 0, "Home on the first piece");
    e.put(25, 25);
    e.key(Key::home);
    check(e.caret() == 20, "Home on a wrapped piece goes to the piece's start, not the paragraph's");
    e.put(3, 3);
    e.key(Key::end);
    check(e.caret() == 20 && e.host().edit("note")->caret_upstream, "End on a wrapped piece stops at its end, upstream");
    check(e.placed().caret_y(e.caret()) == e.placed().view.text.y, "The upstream caret is drawn on the first line");
    e.key(Key::right);
    check(e.caret() == 21 && !e.host().edit("note")->caret_upstream && e.placed().caret_y(21) == e.placed().view.text.y + 24, "Right leaves the wrap point");
    e.put(25, 25);
    e.key(Key::end);
    check(e.caret() == 30 && !e.host().edit("note")->caret_upstream, "End on the last piece is the end of the text");
}

void home_and_end_use_the_current_line_and_ctrl_uses_the_whole_text() {
    Editor e(u"ab\ncd\nef");
    e.put(4, 4);
    e.key(Key::end);
    check(e.caret() == 5, "End: end of the line, before its break");
    e.key(Key::home);
    check(e.caret() == 3, "Home: start of the line");
    e.key(Key::end, false, true);
    check(e.caret() == 8, "Ctrl+End: end of the text");
    e.key(Key::home, false, true);
    check(e.caret() == 0, "Ctrl+Home: start of the text");
    e.put(4, 4);
    e.key(Key::end, true);
    check(e.anchor() == 4 && e.caret() == 5, "Shift+End extends to the line end");
    e.key(Key::home, true, true);
    check(e.anchor() == 4 && e.caret() == 0, "Shift+Ctrl+Home extends to the start");
    e.key(Key::end, true, true);
    check(e.anchor() == 4 && e.caret() == 8, "Shift+Ctrl+End extends to the end");
}

void shift_with_vertical_keys_extends_the_selection() {
    Editor e(u"ab\ncd\nef");
    e.put(1, 1);
    e.key(Key::down, true);
    check(e.anchor() == 1 && e.caret() == 4, "Shift+Down extends one line");
    e.key(Key::down, true);
    check(e.anchor() == 1 && e.caret() == 7, "Shift+Down extends again");
    e.key(Key::up, true);
    check(e.anchor() == 1 && e.caret() == 4, "Shift+Up shrinks it");
    e.key(Key::down);
    check(e.anchor() == 7 && e.caret() == 7, "Down without Shift collapses to the new caret");
}

void page_keys_move_by_the_visible_line_count() {
    Editor e(u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9", 3);
    check(e.placed().view.lines.size() == 10 && e.first() == 0, "Ten lines, three visible");
    e.put(0, 0);
    e.key(Key::page_down);
    check(e.caret() == 6 && e.first() == 3, "PageDown moves three lines and scrolls three lines");
    e.key(Key::page_down);
    check(e.caret() == 12 && e.first() == 6, "PageDown again");
    e.key(Key::page_down);
    check(e.caret() == 18 && e.first() == 7, "PageDown at the end stops on the last line with the last page showing");
    e.key(Key::page_up);
    check(e.caret() == 12 && e.first() == 4, "PageUp moves three lines back, the view follows");
    e.key(Key::page_up, true);
    check(e.anchor() == 12 && e.caret() == 6, "Shift+PageUp extends the selection");
}

// ---- Scrolling ----------------------------------------------------------------------------
std::u16string numbered_lines(int count) {
    std::u16string text;
    for (int i = 0; i < count; ++i) { if (i) text += u'\n'; text += std::u16string(1, static_cast<char16_t>(u'0' + i % 10)); }
    return text;
}
unsigned scroll_line_of(const TextHandle& field) { return field.scroll_line().value.value(); }

void typing_to_line_twenty_scrolls_the_field_to_the_caret() {
    Editor e(u"x", 3);
    e.key(Key::end, false, true);
    for (int i = 0; i < 19; ++i) {
        e.key(Key::enter);
        e.host().text_input(u"y"); e.layout();
    }
    check(e.placed().view.lines.size() == 20 && e.caret() == 39, "Twenty lines, the caret on the last");
    check(e.first() == 17, "The field scrolled so the caret's line is the last visible one");
    check(e.placed().view.thumb.h > 0 && e.placed().view.thumb.y + e.placed().view.thumb.h == e.placed().view.track.y + e.placed().view.track.h,
          "The thumb sits at the bottom");
    e.key(Key::up); e.key(Key::up); e.key(Key::up);
    check(e.first() == 16, "Moving the caret above the view scrolls up just enough");
    e.key(Key::home, false, true);
    check(e.first() == 0, "Ctrl+Home returns to the top");
    check(bool(e.field.set_value(numbered_lines(20))), "Code replaces the value");
    e.layout();
    check(e.caret() == 39 && e.first() == 17, "A value set from code scrolls to the caret it leaves at the end");
    check(bool(e.host().select("note", 4, 4)), "Code moves the caret");
    e.layout();
    check(e.first() == 2, "A caret moved from code is revealed too: from above the view it lands on the first row");
}

void scrolling_alone_does_not_move_the_caret_or_snap_back() {
    Editor e(numbered_lines(10), 3);
    check(bool(e.field.set_scroll_line(5)), "Scroll the view");
    e.layout();
    check(e.first() == 5 && e.caret() == 0, "The caret stays where it was, out of view");
    e.layout();
    check(e.first() == 5, "Arranging again does not reveal the caret");
    e.key(Key::right);
    check(e.first() == 0, "Moving the caret reveals it");
}

void the_wheel_scrolls_the_field_then_the_page() {
    Ui ui;
    auto field = connect(ui, multi("note", numbered_lines(10), 3));
    auto tall = *ui.add_label({.id = "tall", .value = numbered_lines(30)});
    FakeMeasure measure; ScrollState page, outer;
    auto node = layout::scroll(layout::column({layout::control(field), layout::control(tall)}, 8), outer, 200);
    auto frame = arrange(node, ui, web_appearance(), measure, 300, 400, 1.f, page);
    const auto* placed = frame.find(field);
    check(placed != nullptr && frame.regions.size() == 2 && frame.regions.back().field == "note", "The field is an inner scroll region of the page");
    const int x = placed->view.text.x + 10, y = placed->view.text.y + 10;
    check(frame.wheel(ui, x, y, -48) && scroll_line_of(field) == 2 && outer.offset == 0, "One notch scrolls two lines of the field, not the page");
    while (frame.wheel(ui, x, y, -48) && scroll_line_of(field) < 7) {}
    check(scroll_line_of(field) == 7 && outer.offset == 0, "The field reaches its last page");
    check(frame.wheel(ui, x, y, -48) && scroll_line_of(field) == 7 && outer.offset > 0, "At the bottom the wheel passes to the page");
    const int page_offset = outer.offset;
    check(frame.wheel(ui, x, y, 48) && scroll_line_of(field) == 5 && outer.offset == page_offset, "Scrolling back up moves the field first again");
    check(!frame.wheel(ui, 5000, 5000, -48), "A pointer outside every region scrolls nothing");
    check(scroll_line_of(field) == 5, "The field kept its position");
}

void a_dialog_scrolls_its_own_content_and_blocks_the_page() {
    Ui ui;
    auto background = *ui.add_label({.id = "background", .value = numbered_lines(40)});
    auto backup = *ui.add_button({.id = "backup", .label = u"Back"});
    auto cancel = *ui.add_button({.id = "cancel", .label = u"Cancel"});
    auto note = connect(ui, multi("note", numbered_lines(10), 3));
    auto dialog = *ui.add_dialog("dlg", {note, cancel}, note, backup, [](bool) {});
    FakeMeasure measure; ScrollState page, outer;
    auto content = layout::column({layout::control(note), layout::control(cancel)}, 8);
    auto root = layout::modal(layout::scroll(layout::control(background), outer, 150), content, dialog);
    check(bool(dialog.open()), "Open the dialog");
    auto frame = arrange(root, ui, web_appearance(), measure, 400, 400, 1.f, page);
    const auto* placed = frame.find(note);
    check(frame.modal && placed != nullptr, "The dialog's field is placed in the open dialog");
    const int x = placed->view.text.x + 10, y = placed->view.text.y + 10;
    check(frame.wheel(ui, x, y, -48) && scroll_line_of(note) == 2, "The wheel scrolls the field inside the dialog");
    check(!frame.wheel(ui, 5, 5, -48) && outer.offset == 0, "The page behind the dialog still does not scroll");
    const auto& region = *std::find_if(frame.regions.begin(), frame.regions.end(), [](const ScrollRegion& r) { return r.field == "note"; });
    check(region.modal_content && region.thumb.h > 0, "The field's scrollbar belongs to the dialog");
    const int travel = region.track.h - region.thumb.h;
    check(frame.scroll_to(ui, *region.state, region.offset_for_thumb(region.track.y + travel)) && scroll_line_of(note) == 7,
          "Dragging the thumb to the bottom scrolls the field to its last page");
    check(frame.scroll_to(ui, *region.state, region.offset_for_thumb(region.track.y + travel / 2)) && scroll_line_of(note) == 4,
          "Dragging the thumb halfway lands on the middle line");
}

void the_scroll_position_is_one_value_for_code_and_semantics() {
    Editor e(numbered_lines(10), 3);
    auto& tree = e.ui.semantics();
    check(scroll_line_of(e.field) == 0, "Starts at the top");
    check(bool(e.field.set_scroll_line(4)) && scroll_line_of(e.field) == 4, "Typed set");
    e.layout();
    check(e.first() == 4 && e.placed().view.lines[e.first()].begin == 8, "The first visible line is the fifth, which starts at offset 8");
    auto node = tree.get({"note"});
    check(node && node->scroll && node->scroll->first_line == 4 && node->scroll->visible_lines == 3 && node->scroll->line_count == 10,
          "The semantic node reports the same position, visible lines and total lines");
    check(std::find(node->verbs.begin(), node->verbs.end(), Verb::scroll) != node->verbs.end(), "The semantic node offers the scroll verb");
    check(bool(tree.perform({"note"}, Verb::scroll, 6)) && scroll_line_of(e.field) == 6, "Semantic scroll moves the same position");
    check(e.field.set_scroll_line(8).status == Status::invalid_value && tree.perform({"note"}, Verb::scroll, 8).status == Status::invalid_value &&
          scroll_line_of(e.field) == 6, "Both entry points reject a line past the last page, with the same status");
    check(tree.perform({"note"}, Verb::scroll, -1).status == Status::invalid_value, "A negative line is invalid");
    check(tree.perform({"note"}, Verb::scroll, std::u16string(u"x")).status == Status::invalid_value, "A scroll needs a number");
    check(bool(e.field.set_scroll_line(7)) && scroll_line_of(e.field) == 7, "The last page is the last valid position");
    e.layout();
    check(e.first() == 7, "Arranging keeps a position set from code");
}

void the_first_visible_line_survives_a_text_size_change() {
    std::u16string text;
    for (int i = 0; i < 6; ++i) { if (i) text += u'\n'; text += u"p" + std::u16string(1, static_cast<char16_t>(u'0' + i)) + std::u16string(37, u'a'); }
    Editor e(text, 3);
    check(e.placed().view.lines.size() == 12, "Six 39-character paragraphs wrap to 20 + 19 characters, two lines each, at 16 px");
    check(bool(e.field.set_scroll_line(6)), "Scroll to the fourth paragraph");
    e.layout();
    check(e.placed().view.lines[e.first()].begin == 120, "Its first line starts at offset 120");
    check(bool(e.ui.set_presentation({.text_size = 28})), "Raise the text size");
    e.layout();
    check(e.placed().view.lines.size() > 12 && e.placed().view.lines[e.first()].begin == 120 && scroll_line_of(e.field) == e.first(),
          "More lines now, and the first visible line still starts the same paragraph");
    check(e.first() == 12, "At 28 px each paragraph takes four lines");
}

void the_visible_line_count_can_change_at_run_time() {
    Editor e(numbered_lines(10), 3);
    int invalidations = 0;
    e.host().on_invalidate([&] { ++invalidations; });
    const int before = e.placed().bounds.h;
    check(bool(e.field.set_visible_lines(5)) && e.field.visible_lines().value.value() == 5 && invalidations > 0, "Set five lines and ask for a new frame");
    e.layout();
    check(e.placed().view.visible == 5 && e.placed().field.editor.h == 5 * 24 && e.placed().bounds.h == before + 2 * 24, "The field is two lines taller at once");
    check(e.ui.semantics().get({"note"})->scroll->visible_lines == 5 && e.field.describe()->visible_lines == 5, "Semantics report the new height");
    check(bool(e.field.set_scroll_line(5)), "Five visible lines of ten leave six scroll positions");
    check(bool(e.field.set_visible_lines(2)) && scroll_line_of(e.field) == 5, "Shrinking keeps the position");
    e.layout();
    check(e.placed().view.visible == 2 && e.first() == 5, "Two lines are shown from the same first line");
    check(e.field.set_visible_lines(0).status == Status::invalid_value && e.field.visible_lines().value.value() == 2, "At least one line");
    check(bool(e.field.set_visible_lines(20)) && scroll_line_of(e.field) == 0, "More lines than the text has leaves nothing to scroll");
    Ui single;
    auto name = connect(single, {.id = "name"});
    check(name.set_visible_lines(2).status == Status::not_supported && name.set_scroll_line(0).status == Status::not_supported &&
          name.scroll_line().status == Status::not_supported && name.visible_lines().status == Status::not_supported,
          "A single-line field has neither");
    check(single.semantics().perform({"name"}, Verb::scroll, 0).status == Status::not_supported, "Nor a semantic scroll");
}

// ---- Pointer and input method ---------------------------------------------------------------
void dragging_selects_across_lines_and_scrolls_at_the_edges() {
    Editor e(numbered_lines(10), 3);
    const auto view = e.placed().view; // a copy: layout() replaces the frame
    const int x = view.text.x, top = view.text.y, bottom = top + 3 * 24;
    check(bool(e.host().select("note", 0, 0)), "Press on the first character");
    check(e.frame.select_to(e.ui, e.field, x, top + 24 * 2 + 3) && e.anchor() == 0 && e.caret() == 4, "Dragging to the third line selects through it");
    check(e.first() == 0, "Inside the field nothing scrolls");
    check(e.frame.select_to(e.ui, e.field, x, bottom + 5) && scroll_line_of(e.field) == 1 && e.anchor() == 0 && e.caret() == 6,
          "Below the field it scrolls one line and the caret takes the last visible line (the fourth)");
    check(e.frame.select_to(e.ui, e.field, x, bottom + 50) && scroll_line_of(e.field) == 2 && e.caret() == 8,
          "Each call scrolls one more, from the current position rather than the frame's");
    e.layout();
    check(e.first() == 2 && e.caret() == 8, "The next frame keeps the scrolled view");
    check(e.frame.select_to(e.ui, e.field, x, top - 5) && scroll_line_of(e.field) == 1 && e.caret() == 2, "Above the field it scrolls back up");
    while (e.frame.select_to(e.ui, e.field, x, bottom + 5)) {}
    check(scroll_line_of(e.field) == 7 && e.caret() == 18 && !e.frame.select_to(e.ui, e.field, x, bottom + 5),
          "At the last page the drag stops scrolling and the caret stays on the last line");
    check(e.anchor() == 0, "The anchor never moved during the drag");
}

void a_click_past_a_wrapped_line_end_stays_on_that_line() {
    Editor e(std::u16string(30, u'a'), 3);
    const auto view = e.placed().view; // a copy: layout() replaces the frame
    auto hit = detail::placement_text_hit(e.frame, "note", view.text.x + 500, view.text.y + 3);
    check(hit.offset == 20 && hit.upstream, "Past the end of a wrapped piece: its end, flagged upstream");
    check(bool(e.host().select_pointer("note", hit.offset, hit.offset, hit.upstream)), "Place the caret there");
    e.layout();
    check(e.placed().caret_y(e.caret()) == view.text.y && e.placed().caret_x(e.caret()) == view.text.x + 20 * 8, "It is drawn at the end of the first line");
    auto second = detail::placement_text_hit(e.frame, "note", view.text.x + 500, view.text.y + 24 + 3);
    check(second.offset == 30 && !second.upstream, "Past the end of the last line is the end of the text");
}

void composition_wraps_and_the_candidate_position_follows_the_caret_line() {
    Editor e(std::u16string(18, u'a'), 3);
    e.put(18, 18);
    check(bool(e.host().composition_update(u"你好", 2)), "Start composing two wide characters");
    e.layout();
    const auto& p = e.placed();
    check(spans(p) == Spans{{0, 19}, {19, 20}}, "The composed text wraps like committed text");
    const auto rect = p.caret_rect(e.host().edit("note")->visible_caret());
    check(rect.y == p.view.text.y + 24 && rect.x == p.view.text.x + 16 && rect.h == 24, "The candidate window anchors at the caret's wrapped line");
    check(bool(e.host().composition_commit(u"你好")) && e.host().text("note") == std::u16string(18, u'a') + u"你好", "Commit it");

    Editor s(numbered_lines(3), 2);
    s.put(2, 2);
    check(bool(s.host().composition_update(u"x", 1)), "Compose on the second line");
    s.layout();
    check(s.placed().caret_rect(s.host().edit("note")->visible_caret()).y == s.placed().view.text.y + 24, "On the second row of the view");
    check(bool(s.field.set_scroll_line(1)), "Scroll one line while composing");
    s.layout();
    check(s.placed().caret_rect(s.host().edit("note")->visible_caret()).y == s.placed().view.text.y && s.host().edit("note")->composing,
          "The anchor follows the caret to the first row and the composition goes on");
}
}

int main() {
    const test::Case cases[] = {
        {"multiline values allow LF and reject CR", multiline_values_allow_line_feed_and_reject_carriage_return},
        {"semantic and typed set_value share the rule", semantic_and_typed_set_value_share_the_line_feed_rule},
        {"paste normalizes breaks and copy writes CRLF", paste_normalizes_every_line_break_to_one_line_feed},
        {"single-line paste still converts to spaces", single_line_paste_still_turns_breaks_into_spaces},
        {"a line feed counts one toward max_length", a_line_feed_counts_as_one_toward_max_length},
        {"a source value with CR is ignored", a_source_value_with_carriage_return_is_ignored},
        {"Enter inserts LF and no Enter shortcut fires", enter_inserts_a_line_feed_and_no_enter_shortcut_fires},
        {"Ctrl+Enter reaches its shortcut; Tab moves focus", ctrl_enter_reaches_the_shortcut_and_tab_moves_focus},
        {"each Enter is one undo entry", each_enter_is_one_undo_entry},
        {"Enter during composition does not break the line", enter_during_composition_does_not_break_the_line},
        {"single-line Enter still reaches its shortcut", single_line_enter_still_reaches_its_shortcut},
        {"read-only multi-line field refuses Enter", read_only_multiline_field_refuses_enter},
        {"a long line wraps and the height stays fixed", a_long_line_wraps_at_the_edge_and_the_height_stays_fixed},
        {"hard breaks make lines; wrapping prefers spaces", hard_breaks_make_lines_and_wrapping_prefers_spaces},
        {"the scrollbar width is always reserved", the_scrollbar_width_is_always_reserved},
        {"pointer positions map to line and offset", pointer_positions_map_to_line_and_offset},
        {"a wrap-boundary caret belongs to the next line", a_caret_on_a_wrap_boundary_belongs_to_the_next_line_unless_upstream},
        {"vertical keys move by display line and keep the column", vertical_keys_move_by_display_line_and_keep_the_column},
        {"vertical keys follow wrapped lines", vertical_keys_follow_wrapped_lines},
        {"Home/End use the line, Ctrl the whole text", home_and_end_use_the_current_line_and_ctrl_uses_the_whole_text},
        {"Shift with vertical keys extends the selection", shift_with_vertical_keys_extends_the_selection},
        {"page keys move by the visible line count", page_keys_move_by_the_visible_line_count},
        {"typing to line twenty scrolls to the caret", typing_to_line_twenty_scrolls_the_field_to_the_caret},
        {"scrolling alone neither moves the caret nor snaps back", scrolling_alone_does_not_move_the_caret_or_snap_back},
        {"the wheel scrolls the field, then the page", the_wheel_scrolls_the_field_then_the_page},
        {"a dialog scrolls its own content and blocks the page", a_dialog_scrolls_its_own_content_and_blocks_the_page},
        {"the scroll position is one value for code and semantics", the_scroll_position_is_one_value_for_code_and_semantics},
        {"the first visible line survives a text size change", the_first_visible_line_survives_a_text_size_change},
        {"the visible line count can change at run time", the_visible_line_count_can_change_at_run_time},
        {"dragging selects across lines and scrolls at the edges", dragging_selects_across_lines_and_scrolls_at_the_edges},
        {"a click past a wrapped line end stays on that line", a_click_past_a_wrapped_line_end_stays_on_that_line},
        {"composition wraps and the candidate follows the caret line", composition_wraps_and_the_candidate_position_follows_the_caret_line},
    };
    return test::run("multiline", cases);
}
