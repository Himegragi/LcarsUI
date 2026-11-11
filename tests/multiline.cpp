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
TextHandle connect(Ui& ui, Control control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto query = ui.add_text(std::move(control));
    check(bool(query), "Create the text field");
    auto field = *query;
    check(bool(field.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connect_confirmed([](const TextChange&) { return Result{}; })), "Connect a write");
    return field;
}
Control multi(std::string id, std::u16string value = {}, unsigned lines = 3) {
    return {.id = std::move(id), .kind = Kind::text, .value = std::move(value), .multiline = true, .visible_lines = lines};
}
std::u16string value_of(Ui& ui, std::string_view id) { return detail::access(ui).text(id); }

void multiline_values_allow_line_feed_and_reject_carriage_return() {
    Ui ui;
    check(bool(ui.add_text(multi("ok", u"a\nb"))), "A multi-line field may start with a line feed");
    check(ui.add_text(multi("cr", u"a\rb")).status == Status::invalid_value, "A carriage return is an invalid initial value");
    check(ui.add_text(multi("tab", u"a\tb")).status == Status::invalid_value, "Other control characters stay invalid");
    check(ui.add_text({.id = "single", .kind = Kind::text, .value = u"a\nb"}).status == Status::invalid_value,
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
    auto field = connect(ui, {.id = "name", .kind = Kind::text, .value = u"x"});
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
    auto field = connect(ui, {.id = "note", .kind = Kind::text, .max_length = 5, .multiline = true, .visible_lines = 2});
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
        name = connect(ui, {.id = "name", .kind = Kind::text, .value = u"x"});
        plain_enter = *ui.add_button({.id = "plain", .kind = Kind::button, .label = u"OK"});
        ctrl_enter = *ui.add_button({.id = "ctrl", .kind = Kind::button, .label = u"Apply"});
        scoped_enter = *ui.add_button({.id = "scoped", .kind = Kind::button, .label = u"Edit"});
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
    };
    return test::run("multiline", cases);
}
