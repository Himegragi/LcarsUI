#include "core_internal.hpp"
// #79 multi-line text fields through the public core and layout seams, with a deterministic
// fake text backend. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "text_geometry_checks.hpp"
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"
#include "isoduo/semantics.hpp"
using namespace isoduo;
using test::check;
using test::FakeMeasure;

namespace {
struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override {
        if (!content) return {Status::notSupported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, content};
    }
};
KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
KeyPress press(Key key, bool shift = false, bool control = false) { return {.key = key, .shift = shift, .ctrl = control}; }

// Every field gets an explicit write timing, as an application would.
TextFieldHandle connect(Ui& ui, ComponentConfig control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto query = ui.addTextField(std::move(control));
    check(bool(query), "Create the text field");
    auto field = *query;
    check(bool(field.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; })), "Connect a write");
    return field;
}
ComponentConfig multi(std::string id, std::u16string value = {}, unsigned lines = 3) {
    return {.id = std::move(id), .value = std::move(value), .multiline = true, .visibleLines = lines};
}
std::u16string valueOf(Ui& ui, std::string_view id) { return detail::access(ui).text(id); }

void multilineValuesAllowLineFeedAndRejectCarriageReturn() {
    Ui ui;
    check(bool(ui.addTextField(multi("ok", u"a\nb"))), "A multi-line field may start with a line feed");
    check(ui.addTextField(multi("cr", u"a\rb")).status == Status::invalidValue, "A carriage return is an invalid initial value");
    check(ui.addTextField(multi("tab", u"a\tb")).status == Status::invalidValue, "Other control characters stay invalid");
    check(ui.addTextField({.id = "single", .value = u"a\nb"}).status == Status::invalidValue,
          "A single-line field still rejects a line feed");
    check(ui.addTextField(multi("zero", u"", 0)).status == Status::invalidValue, "A multi-line field needs at least one visible line");
    check(validMultiLine(u"a\nb") && !validMultiLine(u"a\r\nb") && !validMultiLine(u"a\tb") && !validMultiLine(u"ab", 1),
          "valid_multi_line allows only LF, within the length");
    check(!validSingleLine(u"a\nb"), "valid_single_line is unchanged");
}

void semanticAndTypedSetValueShareTheLineFeedRule() {
    Ui ui;
    auto field = connect(ui, multi("note"));
    check(bool(field.setValue(u"one\ntwo")) && valueOf(ui, "note") == u"one\ntwo", "Typed set_value accepts a line feed");
    check(field.setValue(u"x\ry").status == Status::invalidValue && valueOf(ui, "note") == u"one\ntwo",
          "Typed set_value rejects CR and leaves the field unchanged");
    auto& tree = ui.semantics();
    check(bool(tree.perform({"note"}, Verb::setValue, std::u16string(u"semantic\nvalue"))) && field.draft().value.value() == u"semantic\nvalue",
          "Semantic set_value accepts a line feed");
    check(tree.perform({"note"}, Verb::setValue, std::u16string(u"bad\r")).status == Status::invalidValue && valueOf(ui, "note") == u"semantic\nvalue",
          "Semantic set_value rejects CR with the same status");
    check(tree.get({"note"})->value == u"semantic\nvalue", "The semantic value includes the line feed");
}

void pasteNormalizesEveryLineBreakToOneLineFeed() {
    Ui ui;
    auto field = connect(ui, multi("note"));
    MemoryClipboard clipboard;
    auto& host = detail::access(ui);
    host.setClipboard(&clipboard); host.focus("note");
    clipboard.content = u"a\r\nb\rc\nd";
    host.keyDown(ctrl(u'V'));
    check(valueOf(ui, "note") == u"a\nb\nc\nd", "CRLF, lone CR and lone LF each paste as one LF");
    host.keyDown(ctrl(u'Z'));
    check(valueOf(ui, "note").empty(), "One paste is one undo entry");
    host.keyDown(ctrl(u'Y'));
    check(valueOf(ui, "note") == u"a\nb\nc\nd", "Redo restores the paste");
    check(pasteAsMultiLine(u"x\r\n\r\ny") == u"x\n\ny", "paste_as_multi_line counts CRLF once");
    check(pasteAsSingleLine(u"x\r\ny") == u"x y", "paste_as_single_line is unchanged");
    host.select("note", 0, 3);
    host.keyDown(ctrl(u'C'));
    check(clipboard.content == u"a\r\nb", "Copy writes line breaks as CRLF");
    check(valueOf(ui, "note") == u"a\nb\nc\nd", "Copy does not change the value");
    host.keyDown(ctrl(u'X'));
    check(clipboard.content == u"a\r\nb" && valueOf(ui, "note") == u"\nc\nd", "Cut also writes CRLF");
}

void singleLinePasteStillTurnsBreaksIntoSpaces() {
    Ui ui;
    auto field = connect(ui, {.id = "name", .value = u"x"});
    MemoryClipboard clipboard;
    auto& host = detail::access(ui);
    host.setClipboard(&clipboard); host.focus("name");
    clipboard.content = u"a\r\nb\rc\nd";
    host.keyDown(ctrl(u'A'));
    host.keyDown(ctrl(u'V'));
    check(valueOf(ui, "name") == u"a b c d", "A single-line paste converts breaks to spaces");
    host.select("name", 0, 7);
    host.keyDown(ctrl(u'C'));
    check(clipboard.content == u"a b c d", "A single-line copy is not converted");
}

void aLineFeedCountsAsOneTowardMaxLength() {
    Ui ui;
    auto field = connect(ui, {.id = "note", .maxLength = 5, .multiline = true, .visibleLines = 2});
    check(bool(field.setValue(u"ab\ncd")), "Five characters including a line feed fit a limit of five");
    check(field.setValue(u"ab\ncde").status == Status::invalidValue && valueOf(ui, "note") == u"ab\ncd", "Six characters are rejected whole");
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 5, 5);
    check(host.paste("note", u"\r\n").status == Status::invalidValue && valueOf(ui, "note") == u"ab\ncd",
          "A pasted CRLF counts once and is rejected at the limit");
    host.select("note", 2, 3);
    check(bool(host.paste("note", u"\r\n")) && valueOf(ui, "note") == u"ab\ncd", "Replacing the line feed with CRLF stays within the limit");
}

void aSourceValueWithCarriageReturnIsIgnored() {
    Ui ui;
    auto model = std::make_shared<std::u16string>(u"a\nb");
    std::function<void()> notify;
    auto field = *ui.addTextField(multi("note", u"a\nb"));
    check(bool(field.connectSource({[model] { return *model; }, [&](std::function<void()> cb) { notify = cb; return [] {}; }})), "Connect a source");
    *model = u"c\rd"; notify();
    check(valueOf(ui, "note") == u"a\nb", "An invalid source value is ignored like any invalid value");
    *model = u"c\nd"; notify();
    check(valueOf(ui, "note") == u"c\nd", "A valid multi-line source value is adopted");
}

// A form with a multi-line field, a single-line field and three shortcut targets.
struct Form {
    Ui ui;
    TextFieldHandle note, name;
    ButtonHandle plainEnter, ctrlEnter, scopedEnter;
    int plain = 0, withCtrl = 0, scoped = 0;
    Form() {
        note = connect(ui, multi("note"));
        name = connect(ui, {.id = "name", .value = u"x"});
        plainEnter = *ui.addButton({.id = "plain", .label = u"OK"});
        ctrlEnter = *ui.addButton({.id = "ctrl", .label = u"Apply"});
        scopedEnter = *ui.addButton({.id = "scoped", .label = u"Edit"});
        check(bool(plainEnter.bind([this] { ++plain; return Result{}; })), "Bind the global Enter action");
        check(bool(ctrlEnter.bind([this] { ++withCtrl; return Result{}; })), "Bind the Ctrl+Enter action");
        check(bool(scopedEnter.bind([this] { ++scoped; return Result{}; })), "Bind the scoped Enter action");
        check(bool(ui.addShortcut({.key = Key::enter}, plainEnter)), "Global Enter shortcut");
        check(bool(ui.addShortcut({.key = Key::enter, .ctrl = true}, ctrlEnter)), "Ctrl+Enter shortcut");
        check(bool(ui.addShortcut({.key = Key::enter}, scopedEnter, note)), "Enter shortcut scoped to the multi-line field");
        check(bool(ui.addShortcut({.key = Key::enter}, scopedEnter, name)), "Enter shortcut scoped to the single-line field");
    }
    detail::UiState& host() { return detail::access(ui); }
};

void enterInsertsALineFeedAndNoEnterShortcutFires() {
    Form form; auto& host = form.host();
    host.focus("note");
    check(bool(host.keyDown(press(Key::enter))) && valueOf(form.ui, "note") == u"\n", "Enter inserts a line feed");
    host.textInput(u"a");
    check(bool(host.keyDown(press(Key::enter, true))) && valueOf(form.ui, "note") == u"\na\n", "Shift+Enter inserts a line feed too");
    check(form.plain == 0 && form.scoped == 0 && form.withCtrl == 0, "Neither the global nor the scoped Enter shortcut fired");
    host.select("note", 1, 1);
    host.keyDown(press(Key::enter));
    check(valueOf(form.ui, "note") == u"\n\na\n", "Enter inserts at the caret");
    check(host.edit("note")->selection.caret == 2 && host.edit("note")->selection.anchor == 2, "The caret follows the inserted break");
    host.select("note", 0, 3);
    host.keyDown(press(Key::enter));
    check(valueOf(form.ui, "note") == u"\n\n", "Enter replaces the selection (three of four characters) with one break");
}

void ctrlEnterReachesTheShortcutAndTabMovesFocus() {
    Form form; auto& host = form.host();
    host.focus("note");
    host.keyDown(press(Key::enter, false, true));
    check(form.withCtrl == 1 && form.plain == 0 && valueOf(form.ui, "note").empty(), "Ctrl+Enter triggers its shortcut and inserts nothing");
    host.keyDown(press(Key::tab));
    check(host.focused() == "name" && valueOf(form.ui, "note").empty(), "Tab moves the focus and inserts no tab character");
    host.keyDown(press(Key::tab, true));
    check(host.focused() == "note", "Shift+Tab moves the focus back");
}

void eachEnterIsOneUndoEntry() {
    Form form; auto& host = form.host();
    host.focus("note");
    host.textInput(u"a"); host.keyDown(press(Key::enter)); host.textInput(u"b");
    check(valueOf(form.ui, "note") == u"a\nb", "Typed text with a break");
    host.keyDown(ctrl(u'Z'));
    check(valueOf(form.ui, "note") == u"a\n", "Undo removes the last typed character");
    host.keyDown(ctrl(u'Z'));
    check(valueOf(form.ui, "note") == u"a", "Undo removes the break by itself");
    host.keyDown(ctrl(u'Z'));
    check(valueOf(form.ui, "note").empty(), "Undo removes the first character");
    host.keyDown(ctrl(u'Y'));
    host.keyDown(ctrl(u'Y'));
    check(valueOf(form.ui, "note") == u"a\n", "Redo restores the break as one entry");
}

void enterDuringCompositionDoesNotBreakTheLine() {
    Form form; auto& host = form.host();
    host.focus("note");
    host.textInput(u"a");
    check(bool(host.compositionUpdate(u"ㄅㄉ", 2)), "Start composing");
    auto result = host.keyDown(press(Key::enter));
    check(result.status == Status::busy && host.edit("note")->composing, "Enter during composition is left to the input method");
    check(valueOf(form.ui, "note") == u"a" && form.plain == 0 && form.scoped == 0, "No break and no shortcut");
    check(bool(host.compositionCommit(u"你")) && valueOf(form.ui, "note") == u"a你", "The input method commits the chosen text");
    check(bool(host.keyDown(press(Key::enter))) && valueOf(form.ui, "note") == u"a你\n", "Enter breaks the line again after the composition");
}

void inputMethodReplacementIsOneUndoEntry() {
    Form form;
    auto& host = form.host();
    auto field = connect(form.ui, multi("replace", u"甲🙂舊文乙"));
    field.focus();
    host.select("replace", 3, 5);
    host.compositionUpdate(u"中", 1);
    check(field.draft().value == u"甲🙂舊文乙" && host.edit("replace")->visible() == u"甲🙂中乙",
          "Preedit temporarily replaces the selection only in visible text");
    check(field.setValue(u"other").status == Status::busy && host.keyDown(press(Key::enter)).status == Status::busy,
          "Competing edits and Enter leave the composition alone");
    check(bool(host.compositionCommit(u"中文", 2, 0)) && field.draft().value == u"甲中文乙",
          "The IME can replace the selection and a supplementary character before it");
    check(bool(host.undo("replace")) && field.draft().value == u"甲🙂舊文乙" &&
          host.edit("replace")->selection.anchor == 3 && host.edit("replace")->selection.caret == 5,
          "One undo restores the original text and selection");
    host.compositionUpdate(u"取消", 2);
    host.compositionCancel();
    check(bool(host.redo("replace")) && field.draft().value == u"甲中文乙",
          "Cancel preserves the redo entry");
}

void singleLineEnterStillReachesItsShortcut() {
    Form form; auto& host = form.host();
    host.focus("name");
    host.keyDown(press(Key::enter));
    check(form.scoped == 1 && form.plain == 0 && valueOf(form.ui, "name") == u"x", "A single-line field leaves Enter to its shortcut");
}

void readOnlyMultilineFieldRefusesEnter() {
    Form form; auto& host = form.host();
    host.focus("note");
    form.note.readOnly(true);
    check(host.keyDown(press(Key::enter)).status == Status::readOnly && valueOf(form.ui, "note").empty() && form.scoped == 0,
          "A read-only multi-line field neither edits nor lets Enter through");
}

// ---- Layout -------------------------------------------------------------------------------
// FakeMeasure at 16 px text: an ASCII character is 8 px wide and a line is 24 px tall. A 200 px
// field has a 176 px editor, a 14 px scrollbar reserve and a 2 px caret slack, so a line holds
// (200 - 24 - 14 - 2) / 8 = 20 characters.
constexpr int fieldWidth = 200;
struct Laid {
    FakeMeasure measure; ScrollState scroll; Frame frame;
    const Placement* field = nullptr;
    Laid(Ui& ui, const TextFieldHandle& handle, int width = fieldWidth) {
        frame = arrange(layout::component(handle), ui, webAppearance(), measure, width, 400, 1.f, scroll);
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

void aLongLineWrapsAtTheEdgeAndTheHeightStaysFixed() {
    Ui ui;
    auto field = connect(ui, multi("note", std::u16string(45, u'a')));
    Laid laid(ui, field);
    check(spans(*laid.field) == Spans{{0, 20}, {20, 40}, {40, 45}}, "45 characters wrap into 20 + 20 + 5");
    check(laid.field->view.lines[0].soft && laid.field->view.lines[1].soft && !laid.field->view.lines[2].soft, "Only wrapped pieces continue into the next line");
    check(laid.field->textField.editor.h == 3 * 24 && laid.field->view.visible == 3 && laid.field->view.lineHeight == 24, "The editor is exactly three lines tall");
    Ui other;
    auto longer = connect(other, multi("note", std::u16string(400, u'a')));
    Laid tall(other, longer);
    check(tall.field->bounds.h == laid.field->bounds.h, "More content never lengthens the field");
    check(tall.field->view.lines.size() == 20 && tall.field->view.first == 0, "A long text keeps every line and shows the first ones");
}

void hardBreaksMakeLinesAndWrappingPrefersSpaces() {
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

void theScrollbarWidthIsAlwaysReserved() {
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

void pointerPositionsMapToLineAndOffset() {
    Ui ui;
    auto field = connect(ui, multi("note", u"abc\ndef\nghi"));
    Laid laid(ui, field);
    const auto& view = laid.field->view;
    auto at = [&](int column, int row) { return laid.frame.textOffset(field, view.text.x + 8 * column + 1, view.text.y + 24 * row + 3); };
    check(at(0, 0) == 0 && at(2, 0) == 2 && at(3, 0) == 3, "First line: columns map to offsets");
    check(at(2, 1) == 6 && at(1, 2) == 9, "Second and third lines add their start offsets");
    check(at(9, 0) == 3 && at(9, 1) == 7, "Past the end of a line is that line's end, before its break");
    check(laid.frame.textOffset(field, view.text.x, view.text.y + 24 * 5) == 8, "Below the last line is that line at the same column");
    check(laid.frame.textOffset(field, view.text.x, view.text.y - 40) == 0, "Above the first line is the start");
    check(laid.field->caretX(5) == view.text.x + 8 && laid.field->caretY(5) == view.text.y + 24, "Caret position of offset 5: column 1, second line");
}

void aWideStyledStopAfterAPreferredBreakStillFits() {
    FakeMeasure measure;
    constexpr std::u16string_view text = u" bbbc\n\U0001f600\n";
    const std::vector<StyleSpan> spans{{.start = 4, .length = 1, .bold = true, .color = {}}};
    const auto lines = wrapLines(measure, text, 16, 32, &spans);
    check(lines.size() == 5, "The preferred space break leaves three narrow letters and a wide letter on separate lines");
    check(lines[0].begin == 0 && lines[0].end == 1 && lines[0].soft && lines[0].run.width == 8 &&
          lines[1].begin == 1 && lines[1].end == 4 && lines[1].soft && lines[1].run.width == 24 &&
          lines[2].begin == 4 && lines[2].end == 5 && !lines[2].soft && lines[2].run.width == 10,
          "The styled advance is included and the paragraph's LF is excluded");
    check(lines[3].begin == 6 && lines[3].end == 8 && !lines[3].soft && lines[3].run.width == 16 &&
          lines[3].run.stops.size() == 2 && lines[4].begin == 9 && lines[4].end == 9,
          "An emoji takes one advance and a trailing LF leaves an empty line");
}

void publicWrappingMarksParagraphsAndPreservesEmptyLines() {
    FakeMeasure measure;
    constexpr std::u16string_view text = u"\n甲乙\U0001f600\n";
    const std::vector<std::u16string> expected{u"", u"甲乙", u"\U0001f600", u""};
    check(wrap(measure, text, 16, 32) == expected, "A leading and trailing LF each leave an empty line");
    const auto marked = wrapMarked(measure, text, 16, 32);
    check(marked.size() == expected.size(), "Marked and plain wrapping produce the same display lines");
    for (std::size_t i = 0; i < marked.size(); ++i)
        check(marked[i].text == expected[i] && marked[i].soft == (i == 1), "Only the middle cut is soft");
}

void aCaretOnAWrapBoundaryBelongsToTheNextLineUnlessUpstream() {
    Ui ui;
    auto field = connect(ui, multi("note", std::u16string(30, u'a')));
    Laid laid(ui, field);
    const auto& view = laid.field->view;
    check(view.lineOf(20) == 1 && laid.field->caretY(20) == view.text.y + 24 && laid.field->caretX(20) == view.text.x,
          "A caret at the wrap point is drawn at the start of the next line");
    check(view.lineOf(19) == 0 && view.lineOf(0) == 0 && view.lineOf(30) == 1, "Other offsets follow their lines");
    auto upstream = view;
    upstream.upstream = true;
    check(upstream.lineOf(20) == 0 && upstream.lineOf(21) == 1, "Upstream puts the wrap-point caret on the line above");
    check(test::caretMatchesHit(laid.frame, field, 1, 20), "A click on the drawn downstream caret returns the wrap offset");
}

// ---- Cursor movement ----------------------------------------------------------------------
// The host arranges after every input; so does this fixture, which is what keeps core's copy of the lines fresh.
struct Editor {
    Ui ui; TextFieldHandle field; FakeMeasure measure; ScrollState scroll; Frame frame;
    explicit Editor(std::u16string value, unsigned lines = 3, int width = fieldWidth) : width_(width) {
        field = connect(ui, multi("note", std::move(value), lines));
        host().focus("note");
        layout();
    }
    detail::UiState& host() { return detail::access(ui); }
    void layout() { frame = arrange(layout::component(field), ui, webAppearance(), measure, width_, 400, 1.f, scroll); }
    const Placement& placed() const { return *frame.find(field); }
    void key(Key k, bool shift = false, bool control = false) { host().keyDown(press(k, shift, control)); layout(); }
    void put(unsigned anchor, unsigned caret) { check(bool(host().select("note", anchor, caret)), "Select"); layout(); }
    unsigned caret() { return host().edit("note")->selection.caret; }
    unsigned anchor() { return host().edit("note")->selection.anchor; }
    unsigned first() const { return placed().view.first; }
    int width_;
};

void verticalKeysMoveByDisplayLineAndKeepTheColumn() {
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

void verticalKeysFollowWrappedLines() {
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
    check(e.caret() == 20 && e.host().edit("note")->caretUpstream, "End on a wrapped piece stops at its end, upstream");
    check(e.placed().caretY(e.caret()) == e.placed().view.text.y, "The upstream caret is drawn on the first line");
    e.key(Key::right);
    check(e.caret() == 21 && !e.host().edit("note")->caretUpstream && e.placed().caretY(21) == e.placed().view.text.y + 24, "Right leaves the wrap point");
    e.put(25, 25);
    e.key(Key::end);
    check(e.caret() == 30 && !e.host().edit("note")->caretUpstream, "End on the last piece is the end of the text");
}

void homeAndEndUseTheCurrentLineAndCtrlUsesTheWholeText() {
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

void shiftWithVerticalKeysExtendsTheSelection() {
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

void pageKeysMoveByTheVisibleLineCount() {
    Editor e(u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9", 3);
    check(e.placed().view.lines.size() == 10 && e.first() == 0, "Ten lines, three visible");
    e.put(0, 0);
    e.key(Key::pageDown);
    check(e.caret() == 6 && e.first() == 3, "PageDown moves three lines and scrolls three lines");
    e.key(Key::pageDown);
    check(e.caret() == 12 && e.first() == 6, "PageDown again");
    e.key(Key::pageDown);
    check(e.caret() == 18 && e.first() == 7, "PageDown at the end stops on the last line with the last page showing");
    e.key(Key::pageUp);
    check(e.caret() == 12 && e.first() == 4, "PageUp moves three lines back, the view follows");
    e.key(Key::pageUp, true);
    check(e.anchor() == 12 && e.caret() == 6, "Shift+PageUp extends the selection");
}

// ---- Scrolling ----------------------------------------------------------------------------
std::u16string numberedLines(int count) {
    std::u16string text;
    for (int i = 0; i < count; ++i) { if (i) text += u'\n'; text += std::u16string(1, static_cast<char16_t>(u'0' + i % 10)); }
    return text;
}
unsigned scrollLineOf(const TextFieldHandle& field) { return field.scrollLine().value.value(); }

void typingToLineTwentyScrollsTheFieldToTheCaret() {
    Editor e(u"x", 3);
    e.key(Key::end, false, true);
    for (int i = 0; i < 19; ++i) {
        e.key(Key::enter);
        e.host().textInput(u"y"); e.layout();
    }
    check(e.placed().view.lines.size() == 20 && e.caret() == 39, "Twenty lines, the caret on the last");
    check(e.first() == 17, "The field scrolled so the caret's line is the last visible one");
    check(e.placed().view.thumb.h > 0 && e.placed().view.thumb.y + e.placed().view.thumb.h == e.placed().view.track.y + e.placed().view.track.h,
          "The thumb sits at the bottom");
    e.key(Key::up); e.key(Key::up); e.key(Key::up);
    check(e.first() == 16, "Moving the caret above the view scrolls up just enough");
    e.key(Key::home, false, true);
    check(e.first() == 0, "Ctrl+Home returns to the top");
    check(bool(e.field.setValue(numberedLines(20))), "Code replaces the value");
    e.layout();
    check(e.caret() == 39 && e.first() == 17, "A value set from code scrolls to the caret it leaves at the end");
    check(bool(e.host().select("note", 4, 4)), "Code moves the caret");
    e.layout();
    check(e.first() == 2, "A caret moved from code is revealed too: from above the view it lands on the first row");
}

void scrollingAloneDoesNotMoveTheCaretOrSnapBack() {
    Editor e(numberedLines(10), 3);
    check(bool(e.field.setScrollLine(5)), "Scroll the view");
    e.layout();
    check(e.first() == 5 && e.caret() == 0, "The caret stays where it was, out of view");
    e.layout();
    check(e.first() == 5, "Arranging again does not reveal the caret");
    e.key(Key::right);
    check(e.first() == 0, "Moving the caret reveals it");
}

void theWheelScrollsTheFieldThenThePage() {
    Ui ui;
    auto field = connect(ui, multi("note", numberedLines(10), 3));
    auto tall = *ui.addLabel({.id = "tall", .value = numberedLines(30)});
    FakeMeasure measure; ScrollState page, outer;
    auto node = layout::scroll(layout::column({layout::component(field), layout::component(tall)}, 8), outer, 200);
    auto frame = arrange(node, ui, webAppearance(), measure, 300, 400, 1.f, page);
    const auto* placed = frame.find(field);
    check(placed != nullptr && frame.regions.size() == 2 && frame.regions.back().textField == "note", "The field is an inner scroll region of the page");
    const int x = placed->view.text.x + 10, y = placed->view.text.y + 10;
    check(frame.wheel(ui, x, y, -48) && scrollLineOf(field) == 2 && outer.offset == 0, "One notch scrolls two lines of the field, not the page");
    while (frame.wheel(ui, x, y, -48) && scrollLineOf(field) < 7) {}
    check(scrollLineOf(field) == 7 && outer.offset == 0, "The field reaches its last page");
    check(frame.wheel(ui, x, y, -48) && scrollLineOf(field) == 7 && outer.offset > 0, "At the bottom the wheel passes to the page");
    const int pageOffset = outer.offset;
    check(frame.wheel(ui, x, y, 48) && scrollLineOf(field) == 5 && outer.offset == pageOffset, "Scrolling back up moves the field first again");
    check(!frame.wheel(ui, 5000, 5000, -48), "A pointer outside every region scrolls nothing");
    check(scrollLineOf(field) == 5, "The field kept its position");
}

void aDialogScrollsItsOwnContentAndBlocksThePage() {
    Ui ui;
    auto background = *ui.addLabel({.id = "background", .value = numberedLines(40)});
    auto backup = *ui.addButton({.id = "backup", .label = u"Back"});
    auto cancel = *ui.addButton({.id = "cancel", .label = u"Cancel"});
    auto note = connect(ui, multi("note", numberedLines(10), 3));
    auto dialog = *ui.addDialog("dlg", {note, cancel}, note, backup, [](bool) {});
    FakeMeasure measure; ScrollState page, outer;
    auto content = layout::column({layout::component(note), layout::component(cancel)}, 8);
    auto root = layout::modal(layout::scroll(layout::component(background), outer, 150), content, dialog);
    check(bool(dialog.open()), "Open the dialog");
    auto frame = arrange(root, ui, webAppearance(), measure, 400, 400, 1.f, page);
    const auto* placed = frame.find(note);
    check(frame.modal && placed != nullptr, "The dialog's field is placed in the open dialog");
    const int x = placed->view.text.x + 10, y = placed->view.text.y + 10;
    check(frame.wheel(ui, x, y, -48) && scrollLineOf(note) == 2, "The wheel scrolls the field inside the dialog");
    check(!frame.wheel(ui, 5, 5, -48) && outer.offset == 0, "The page behind the dialog still does not scroll");
    const auto& region = *std::find_if(frame.regions.begin(), frame.regions.end(), [](const ScrollRegion& r) { return r.textField == "note"; });
    check(region.modalContent && region.thumb.h > 0, "The field's scrollbar belongs to the dialog");
    const int travel = region.track.h - region.thumb.h;
    check(frame.scrollTo(ui, *region.state, region.offsetForThumb(region.track.y + travel)) && scrollLineOf(note) == 7,
          "Dragging the thumb to the bottom scrolls the field to its last page");
    check(frame.scrollTo(ui, *region.state, region.offsetForThumb(region.track.y + travel / 2)) && scrollLineOf(note) == 4,
          "Dragging the thumb halfway lands on the middle line");
}

void theScrollPositionIsOneValueForCodeAndSemantics() {
    Editor e(numberedLines(10), 3);
    auto& tree = e.ui.semantics();
    check(scrollLineOf(e.field) == 0, "Starts at the top");
    check(bool(e.field.setScrollLine(4)) && scrollLineOf(e.field) == 4, "Typed set");
    e.layout();
    check(e.first() == 4 && e.placed().view.lines[e.first()].begin == 8, "The first visible line is the fifth, which starts at offset 8");
    auto node = tree.get({"note"});
    check(node && node->scroll && node->scroll->firstLine == 4 && node->scroll->visibleLines == 3 && node->scroll->lineCount == 10,
          "The semantic node reports the same position, visible lines and total lines");
    check(std::find(node->verbs.begin(), node->verbs.end(), Verb::scroll) != node->verbs.end(), "The semantic node offers the scroll verb");
    check(bool(tree.perform({"note"}, Verb::scroll, 6)) && scrollLineOf(e.field) == 6, "Semantic scroll moves the same position");
    check(e.field.setScrollLine(8).status == Status::invalidValue && tree.perform({"note"}, Verb::scroll, 8).status == Status::invalidValue &&
          scrollLineOf(e.field) == 6, "Both entry points reject a line past the last page, with the same status");
    check(tree.perform({"note"}, Verb::scroll, -1).status == Status::invalidValue, "A negative line is invalid");
    check(tree.perform({"note"}, Verb::scroll, std::u16string(u"x")).status == Status::invalidValue, "A scroll needs a number");
    check(bool(e.field.setScrollLine(7)) && scrollLineOf(e.field) == 7, "The last page is the last valid position");
    e.layout();
    check(e.first() == 7, "Arranging keeps a position set from code");
}

void theFirstVisibleLineSurvivesATextSizeChange() {
    std::u16string text;
    for (int i = 0; i < 6; ++i) { if (i) text += u'\n'; text += u"p" + std::u16string(1, static_cast<char16_t>(u'0' + i)) + std::u16string(37, u'a'); }
    Editor e(text, 3);
    check(e.placed().view.lines.size() == 12, "Six 39-character paragraphs wrap to 20 + 19 characters, two lines each, at 16 px");
    check(bool(e.field.setScrollLine(6)), "Scroll to the fourth paragraph");
    e.layout();
    check(e.placed().view.lines[e.first()].begin == 120, "Its first line starts at offset 120");
    check(bool(e.ui.setPresentation({.textSize = 28})), "Raise the text size");
    e.layout();
    check(e.placed().view.lines.size() > 12 && e.placed().view.lines[e.first()].begin == 120 && scrollLineOf(e.field) == e.first(),
          "More lines now, and the first visible line still starts the same paragraph");
    check(e.first() == 12, "At 28 px each paragraph takes four lines");
}

void theVisibleLineCountCanChangeAtRunTime() {
    Editor e(numberedLines(10), 3);
    int invalidations = 0;
    e.host().onInvalidate([&] { ++invalidations; });
    const int before = e.placed().bounds.h;
    check(bool(e.field.setVisibleLines(5)) && e.field.visibleLines().value.value() == 5 && invalidations > 0, "Set five lines and ask for a new frame");
    e.layout();
    check(e.placed().view.visible == 5 && e.placed().textField.editor.h == 5 * 24 && e.placed().bounds.h == before + 2 * 24, "The field is two lines taller at once");
    check(e.ui.semantics().get({"note"})->scroll->visibleLines == 5 && e.field.describe()->scroll->visibleLines == 5, "Semantics report the new height");
    check(bool(e.field.setScrollLine(5)), "Five visible lines of ten leave six scroll positions");
    check(bool(e.field.setVisibleLines(2)) && scrollLineOf(e.field) == 5, "Shrinking keeps the position");
    e.layout();
    check(e.placed().view.visible == 2 && e.first() == 5, "Two lines are shown from the same first line");
    check(e.field.setVisibleLines(0).status == Status::invalidValue && e.field.visibleLines().value.value() == 2, "At least one line");
    check(bool(e.field.setVisibleLines(20)) && scrollLineOf(e.field) == 0, "More lines than the text has leaves nothing to scroll");
    Ui single;
    auto name = connect(single, {.id = "name"});
    check(name.setVisibleLines(2).status == Status::notSupported && name.setScrollLine(0).status == Status::notSupported &&
          name.scrollLine().status == Status::notSupported && name.visibleLines().status == Status::notSupported,
          "A single-line field has neither");
    check(single.semantics().perform({"name"}, Verb::scroll, 0).status == Status::notSupported, "Nor a semantic scroll");
}

// ---- Pointer and input method ---------------------------------------------------------------
void draggingSelectsAcrossLinesAndScrollsAtTheEdges() {
    Editor e(numberedLines(10), 3);
    const auto view = e.placed().view; // a copy: layout() replaces the frame
    const int x = view.text.x, top = view.text.y, bottom = top + 3 * 24;
    check(bool(e.host().select("note", 0, 0)), "Press on the first character");
    check(e.frame.selectTo(e.ui, e.field, x, top + 24 * 2 + 3) && e.anchor() == 0 && e.caret() == 4, "Dragging to the third line selects through it");
    check(e.first() == 0, "Inside the field nothing scrolls");
    check(e.frame.selectTo(e.ui, e.field, x, bottom + 5) && scrollLineOf(e.field) == 1 && e.anchor() == 0 && e.caret() == 6,
          "Below the field it scrolls one line and the caret takes the last visible line (the fourth)");
    check(e.frame.selectTo(e.ui, e.field, x, bottom + 50) && scrollLineOf(e.field) == 2 && e.caret() == 8,
          "Each call scrolls one more, from the current position rather than the frame's");
    e.layout();
    check(e.first() == 2 && e.caret() == 8, "The next frame keeps the scrolled view");
    check(e.frame.selectTo(e.ui, e.field, x, top - 5) && scrollLineOf(e.field) == 1 && e.caret() == 2, "Above the field it scrolls back up");
    while (e.frame.selectTo(e.ui, e.field, x, bottom + 5)) {}
    check(scrollLineOf(e.field) == 7 && e.caret() == 18 && !e.frame.selectTo(e.ui, e.field, x, bottom + 5),
          "At the last page the drag stops scrolling and the caret stays on the last line");
    check(e.anchor() == 0, "The anchor never moved during the drag");
}

void aClickPastAWrappedLineEndStaysOnThatLine() {
    Editor e(std::u16string(30, u'a'), 3);
    const auto view = e.placed().view; // a copy: layout() replaces the frame
    auto hit = detail::placementTextHit(e.frame, "note", view.text.x + 500, view.text.y + 3);
    check(hit.offset == 20 && hit.upstream, "Past the end of a wrapped piece: its end, flagged upstream");
    check(bool(e.host().selectPointer("note", hit.offset, hit.offset, hit.upstream)), "Place the caret there");
    e.layout();
    check(e.placed().caretY(e.caret()) == view.text.y && e.placed().caretX(e.caret()) == view.text.x + 20 * 8, "It is drawn at the end of the first line");
    auto second = detail::placementTextHit(e.frame, "note", view.text.x + 500, view.text.y + 24 + 3);
    check(second.offset == 30 && !second.upstream, "Past the end of the last line is the end of the text");
}

void compositionWrapsAndTheCandidatePositionFollowsTheCaretLine() {
    Editor e(std::u16string(18, u'a'), 3);
    e.put(18, 18);
    check(bool(e.host().compositionUpdate(u"你好", 2)), "Start composing two wide characters");
    e.layout();
    const auto& p = e.placed();
    check(spans(p) == Spans{{0, 19}, {19, 20}}, "The composed text wraps like committed text");
    const auto rect = p.caretRect(e.host().edit("note")->visibleCaret());
    check(rect.y == p.view.text.y + 24 && rect.x == p.view.text.x + 16 && rect.h == 24, "The candidate window anchors at the caret's wrapped line");
    check(bool(e.host().compositionCommit(u"你好")) && e.host().text("note") == std::u16string(18, u'a') + u"你好", "Commit it");

    Editor s(numberedLines(3), 2);
    s.put(2, 2);
    check(bool(s.host().compositionUpdate(u"x", 1)), "Compose on the second line");
    s.layout();
    check(s.placed().caretRect(s.host().edit("note")->visibleCaret()).y == s.placed().view.text.y + 24, "On the second row of the view");
    check(bool(s.field.setScrollLine(1)), "Scroll one line while composing");
    s.layout();
    check(s.placed().caretRect(s.host().edit("note")->visibleCaret()).y == s.placed().view.text.y && s.host().edit("note")->composing,
          "The anchor follows the caret to the first row and the composition goes on");
}
}

int main() {
    const test::Case cases[] = {
        {"multiline values allow LF and reject CR", multilineValuesAllowLineFeedAndRejectCarriageReturn},
        {"semantic and typed set_value share the rule", semanticAndTypedSetValueShareTheLineFeedRule},
        {"paste normalizes breaks and copy writes CRLF", pasteNormalizesEveryLineBreakToOneLineFeed},
        {"single-line paste still converts to spaces", singleLinePasteStillTurnsBreaksIntoSpaces},
        {"a line feed counts one toward max_length", aLineFeedCountsAsOneTowardMaxLength},
        {"a source value with CR is ignored", aSourceValueWithCarriageReturnIsIgnored},
        {"Enter inserts LF and no Enter shortcut fires", enterInsertsALineFeedAndNoEnterShortcutFires},
        {"Ctrl+Enter reaches its shortcut; Tab moves focus", ctrlEnterReachesTheShortcutAndTabMovesFocus},
        {"each Enter is one undo entry", eachEnterIsOneUndoEntry},
        {"Enter during composition does not break the line", enterDuringCompositionDoesNotBreakTheLine},
        {"input method replacement is one undo entry", inputMethodReplacementIsOneUndoEntry},
        {"single-line Enter still reaches its shortcut", singleLineEnterStillReachesItsShortcut},
        {"read-only multi-line field refuses Enter", readOnlyMultilineFieldRefusesEnter},
        {"a long line wraps and the height stays fixed", aLongLineWrapsAtTheEdgeAndTheHeightStaysFixed},
        {"a wide styled stop after a preferred break still fits", aWideStyledStopAfterAPreferredBreakStillFits},
        {"public wrapping marks paragraphs and preserves empty lines", publicWrappingMarksParagraphsAndPreservesEmptyLines},
        {"hard breaks make lines; wrapping prefers spaces", hardBreaksMakeLinesAndWrappingPrefersSpaces},
        {"the scrollbar width is always reserved", theScrollbarWidthIsAlwaysReserved},
        {"pointer positions map to line and offset", pointerPositionsMapToLineAndOffset},
        {"a wrap-boundary caret belongs to the next line", aCaretOnAWrapBoundaryBelongsToTheNextLineUnlessUpstream},
        {"vertical keys move by display line and keep the column", verticalKeysMoveByDisplayLineAndKeepTheColumn},
        {"vertical keys follow wrapped lines", verticalKeysFollowWrappedLines},
        {"Home/End use the line, Ctrl the whole text", homeAndEndUseTheCurrentLineAndCtrlUsesTheWholeText},
        {"Shift with vertical keys extends the selection", shiftWithVerticalKeysExtendsTheSelection},
        {"page keys move by the visible line count", pageKeysMoveByTheVisibleLineCount},
        {"typing to line twenty scrolls to the caret", typingToLineTwentyScrollsTheFieldToTheCaret},
        {"scrolling alone neither moves the caret nor snaps back", scrollingAloneDoesNotMoveTheCaretOrSnapBack},
        {"the wheel scrolls the field, then the page", theWheelScrollsTheFieldThenThePage},
        {"a dialog scrolls its own content and blocks the page", aDialogScrollsItsOwnContentAndBlocksThePage},
        {"the scroll position is one value for code and semantics", theScrollPositionIsOneValueForCodeAndSemantics},
        {"the first visible line survives a text size change", theFirstVisibleLineSurvivesATextSizeChange},
        {"the visible line count can change at run time", theVisibleLineCountCanChangeAtRunTime},
        {"dragging selects across lines and scrolls at the edges", draggingSelectsAcrossLinesAndScrollsAtTheEdges},
        {"a click past a wrapped line end stays on that line", aClickPastAWrappedLineEndStaysOnThatLine},
        {"composition wraps and the candidate follows the caret line", compositionWrapsAndTheCandidatePositionFollowsTheCaretLine},
    };
    return test::run("multiline", cases);
}
