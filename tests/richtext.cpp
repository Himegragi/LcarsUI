#include "core_internal.hpp"
// #81 rich text through the public core seams: the value type, typed editing, shortcuts, clipboard,
// data connections and the semantic layer. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "component_type.hpp"
#include "fake_measure.hpp"
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"
#include "isoduo/semantics.hpp"
#include <cmath>
using namespace isoduo;
using test::check;

namespace {
StyleSpan bold(unsigned start, unsigned length) { return {.start = start, .length = length, .bold = true}; }
StyleSpan italic(unsigned start, unsigned length) { return {.start = start, .length = length, .italic = true}; }
StyleSpan underline(unsigned start, unsigned length) { return {.start = start, .length = length, .underline = true}; }
StyleSpan colored(unsigned start, unsigned length, std::string color) { return {.start = start, .length = length, .color = std::move(color)}; }
using Spans = std::vector<StyleSpan>;

void normalizationMergesSortsAndDropsEmptySpans() {
    auto merged = normalizeRichText({u"abcdef", {bold(0, 2), bold(2, 2)}});
    check(bool(merged) && merged->spans == Spans{bold(0, 4)}, "Two adjacent bold spans become one");
    auto sorted = normalizeRichText({u"abcdef", {italic(4, 2), bold(0, 2)}});
    check(bool(sorted) && sorted->spans == Spans{bold(0, 2), italic(4, 2)}, "Spans are sorted by start");
    auto apart = normalizeRichText({u"abcdef", {bold(0, 2), bold(3, 2)}});
    check(bool(apart) && apart->spans == Spans{bold(0, 2), bold(3, 2)}, "A gap keeps two spans");
    auto different = normalizeRichText({u"abcdef", {bold(0, 2), italic(2, 2)}});
    check(bool(different) && different->spans == Spans{bold(0, 2), italic(2, 2)}, "Adjacent spans of different style stay apart");
    auto empty = normalizeRichText({u"abc", {bold(1, 0), bold(0, 0)}});
    check(bool(empty) && empty->spans.empty(), "Zero-length spans are removed");
    auto plain = normalizeRichText({u"abc", {{.start = 0, .length = 3}}});
    check(bool(plain) && plain->spans.empty(), "A span of plain style says nothing and is removed");
    auto colors = normalizeRichText({u"abcd", {colored(0, 2, "red"), colored(2, 2, "red")}});
    check(bool(colors) && colors->spans == Spans{colored(0, 4, "red")}, "Equal color names merge");
    auto names = normalizeRichText({u"abcd", {colored(0, 2, "red"), colored(2, 2, "green")}});
    check(bool(names) && names->spans.size() == 2, "Different color names do not merge");
    auto touching = normalizeRichText({u"abcd", {{.start = 0, .length = 2, .bold = true, .underline = true}, {.start = 2, .length = 2, .bold = true, .underline = true}}});
    check(bool(touching) && touching->spans.size() == 1 && touching->spans[0].length == 4, "Combined styles merge as one style");
    check(normalizeRichText({u"abcdef", {bold(0, 2), bold(2, 2)}})->spans == normalizeRichText({u"abcdef", {bold(0, 4)}})->spans,
          "The same content always reads back as the same value");
}

void normalizationRejectsOverlapAndOutOfRangeSpans() {
    check(normalizeRichText({u"abcdef", {bold(0, 3), italic(2, 2)}}).status == Status::invalidValue, "Overlapping spans are invalid");
    check(normalizeRichText({u"abcdef", {bold(0, 3), bold(2, 2)}}).status == Status::invalidValue, "Overlap is invalid even when the styles are equal");
    check(normalizeRichText({u"abc", {bold(1, 3)}}).status == Status::invalidValue, "A span past the end is invalid");
    check(normalizeRichText({u"abc", {bold(4, 0)}}).status == Status::invalidValue, "Even an empty span must start inside the text");
    check(bool(normalizeRichText({u"abc", {bold(3, 0)}})), "An empty span at the end is inside the text");
    check(normalizeRichText({u"a\U0001F600b", {bold(2, 1)}}).status == Status::invalidValue, "A span may not start inside a surrogate pair");
    check(normalizeRichText({u"a\U0001F600b", {bold(0, 2)}}).status == Status::invalidValue, "A span may not end inside a surrogate pair");
    check(bool(normalizeRichText({u"a\U0001F600b", {bold(1, 2)}})), "A span may cover a whole pair");
    check(normalizeRichText({u"a\rb", {}}).status == Status::invalidValue, "The text follows the multi-line rule (no CR)");
    check(normalizeRichText({u"abc", {}}, 2).status == Status::invalidValue, "The text respects the length limit");
    check(bool(normalizeRichText({u"a\nb", {bold(0, 3)}})), "A span may cover a line feed");
}

// A rich field with an explicit write timing, as an application would have it.
struct RichModel {
    RichText value;
    std::function<void()> notify;
    int writes = 0;
    RichText lastWritten, lastOrigin;
};
ComponentConfig richControl(std::string id, std::u16string value = {}, unsigned lines = 3) {
    return {.id = std::move(id), .value = std::move(value), .multiline = true, .visibleLines = lines, .rich = true};
}
TextFieldHandle connectRich(Ui& ui, ComponentConfig control, std::shared_ptr<RichModel> model = {}, bool immediate = false) {
    if (!model) model = std::make_shared<RichModel>();
    model->value.text = control.value;
    auto query = ui.addTextField(std::move(control));
    check(bool(query), "Create the rich field");
    auto field = *query;
    check(bool(field.connectRichSource({[model] { return model->value; }, [model](std::function<void()> cb) { model->notify = cb; return [] {}; }})), "Connect a rich source");
    auto write = [model](const Change<TextFieldHandle, RichText>& change) { ++model->writes; model->lastWritten = change.value; model->lastOrigin = change.origin; return Result{}; };
    check(bool(immediate ? field.connectRichImmediate(write) : field.connectRichConfirmed(write)), "Connect a rich write");
    return field;
}
std::u16string valueOf(Ui& ui, std::string_view id) { return detail::access(ui).text(id); }

void aRichFieldIsAMultiLineOptionFixedAtCreation() {
    Ui ui;
    check(bool(ui.addTextField(richControl("rich"))), "A multi-line field can be rich");
    check(ui.addTextField({.id = "single", .rich = true}).status == Status::invalidValue, "A single-line field cannot be rich");
}

void richValuesAreReadAndWrittenInNormalForm() {
    Ui ui;
    auto field = connectRich(ui, richControl("note"));
    check(bool(field.setRichValue({u"abcd", {bold(2, 1), bold(0, 2)}})), "Write a rich value");
    auto value = field.richValue();
    check(bool(value) && value->text == u"abcd" && value->spans == Spans{bold(0, 3)}, "The value reads back normalized");
    check(field.draft().value.value() == u"abcd" && valueOf(ui, "note") == u"abcd", "The plain text is the draft and the semantic value");
    check(field.setRichValue({u"xyz", {bold(0, 2), italic(1, 2)}}).status == Status::invalidValue && field.richValue()->text == u"abcd",
          "Overlapping spans are refused and the field is unchanged");
    check(field.setRichValue({u"xyz", {bold(2, 2)}}).status == Status::invalidValue && field.richValue()->spans == Spans{bold(0, 3)},
          "Spans past the text are refused and the field is unchanged");
    check(bool(field.setValue(u"plain")) && field.richValue()->text == u"plain" && field.richValue()->spans.empty(),
          "set_value on a rich field replaces the text and leaves it unstyled");
    Ui other;
    auto plain = *other.addTextField({.id = "p", .multiline = true});
    check(plain.richValue().status == Status::notSupported && plain.setRichValue({u"x", {}}).status == Status::notSupported &&
          plain.setStyle(0, 0, {.bold = true}).status == Status::notSupported && plain.typingStyle().status == Status::notSupported,
          "A field that is not rich answers not_supported");
    check(field.connectSource({[] { return std::u16string(); }, [](std::function<void()>) { return [] {}; }}).status == Status::notSupported,
          "A rich field refuses a plain source");
    check(plain.connectRichSource({[] { return RichText{}; }, [](std::function<void()>) { return [] {}; }}).status == Status::notSupported,
          "A plain field refuses a rich source");
}

void stylesApplyToARangeAndAreOneUndoEntry() {
    Ui ui;
    auto model = std::make_shared<RichModel>();
    auto field = connectRich(ui, richControl("note", u"重要通知內容"), model, true);
    auto spans = [&] { return field.richValue()->spans; };
    check(bool(field.setStyle(0, 2, {.bold = true})) && spans() == Spans{bold(0, 2)}, "Bold the first two characters");
    check(model->writes == 1 && model->lastWritten.spans == Spans{bold(0, 2)} && model->lastOrigin.spans.empty(), "An immediate write carries the styled value and the origin");
    check(bool(field.setStyle(1, 4, {.italic = true})) && spans() == Spans{bold(0, 1), {.start = 1, .length = 1, .bold = true, .italic = true}, italic(2, 2)},
          "Italic over a bold edge combines the styles");
    check(bool(field.setStyle(0, 6, {.bold = false, .italic = false})) && spans().empty(), "Removing bold and italic from the whole text leaves no spans");
    check(bool(field.setStyle(2, 4, {.color = "red", .underline = true})) && spans() == Spans{{.start = 2, .length = 2, .underline = true, .color = "red"}},
          "Color and underline set together");
    check(bool(field.setStyle(2, 3, {.color = ""})) && spans() == Spans{underline(2, 1), {.start = 3, .length = 1, .underline = true, .color = "red"}},
          "An empty color name returns text to the normal color");
    check(field.setStyle(3, 2, {.bold = true}).status == Status::invalidValue && field.setStyle(0, 7, {.bold = true}).status == Status::invalidValue,
          "A reversed or out-of-range span is invalid_value");
    check(bool(field.setStyle(0, 6, {.bold = true})), "Bold everything");
    auto before = spans();
    check(bool(field.undo()) && spans() != before && spans().size() == 2, "One undo reverts one style application");
    check(bool(field.redo()) && spans() == before, "Redo restores it");
    check(field.richValue()->text == u"重要通知內容", "Styling never changes the text");
}

void readOnlyAndUnconnectedRichFieldsRefuseStyleEdits() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abc"));
    field.readOnly(true);
    check(field.setStyle(0, 1, {.bold = true}).status == Status::readOnly && field.setRichValue({u"x", {}}).status == Status::readOnly,
          "A read-only field refuses style edits like any edit");
    auto bare = *ui.addTextField(richControl("bare", u"abc"));
    check(bare.setStyle(0, 1, {.bold = true}).status == Status::invalidValue, "A field without a write timing refuses edits");
}

void maxLengthCountsPlainCharactersOnly() {
    Ui ui;
    auto control = richControl("note", u"abcd");
    control.maxLength = 4;
    auto field = connectRich(ui, control);
    check(bool(field.setStyle(0, 4, {.bold = true, .color = "a-very-long-color-name"})), "Styling a field at its limit is allowed");
    check(bool(field.setRichValue({u"wxyz", {bold(0, 4)}})), "A rich value of exactly the limit is allowed");
    check(field.setRichValue({u"vwxyz", {bold(0, 1)}}).status == Status::invalidValue, "One more character is refused, however it is styled");
}

// ---- Keyboard ------------------------------------------------------------------------------
KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
KeyPress press(Key key, bool shift = false) { return {.key = key, .shift = shift}; }
Spans spansOf(TextFieldHandle& field) { return field.richValue()->spans; }

void ctrlBTogglesTheSelectionAndAllThreeShortcutsWork() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"重要通知內容"));
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 0, 2);
    check(bool(host.keyDown(ctrl(u'B'))) && spansOf(field) == Spans{bold(0, 2)}, "Ctrl+B bolds the selection");
    check(bool(host.keyDown(ctrl(u'B'))) && spansOf(field).empty(), "A second Ctrl+B removes it");
    host.select("note", 0, 2);
    host.keyDown(ctrl(u'I')); host.keyDown(ctrl(u'U'));
    check(spansOf(field) == Spans{{.start = 0, .length = 2, .italic = true, .underline = true}}, "Ctrl+I and Ctrl+U apply italic and underline");
    check(field.richValue()->text == u"重要通知內容" && valueOf(ui, "note") == u"重要通知內容", "Styling never inserts characters");
}

void aPartlyStyledSelectionBecomesFullyStyledFirst() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abcdef"));
    auto& host = detail::access(ui);
    field.setStyle(0, 3, {.bold = true});
    host.focus("note");
    host.select("note", 1, 5);
    host.keyDown(ctrl(u'B'));
    check(spansOf(field) == Spans{bold(0, 5)}, "Half bold, half plain: Ctrl+B makes all of it bold");
    host.keyDown(ctrl(u'B'));
    check(spansOf(field) == Spans{bold(0, 1)}, "When all of the selection is bold, Ctrl+B removes bold from it");
    field.setValue(u"a\nb");
    field.setStyle(0, 1, {.bold = true});
    field.setStyle(2, 3, {.bold = true});
    host.select("note", 0, 3);
    host.keyDown(ctrl(u'B'));
    check(spansOf(field).empty(), "A line feed between bold lines does not count as unstyled, so Ctrl+B removes bold from both");
}

void withoutASelectionCtrlBChangesTheNextTypedTextOnly() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 2, 2);
    host.keyDown(ctrl(u'B'));
    check(spansOf(field).empty() && field.typingStyle().value->bold, "Ctrl+B at a caret styles nothing yet but sets the typing style");
    host.textInput(u"c");
    check(spansOf(field) == Spans{bold(2, 1)}, "The next typed character is bold");
    host.textInput(u"d");
    check(spansOf(field) == Spans{bold(2, 2)}, "The following ones follow the previous character, so they stay bold");
    host.select("note", 0, 0);
    check(!field.typingStyle().value->bold, "At the start of a plain paragraph the typing style is plain");
    host.keyDown(ctrl(u'B'));
    host.select("note", 1, 1);
    host.textInput(u"x");
    check(spansOf(field) == Spans{bold(3, 2)}, "Moving the caret cancels the temporary typing style");
    host.select("note", 1, 1);
    host.keyDown(ctrl(u'B')); host.keyDown(ctrl(u'B'));
    host.textInput(u"y");
    check(spansOf(field) == Spans{bold(4, 2)}, "Pressing Ctrl+B twice leaves the style as it was");
}

void typedTextFollowsThePreviousCharacterOrTheNextAtAParagraphStart() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    field.setStyle(0, 1, {.bold = true});
    host.select("note", 1, 1); host.textInput(u"x");
    check(spansOf(field) == Spans{bold(0, 2)}, "After a bold character, typing is bold");
    host.select("note", 3, 3); host.textInput(u"y");
    check(spansOf(field) == Spans{bold(0, 2)}, "After a plain character, typing is plain");
    field.setRichValue({u"ab\ncd", {bold(0, 2), italic(3, 2)}});
    host.select("note", 0, 0); host.textInput(u"1");
    check(spansOf(field) == Spans{bold(0, 3), italic(4, 2)}, "At the start of a paragraph, typing follows the next character");
    field.setRichValue({u"ab\ncd", {bold(0, 2), italic(3, 2)}});
    host.select("note", 3, 3); host.textInput(u"2");
    check(spansOf(field) == Spans{bold(0, 2), italic(3, 3)}, "The second paragraph's start follows its own first character");
    field.setRichValue({u"ab\n\ncd", {bold(0, 2)}});
    host.select("note", 3, 3); host.textInput(u"3");
    check(spansOf(field) == Spans{bold(0, 2)}, "An empty paragraph has no neighbor to follow, so it types plain");
    field.setRichValue({u"abcd", {bold(0, 4)}});
    host.select("note", 1, 3); host.textInput(u"z");
    check(spansOf(field) == Spans{bold(0, 3)}, "Replacing a selection follows the character before it");
    field.setRichValue({u"abcd", {bold(1, 3)}});
    host.select("note", 0, 2); host.textInput(u"z");
    check(spansOf(field) == Spans{bold(0, 3)}, "Replacing from the paragraph start follows the first character after the selection");
}

void eachStyledInsertionIsOneUndoEntry() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 2, 2);
    host.keyDown(ctrl(u'B'));
    host.textInput(u"c");
    check(spansOf(field) == Spans{bold(2, 1)}, "A bold character");
    host.keyDown(ctrl(u'Z'));
    check(field.richValue()->text == u"ab" && spansOf(field).empty(), "One undo removes the character and its style together");
    host.keyDown(ctrl(u'Y'));
    check(field.richValue()->text == u"abc" && spansOf(field) == Spans{bold(2, 1)}, "Redo restores both");
    host.select("note", 0, 1);
    host.keyDown(ctrl(u'I'));
    host.keyDown(ctrl(u'Z'));
    check(spansOf(field) == Spans{bold(2, 1)} && field.richValue()->text == u"abc", "One Ctrl+I is one undo entry");
}

void theFieldTakesCtrlBBeforeAnApplicationShortcut() {
    Ui ui;
    auto rich = connectRich(ui, richControl("note", u"abc"));
    auto plain = *ui.addTextField({.id = "plain", .value = u"abc", .multiline = true});
    auto button = *ui.addButton({.id = "bold", .label = u"Bold"});
    int pressed = 0;
    button.bind([&] { ++pressed; return Result{}; });
    check(bool(ui.addShortcut({.key = Key::other, .letter = u'B', .ctrl = true}, button)), "An application shortcut on Ctrl+B");
    auto& host = detail::access(ui);
    host.focus("note"); host.select("note", 0, 3);
    host.keyDown(ctrl(u'B'));
    check(pressed == 0 && spansOf(rich) == Spans{bold(0, 3)}, "With the rich field focused, the field takes Ctrl+B");
    host.focus("plain");
    host.keyDown(ctrl(u'B'));
    check(pressed == 1, "With any other field focused, the application shortcut fires");
    host.focus("bold");
    host.keyDown(ctrl(u'B'));
    check(pressed == 2, "With the rich field not focused, the application shortcut fires");
    rich.readOnly(true);
    host.focus("note");
    check(host.keyDown(ctrl(u'B')).status == Status::readOnly && pressed == 2, "A read-only rich field refuses the style but still keeps the key");
}

// ---- Colors --------------------------------------------------------------------------------
void aColorNameResolvesPerThemeAndUnknownNamesKeepTheirName() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abc"));
    auto& host = detail::access(ui);
    check(field.setColors({{"red", {200, 0, 0}, {255, 120, 120}}, {"", {1, 1, 1}, {2, 2, 2}}}).status == Status::invalidValue &&
          field.setColors({{"red", {200, 0, 0}, {255, 120, 120}}, {"red", {1, 1, 1}, {2, 2, 2}}}).status == Status::invalidValue,
          "An empty or repeated color name is invalid_value");
    check(field.colors()->empty(), "A refused list changes nothing");
    check(bool(field.setColors({{"red", {200, 0, 0}, {255, 120, 120}}, {"green", {0, 120, 0}, {120, 255, 120}}})), "Set the color list");
    check(field.colors()->size() == 2 && (*field.colors())[1].name == "green", "The list reads back in order");
    field.setStyle(0, 2, {.color = "red"});
    field.setStyle(2, 3, {.color = "teal"});
    check(host.namedColor("note", "red") == Color{200, 0, 0} && host.namedColor("note", "green") == Color{0, 120, 0}, "Light theme uses the light values");
    check(!host.namedColor("note", "").has_value(), "No name means the normal text color");
    ui.setPresentation({.variant = ThemeVariant::dark()});
    check(host.namedColor("note", "red") == Color{255, 120, 120}, "Switching theme recolors by name: dark values");
    check(!host.namedColor("note", "teal").has_value(), "A name that is not in the list falls back to the normal color");
    check(field.richValue()->spans == Spans{colored(0, 2, "red"), colored(2, 1, "teal")}, "The unknown name stays in the value");
    int repaints = 0;
    host.onInvalidate([&] { ++repaints; });
    field.setColors({{"teal", {0, 128, 128}, {64, 224, 208}}});
    check(repaints == 1 && host.namedColor("note", "teal") == Color{64, 224, 208} && !host.namedColor("note", "red").has_value(),
          "Replacing the list repaints at once and applies to existing text");
    Ui other;
    auto plain = *other.addTextField({.id = "p", .multiline = true});
    check(plain.setColors({}).status == Status::notSupported && plain.colors().status == Status::notSupported, "A field that is not rich has no color list");
}

// ---- Data connections ---------------------------------------------------------------------
void externalSourceChangesAndConflictsCoverStyles() {
    Ui ui;
    auto model = std::make_shared<RichModel>();
    auto field = connectRich(ui, richControl("note", u"abc"), model);
    model->value = {u"xyz", {bold(0, 1)}}; model->notify();
    check(field.richValue()->text == u"xyz" && spansOf(field) == Spans{bold(0, 1)}, "An external change brings its text and styles");
    check(!field.sourceConflict().value.value(), "A clean field follows the source without a conflict");
    field.setStyle(1, 3, {.italic = true});
    check(!field.sourceConflict().value.value(), "A style edit is a local edit, not a conflict");
    model->value = {u"xyz", {bold(0, 2)}}; model->notify();
    check(spansOf(field) == Spans{bold(0, 1), italic(1, 2)} && field.sourceConflict().value.value(),
          "A source that changes only styles while the field is edited is a conflict");
    check(bool(field.useLatest()) && spansOf(field) == Spans{bold(0, 2)} && !field.sourceConflict().value.value(),
          "use_latest adopts the source's styles too");
    field.setStyle(0, 1, {.bold = false});
    check(bool(field.resetToSource()) && spansOf(field) == Spans{bold(0, 2)}, "reset_to_source restores text and styles");
    model->value = {u"xyz", {bold(0, 5)}}; model->notify();
    check(spansOf(field) == Spans{bold(0, 2)}, "A source value that is not valid (span past the text) is ignored");
}

void confirmedAndImmediateWritesCarryTheRichValue() {
    Ui ui;
    auto model = std::make_shared<RichModel>();
    auto field = connectRich(ui, richControl("note", u"abc"), model);
    field.setStyle(0, 1, {.bold = true});
    check(model->writes == 0, "A confirmed field writes nothing until confirmed");
    check(bool(field.confirm()) && model->writes == 1 && model->lastWritten == RichText{u"abc", {bold(0, 1)}} && model->lastOrigin == RichText{u"abc", {}},
          "Confirm writes the value with its styles and the origin before the edit");
    field.setStyle(1, 2, {.underline = true});
    field.confirm();
    check(model->lastOrigin == RichText{u"abc", {bold(0, 1)}}, "The next write's origin is the value last written, styles included");
    auto& tree = ui.semantics();
    const auto node = tree.get({"note"});  // one snapshot: iterators of two different temporaries must not be compared
    check(node && std::find(node->verbs.begin(), node->verbs.end(), Verb::confirm) != node->verbs.end(), "The semantic layer offers confirm");
}

void aConfirmationGroupTakesARichFieldInTheMixedForm() {
    Ui ui;
    auto noteModel = std::make_shared<RichModel>();
    auto nameValue = std::make_shared<std::u16string>(u"n");
    auto note = *ui.addTextField(richControl("note", u"abc"));
    auto name = *ui.addTextField({.id = "name", .value = u"n"});
    auto button = *ui.addButton({.id = "ok", .label = u"OK"});
    noteModel->value.text = u"abc";
    note.connectRichSource({[noteModel] { return noteModel->value; }, [](std::function<void()>) { return [] {}; }});
    name.connectSource({[nameValue] { return *nameValue; }, [](std::function<void()>) { return [] {}; }});
    RichText got, origin; std::u16string gotName; int calls = 0;
    check(bool(ui.connectConfirmation(button, {name, note}, [&](const std::vector<ConfirmFieldChange>& changes) {
        ++calls;
        gotName = std::get<Change<TextFieldHandle>>(changes[0]).value;
        const auto& rich = std::get<Change<TextFieldHandle, RichText>>(changes[1]);
        got = rich.value; origin = rich.origin;
        return ConfirmationResult{};
    })), "A rich field joins a mixed confirmation group");
    auto& host = detail::access(ui);
    host.focus("note"); host.select("note", 0, 2);
    host.keyDown(ctrl(u'B'));
    check(bool(host.invoke("ok")) && calls == 1 && got == RichText{u"abc", {bold(0, 2)}} && origin == RichText{u"abc", {}} && gotName == u"n",
          "The group's submit sees the rich value and origin beside the plain field");
    host.select("note", 2, 3); host.keyDown(ctrl(u'I'));
    host.invoke("ok");
    check(origin == RichText{u"abc", {bold(0, 2)}} && got == RichText{u"abc", {bold(0, 2), italic(2, 1)}}, "After a successful submit the origin includes the styles that were written");
}

// ---- Clipboard -----------------------------------------------------------------------------
// Plain text and the framework's private payload, as the Windows clipboard keeps two formats: a plain
// write replaces everything, so the payload of an earlier rich copy is gone.
struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    std::optional<std::string> payload;
    bool write(std::u16string_view text) override { content = std::u16string(text); payload.reset(); return true; }
    bool writeRich(std::u16string_view text, std::string_view rich) override { content = std::u16string(text); payload = std::string(rich); return true; }
    Query<std::u16string> read() override {
        if (!content) return {Status::notSupported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, content};
    }
    Query<std::string> readRich() override {
        if (!payload) return {Status::notSupported, u"Clipboard has no rich text", std::nullopt};
        return {Status::ok, {}, payload};
    }
};
struct PlainOnlyClipboard final : Clipboard {  // a host that never learned the private format
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override { return {Status::ok, {}, content}; }
};


TextFieldHandle connectPlain(Ui& ui, ComponentConfig control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto field = *ui.addTextField(std::move(control));
    field.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    return field;
}

void copyAndPasteKeepStylesBetweenRichFields() {
    Ui ui;
    auto source = connectRich(ui, richControl("source", u"abc\ndef"));
    auto target = connectRich(ui, richControl("target", u"XY"));
    auto& host = detail::access(ui);
    MemoryClipboard clipboard;
    host.setClipboard(&clipboard);
    source.setStyle(1, 6, {.bold = true});
    source.setStyle(4, 7, {.color = "teal"});
    host.focus("source"); host.select("source", 1, 6);
    host.keyDown(ctrl(u'C'));
    check(clipboard.content == u"bc\r\nde" && clipboard.payload.has_value(), "Copy writes CRLF plain text and the private payload together");
    check(source.richValue()->text == u"abc\ndef", "Copy does not change the field");
    host.focus("target"); host.select("target", 1, 1);
    host.keyDown(ctrl(u'V'));
    check(target.richValue()->text == u"Xbc\ndeY", "Paste inserts the text with its line break as LF");
    check(spansOf(target) == Spans{bold(1, 3), {.start = 4, .length = 2, .bold = true, .color = "teal"}}, "Paste keeps the copied styles, color names included, relative to the insertion");
    host.keyDown(ctrl(u'Z'));
    check(target.richValue()->text == u"XY" && spansOf(target).empty(), "One paste is one undo entry");
}

void aColorNameMissingFromTheTargetListSurvivesAPaste() {
    Ui ui;
    auto source = connectRich(ui, richControl("source", u"abc"));
    auto target = connectRich(ui, richControl("target"));
    source.setColors({{"red", {200, 0, 0}, {255, 120, 120}}});
    source.setStyle(0, 3, {.color = "red"});
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.setClipboard(&clipboard);
    host.focus("source"); host.select("source", 0, 3); host.keyDown(ctrl(u'C'));
    host.focus("target"); host.keyDown(ctrl(u'V'));
    check(spansOf(target) == Spans{colored(0, 3, "red")} && !host.namedColor("target", "red").has_value(),
          "The name is kept in the value; the target (which has no such color) draws it in the normal color");
}

void externalPlainTextTakesTheTypingStyle() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"ab"));
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.setClipboard(&clipboard);
    host.focus("note");
    field.setStyle(0, 1, {.bold = true});
    clipboard.write(u"xyz");
    host.select("note", 1, 1);
    host.keyDown(ctrl(u'V'));
    check(field.richValue()->text == u"axyzb" && spansOf(field) == Spans{bold(0, 4)}, "Plain text pasted after a bold character is bold");
    host.select("note", 5, 5);
    host.keyDown(ctrl(u'V'));
    check(spansOf(field) == Spans{bold(0, 4)}, "Plain text pasted after a plain character is plain");
    host.select("note", 5, 5);
    host.keyDown(ctrl(u'B'));
    host.keyDown(ctrl(u'V'));
    check(spansOf(field) == Spans{bold(0, 4), bold(5, 3)}, "A pending Ctrl+B styles the pasted text");
    clipboard.content = u"Q\r\nR"; clipboard.payload = std::string("stale payload from another time");
    host.select("note", 0, 0);
    host.keyDown(ctrl(u'V'));
    check(field.richValue()->text.substr(0, 4) == u"Q\nRa" && spansOf(field)[0] == bold(0, 7),
          "A private payload that does not belong to the plain text is ignored; the text is pasted with the typing style (CRLF becomes LF)");
}

void otherFieldsAndHostsGetPlainTextOnly() {
    Ui ui;
    auto source = connectRich(ui, richControl("source", u"a\nb"));
    auto multi = connectPlain(ui, {.id = "multi", .multiline = true, .visibleLines = 2});
    auto single = connectPlain(ui, {.id = "single"});
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.setClipboard(&clipboard);
    source.setStyle(0, 3, {.bold = true});
    host.focus("source"); host.select("source", 0, 3); host.keyDown(ctrl(u'C'));
    host.focus("multi"); host.keyDown(ctrl(u'V'));
    check(multi.draft().value.value() == u"a\nb", "A plain multi-line field takes the text, with its line feed");
    host.focus("single"); host.keyDown(ctrl(u'V'));
    check(single.draft().value.value() == u"a b", "A single-line field takes the text by its own rule (breaks become spaces)");
    check(!multi.richValue() && multi.richValue().status == Status::notSupported, "The plain field holds no styles");
    PlainOnlyClipboard plainHost;
    host.setClipboard(&plainHost);
    host.focus("source"); host.select("source", 0, 1);
    host.keyDown(ctrl(u'C'));
    check(plainHost.content == u"a", "A host that only knows plain text receives plain text");
    host.focus("single"); host.select("single", 0, 3);
    host.keyDown(ctrl(u'V'));
    check(single.draft().value.value() == u"a", "A plain-only clipboard still pastes into a single-line field as before");
}

void cutRemovesStyledTextAndUndoRestoresIt() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abcd"));
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.setClipboard(&clipboard);
    field.setStyle(0, 2, {.bold = true});
    host.focus("note"); host.select("note", 0, 2);
    host.keyDown(ctrl(u'X'));
    check(field.richValue()->text == u"cd" && spansOf(field).empty() && clipboard.content == u"ab" && clipboard.payload.has_value(), "Cut removes the text and copies it with its styles");
    host.keyDown(ctrl(u'Z'));
    check(field.richValue()->text == u"abcd" && spansOf(field) == Spans{bold(0, 2)}, "Undo brings the styled text back");
}

void aRichPasteOverTheLimitIsRefusedWhole() {
    Ui ui;
    auto source = connectRich(ui, richControl("source", u"abcdef"));
    auto limited = richControl("target", u"XY");
    limited.maxLength = 4;
    auto target = connectRich(ui, limited);
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.setClipboard(&clipboard);
    source.setStyle(0, 6, {.bold = true});
    host.focus("source"); host.select("source", 0, 6); host.keyDown(ctrl(u'C'));
    host.focus("target"); host.select("target", 2, 2);
    check(host.keyDown(ctrl(u'V')).status == Status::invalidValue && target.richValue()->text == u"XY" && spansOf(target).empty(),
          "A paste past max_length leaves text and styles untouched");
}

// ---- Semantic layer ------------------------------------------------------------------------
void theSemanticLayerReadsAndWritesStylesLikeTheTypedCalls() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abcdef"));
    auto& tree = ui.semantics();
    auto node = tree.get({"note"});
    check(bool(node) && node->rich && node->styles.empty() && node->typingStyle.has_value() && node->typingStyle->plain(), "A rich node reports its styles and typing style");
    auto has = [&](Verb verb) { auto n = tree.get({"note"}); return std::find(n->verbs.begin(), n->verbs.end(), verb) != n->verbs.end(); };
    check(has(Verb::setStyle) && has(Verb::setTypingStyle) && has(Verb::setValue), "The style verbs are offered to an editable rich field");
    check(bool(tree.perform({"note"}, Verb::setStyle, StyleEdit{1, 3, {.bold = true}})) && tree.get({"note"})->styles == Spans{bold(1, 2)} && spansOf(field) == Spans{bold(1, 2)},
          "set_style applies a range, as TextFieldHandle::set_style does");
    check(tree.perform({"note"}, Verb::setStyle, StyleEdit{3, 1, {.bold = true}}).status == field.setStyle(3, 1, {.bold = true}).status &&
          tree.perform({"note"}, Verb::setStyle, StyleEdit{3, 1, {.bold = true}}).status == Status::invalidValue, "An invalid range gives the typed call's status");
    check(tree.perform({"note"}, Verb::setStyle, std::u16string(u"bold")).status == Status::invalidValue, "A wrong argument is invalid_value");
    check(bool(tree.perform({"note"}, Verb::setValue, RichText{u"xy", {italic(0, 1)}})) && tree.get({"note"})->value == u"xy" && tree.get({"note"})->styles == Spans{italic(0, 1)},
          "set_value with a rich value replaces text and styles");
    check(tree.perform({"note"}, Verb::setValue, RichText{u"xy", {italic(0, 1), bold(0, 2)}}).status == Status::invalidValue && tree.get({"note"})->styles == Spans{italic(0, 1)},
          "An overlapping value is invalid_value and changes nothing");
    check(bool(tree.perform({"note"}, Verb::setValue, std::u16string(u"plain"))) && tree.get({"note"})->styles.empty(), "A plain set_value leaves the text unstyled");
    check(bool(tree.perform({"note"}, Verb::setTypingStyle, TextStyle{.bold = true})) && tree.get({"note"})->typingStyle->bold && field.typingStyle().value->bold,
          "The typing style set through the tree is the one the typed call reads");
    auto& host = detail::access(ui);
    host.focus("note"); host.select("note", 5, 5);
    host.select("note", 5, 5);
    check(!tree.get({"note"})->typingStyle->bold, "Moving the caret ends the temporary typing style");
    field.readOnly(true);
    check(tree.perform({"note"}, Verb::setStyle, StyleEdit{0, 1, {.bold = true}}).status == field.setStyle(0, 1, {.bold = true}).status, "A read-only field gives the same status through both doors");
    check(!has(Verb::setStyle) && !has(Verb::setTypingStyle), "A read-only field does not offer the style verbs");
    Ui other;
    auto plain = connectPlain(other, {.id = "p", .value = u"abc", .multiline = true});
    check(!other.semantics().get({"p"})->rich && other.semantics().perform({"p"}, Verb::setStyle, StyleEdit{0, 1, {.bold = true}}).status == Status::notSupported,
          "A plain field is not rich and refuses the style verbs");
}

void styleChangesRaiseAPropertyEventAndColorsResolveForTheTheme() {
    Ui ui;
    ui.bindAppearance(&webAppearance());
    auto field = connectRich(ui, richControl("note", u"abc"));
    field.setColors({{"red", {200, 0, 0}, {255, 120, 120}}});
    auto& tree = ui.semantics();
    std::vector<std::string> properties;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::propertyChanged && e.ref.id == "note") properties.push_back(e.property); });
    field.setStyle(0, 1, {.bold = true});
    tree.dispatchEvents();
    check(std::find(properties.begin(), properties.end(), "styles") != properties.end() && std::find(properties.begin(), properties.end(), "value") == properties.end(),
          "A style change raises a 'styles' property event, and no 'value' event (the text did not change)");
    check(tree.textColor({"note"}, "red").value == std::optional<Color>(Color{200, 0, 0}), "A named color resolves to its light value");
    check(tree.textColor({"note"}, "").value == std::optional<Color>(webAppearance().palette().text), "No name resolves to the theme's normal text color");
    check(tree.textColor({"note"}, "nope").value == std::optional<Color>(webAppearance().palette().text), "An unknown name resolves to the normal text color too");
    ui.setPresentation({.variant = ThemeVariant::dark()});
    check(tree.textColor({"note"}, "red").value == std::optional<Color>(Color{255, 120, 120}), "After a theme change it resolves to the dark value");
    check(tree.textColor({"note"}, "").value == std::optional<Color>(webAppearance().palette(ThemeVariant::dark()).text), "...and the normal color to the dark text color");
    Ui other;
    check(connectPlain(other, {.id = "p", .multiline = true}).describe().value->rich == false && other.semantics().textColor({"p"}, "").status == Status::notSupported,
          "Only rich fields resolve colors");
}

// ---- Layout and painting -------------------------------------------------------------------
// FakeMeasure at 16 px: an ASCII character is 8 px, a bold one 10 px, a line is 24 px tall. A 200 px
// field has a 176 px editor, a 14 px scrollbar reserve and 2 px of caret slack: 160 px of text per line.
struct Laid {
    test::FakeMeasure measure; ScrollState scroll; Frame frame;
    const Placement* field = nullptr;
    Laid(Ui& ui, const TextFieldHandle& handle, int width = 200) {
        frame = arrange(layout::component(handle), ui, webAppearance(), measure, width, 400, 1.f, scroll);
        field = frame.find(handle);
        check(field != nullptr && field->multiline, "The field is placed as a multi-line field");
    }
};
using Lines = std::vector<std::pair<unsigned, unsigned>>;
Lines linesOf(const Placement& p) {
    Lines out;
    for (const auto& line : p.view.lines) out.push_back({line.begin, line.end});
    return out;
}

void wrappingAndCaretStopsFollowTheWidthOfBoldText() {
    Ui plainUi;
    auto plain = connectRich(plainUi, richControl("note", std::u16string(20, u'a')));
    Laid unstyled(plainUi, plain);
    check(linesOf(*unstyled.field) == Lines{{0, 20}}, "Twenty plain characters fit one 160 px line");
    Ui ui;
    auto field = connectRich(ui, richControl("note", std::u16string(20, u'a')));
    field.setStyle(0, 10, {.bold = true});
    Laid laid(ui, field);
    check(linesOf(*laid.field) == Lines{{0, 17}, {17, 20}}, "Ten bold (100 px) and seven plain (56 px) characters fill a line; the rest wraps");
    check(laid.field->view.lines[0].soft, "The wrapped piece continues into the next line");
    const auto& view = laid.field->view;
    check(laid.field->caretX(5) == view.text.x + 50 && laid.field->caretX(10) == view.text.x + 100 && laid.field->caretX(12) == view.text.x + 116,
          "Caret stops add up the bold advances (10 px) and then the plain ones (8 px)");
    check(laid.frame.textOffset(field, view.text.x + 116, view.text.y + 3) == 12 && laid.frame.textOffset(field, view.text.x + 52, view.text.y + 3) == 5,
          "A click lands on the stop nearest to it");
    const auto& pieces = view.lines[0].pieces;
    check(pieces.size() == 2 && pieces[0].end == 10 && pieces[0].style.bold && pieces[0].x == 0 && pieces[0].width == 100 &&
          pieces[1].begin == 10 && pieces[1].end == 17 && !pieces[1].style.bold && pieces[1].x == 100,
          "The line is cut into a bold run and a plain run, with their left edges");
    check(laid.field->bounds.h == unstyled.field->bounds.h, "Styling never changes the field's height");
}

void aStyleChangeAloneReWrapsTheNextArrange() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", std::u16string(20, u'a')));
    Laid before(ui, field);
    field.setStyle(0, 20, {.bold = true});
    Laid after(ui, field);
    check(linesOf(*before.field) == Lines{{0, 20}} && linesOf(*after.field) == Lines{{0, 16}, {16, 20}}, "All bold: 16 characters per 160 px line");
    check(detail::access(ui).fieldView("note")->lines.size() == 2, "Core's copy of the lines (for the cursor keys) is the new wrapping");
}

// Records what a component paints, so what the host would draw can be compared with expectations.
struct Recorder final : detail::ComponentPaint {
    struct Text { std::u16string text; FontStyle style; int x, y; Color color; };
    struct Fill { Rect rect; Color color; };
    std::vector<Text> texts; std::vector<Fill> fills;
    using ComponentPaint::ComponentPaint;
    void fill(Rect rect, Color color) override { fills.push_back({rect, color}); }
    void rounded(Rect, int, Color) override {}
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int x, int y, Color color) override { texts.push_back({std::u16string(value), {}, x, y, color}); }
    void styledText(std::u16string_view value, int, FontStyle style, int x, int y, Color color) override { texts.push_back({std::u16string(value), style, x, y, color}); }
};
Recorder paint(Ui& ui, const Placement& field, std::string_view id) {
    auto semantics = *detail::access(ui).describe(id);
    Recorder recorder(detail::access(ui), field, semantics, webAppearance(), 1.f, true);
    detail::componentType(ComponentKind::textField).paint(recorder);
    return recorder;
}

void eachRunIsPaintedInItsFaceColorAndUnderline() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abcdef"));
    field.setColors({{"red", {200, 0, 0}, {255, 120, 120}}});
    field.setStyle(0, 2, {.bold = true});
    field.setStyle(2, 4, {.italic = true, .underline = true});
    field.setStyle(4, 6, {.color = "red"});
    Laid laid(ui, field);
    const auto& view = laid.field->view;
    const auto textColor = detail::access(ui).effectivePalette(webAppearance()).text;
    auto recorded = paint(ui, *laid.field, "note");
    check(recorded.texts.size() == 3, "Three runs, three draw calls");
    check(recorded.texts[0].text == u"ab" && recorded.texts[0].style == FontStyle{true, false} && recorded.texts[0].x == view.text.x && recorded.texts[0].y == view.text.y && recorded.texts[0].color == textColor,
          "The bold run, at the left edge, in the normal text color");
    check(recorded.texts[1].text == u"cd" && recorded.texts[1].style == FontStyle{false, true} && recorded.texts[1].x == view.text.x + 20 && recorded.texts[1].color == textColor,
          "The italic run starts after the 20 px of bold text");
    check(recorded.texts[2].text == u"ef" && recorded.texts[2].style == FontStyle{false, false} && recorded.texts[2].x == view.text.x + 36 && recorded.texts[2].color == Color{200, 0, 0},
          "The red run is in the light value of its name");
    auto underline = std::find_if(recorded.fills.begin(), recorded.fills.end(), [&](const auto& f) { return f.rect == Rect{view.text.x + 20, view.text.y + 20, 16, 1}; });
    check(underline != recorded.fills.end() && underline->color == textColor && std::count_if(recorded.fills.begin(), recorded.fills.end(), [&](const auto& f) { return f.rect.h == 1 && f.rect.x >= view.text.x && f.rect.y == view.text.y + 20; }) == 1,
          "Exactly one underline: 1 px thick, under the italic run only (x 20, 16 px wide, at 5/6 of the line height)");
    ui.setPresentation({.variant = ThemeVariant::dark()});
    Laid dark(ui, field);
    auto recolored = paint(ui, *dark.field, "note");
    check(recolored.texts[2].color == Color{255, 120, 120} && recolored.texts[0].color == detail::access(ui).effectivePalette(webAppearance()).text,
          "After a theme change the same name paints in its dark value and plain text in the dark text color");
}

// #80 x #81: a centered or right-aligned rich line moves its runs with it, so the caret, the selection, the
// pointer hits and the drawn text all use the same x. FakeMeasure: "ab" bold is 20 px, "cd" plain 16 px,
// so line one is 36 px wide and line two ("ef", bold) 20 px.
void alignedRichLinesMoveTheirRunsWithTheLine() {
    for (auto align : {TextAlign::left, TextAlign::center, TextAlign::right}) {
        Ui ui;
        auto field = connectRich(ui, richControl("note", u"abcd\nef"));
        field.setStyle(0, 2, {.bold = true});
        field.setStyle(2, 4, {.underline = true});
        field.setStyle(5, 7, {.bold = true});
        check(bool(field.setTextAlign(align)) && bool(field.focus()) && bool(field.select(1, 3)), "align, focus and select inside the first line");
        Laid laid(ui, field);
        const auto& view = laid.field->view;
        auto shiftOf = [&](float width) { return align == TextAlign::left ? 0.f : align == TextAlign::center ? (view.text.w - width) / 2 : 160.f - width; };
        const float shift0 = shiftOf(36), shift1 = shiftOf(20);
        const int x0 = view.text.x + static_cast<int>(std::lround(shift0)), x1 = view.text.x + static_cast<int>(std::lround(shift1));
        auto at = [&](float shift, float inside) { return view.text.x + static_cast<int>(std::lround(shift + inside)); };
        check(view.lines.size() == 2 && view.lines[0].shift == shift0 && view.lines[1].shift == shift1, "each line is shifted by its own width");
        const auto& first = view.lines[0].pieces;
        check(first.size() == 2 && first[0].x == shift0 && first[1].x == shift0 + 20 && view.lines[1].pieces.size() == 1 && view.lines[1].pieces[0].x == shift1,
              "every run's x moved with its line");
        check(laid.field->caretX(0) == x0 && laid.field->caretX(1) == at(shift0, 10) && laid.field->caretX(2) == at(shift0, 20) &&
              laid.field->caretX(3) == at(shift0, 28) && laid.field->caretX(4) == at(shift0, 36) &&
              laid.field->caretX(5) == x1 && laid.field->caretX(7) == at(shift1, 20), "caret stops sit where the runs are");
        const int row = view.text.y + 3, second = view.rowY(1) + 3;
        check(laid.frame.textOffset(field, x0 + 11, row) == 1 && laid.frame.textOffset(field, at(shift0, 27), row) == 3 &&
              laid.frame.textOffset(field, x1 + 21, second) == 7, "a click on a drawn run lands on the stop under it");
        auto drawn = paint(ui, *laid.field, "note");
        check(drawn.texts.size() == 3 && drawn.texts[0].text == u"ab" && drawn.texts[0].x == x0 && drawn.texts[1].text == u"cd" &&
              drawn.texts[1].x == at(shift0, 20) && drawn.texts[2].text == u"ef" && drawn.texts[2].x == x1, "the runs are drawn at their moved positions");
        const int underlineWidth = at(shift0, 36) - at(shift0, 20);
        check(std::any_of(drawn.fills.begin(), drawn.fills.end(), [&](const auto& f) { return f.rect == Rect{at(shift0, 20), view.text.y + 20, underlineWidth, 1}; }),
              "the underline sits under its own run");
        const int selectionFrom = laid.field->caretX(1), selectionTo = laid.field->caretX(3);
        check(std::any_of(drawn.fills.begin(), drawn.fills.end(), [&](const auto& f) { return f.rect == Rect{selectionFrom, view.text.y, selectionTo - selectionFrom, view.rowHeight}; }),
              "the selection fill spans exactly the selected characters as drawn");
        check(bool(field.select(2, 2)), "a caret between the runs");
        Laid caret(ui, field);
        auto withCaret = paint(ui, *caret.field, "note");
        check(std::any_of(withCaret.fills.begin(), withCaret.fills.end(), [&](const auto& f) { return f.rect.x == caret.field->caretX(2) && f.rect.x == at(shift0, 20) && f.rect.h == caret.field->view.rowHeight; }),
              "the caret is drawn at the seam of the two runs");
    }
}

void unknownColorNamesAndDisabledFieldsUseThePlainColors() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"abc"));
    field.setStyle(0, 3, {.color = "missing", .underline = true});
    Laid laid(ui, field);
    const auto palette = detail::access(ui).effectivePalette(webAppearance());
    auto recorded = paint(ui, *laid.field, "note");
    check(recorded.texts.size() == 1 && recorded.texts[0].color == palette.text && field.richValue()->spans[0].color == "missing",
          "A name that is not in the list paints in the normal text color and stays in the value");
    field.setColors({{"missing", {1, 2, 3}, {4, 5, 6}}});
    check(paint(ui, *Laid(ui, field).field, "note").texts[0].color == Color{1, 2, 3}, "Adding the color to the list recolors the same value");
    field.enable(false);
    auto disabled = paint(ui, *Laid(ui, field).field, "note");
    check(disabled.texts[0].color == palette.muted, "A disabled field paints in the muted color whatever the colors say");
}

void aRichFieldWithoutStylesLaysOutLikeAPlainOne() {
    Ui richUi, plainUi;
    auto text = std::u16string(u"hello world again and more\n中文字\n") + std::u16string(45, u'x');
    auto rich = connectRich(richUi, richControl("note", text));
    auto plain = connectPlain(plainUi, {.id = "note", .value = text, .multiline = true});
    Laid a(richUi, rich), b(plainUi, plain);
    check(linesOf(*a.field) == linesOf(*b.field) && a.field->bounds == b.field->bounds && a.field->view.thumb == b.field->view.thumb,
          "Wrapping, height and scrollbar match a plain multi-line field");
    for (std::size_t i = 0; i < a.field->view.lines.size(); ++i)
        check(a.field->view.lines[i].run.stops == b.field->view.lines[i].run.stops, "Caret stops match too");
}

void compositionTakesTheTypingStyleAndCommitsAsOneStyledUndoEntry() {
    Ui ui;
    auto field = connectRich(ui, richControl("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    field.setStyle(0, 1, {.bold = true});
    host.select("note", 1, 1);
    check(bool(host.compositionUpdate(u"ㄅ", 1)), "Start composing after a bold character");
    check(host.visibleStyleSpans("note") == Spans{bold(0, 2)} && spansOf(field) == Spans{bold(0, 1)},
          "The preedit is shown in the style it will take; the committed styles are untouched while composing");
    Laid laid(ui, field);
    const auto& pieces = laid.field->view.lines[0].pieces;
    check(pieces.size() == 2 && pieces[0].style.bold && pieces[0].end == 2 && !pieces[1].style.bold && pieces[1].begin == 2 && laid.field->view.lines[0].end == 3,
          "The line measures the preedit as a bold run and the rest as plain");
    check(bool(host.compositionCommit(u"你")) && field.richValue()->text == u"a你b" && spansOf(field) == Spans{bold(0, 2)}, "The commit is bold, like typing there");
    check(bool(field.undo()) && field.richValue()->text == u"ab" && spansOf(field) == Spans{bold(0, 1)}, "One undo removes the committed text and its style");
}

const test::Case cases[] = {
    {"normalization_merges_sorts_and_drops_empty_spans", normalizationMergesSortsAndDropsEmptySpans},
    {"normalization_rejects_overlap_and_out_of_range_spans", normalizationRejectsOverlapAndOutOfRangeSpans},
    {"a_rich_field_is_a_multi_line_option_fixed_at_creation", aRichFieldIsAMultiLineOptionFixedAtCreation},
    {"rich_values_are_read_and_written_in_normal_form", richValuesAreReadAndWrittenInNormalForm},
    {"styles_apply_to_a_range_and_are_one_undo_entry", stylesApplyToARangeAndAreOneUndoEntry},
    {"read_only_and_unconnected_rich_fields_refuse_style_edits", readOnlyAndUnconnectedRichFieldsRefuseStyleEdits},
    {"max_length_counts_plain_characters_only", maxLengthCountsPlainCharactersOnly},
    {"ctrl_b_toggles_the_selection_and_all_three_shortcuts_work", ctrlBTogglesTheSelectionAndAllThreeShortcutsWork},
    {"a_partly_styled_selection_becomes_fully_styled_first", aPartlyStyledSelectionBecomesFullyStyledFirst},
    {"without_a_selection_ctrl_b_changes_the_next_typed_text_only", withoutASelectionCtrlBChangesTheNextTypedTextOnly},
    {"typed_text_follows_the_previous_character_or_the_next_at_a_paragraph_start", typedTextFollowsThePreviousCharacterOrTheNextAtAParagraphStart},
    {"each_styled_insertion_is_one_undo_entry", eachStyledInsertionIsOneUndoEntry},
    {"the_field_takes_ctrl_b_before_an_application_shortcut", theFieldTakesCtrlBBeforeAnApplicationShortcut},
    {"a_color_name_resolves_per_theme_and_unknown_names_keep_their_name", aColorNameResolvesPerThemeAndUnknownNamesKeepTheirName},
    {"external_source_changes_and_conflicts_cover_styles", externalSourceChangesAndConflictsCoverStyles},
    {"confirmed_and_immediate_writes_carry_the_rich_value", confirmedAndImmediateWritesCarryTheRichValue},
    {"a_confirmation_group_takes_a_rich_field_in_the_mixed_form", aConfirmationGroupTakesARichFieldInTheMixedForm},
    {"copy_and_paste_keep_styles_between_rich_fields", copyAndPasteKeepStylesBetweenRichFields},
    {"a_color_name_missing_from_the_target_list_survives_a_paste", aColorNameMissingFromTheTargetListSurvivesAPaste},
    {"external_plain_text_takes_the_typing_style", externalPlainTextTakesTheTypingStyle},
    {"other_fields_and_hosts_get_plain_text_only", otherFieldsAndHostsGetPlainTextOnly},
    {"cut_removes_styled_text_and_undo_restores_it", cutRemovesStyledTextAndUndoRestoresIt},
    {"a_rich_paste_over_the_limit_is_refused_whole", aRichPasteOverTheLimitIsRefusedWhole},
    {"the_semantic_layer_reads_and_writes_styles_like_the_typed_calls", theSemanticLayerReadsAndWritesStylesLikeTheTypedCalls},
    {"style_changes_raise_a_property_event_and_colors_resolve_for_the_theme", styleChangesRaiseAPropertyEventAndColorsResolveForTheTheme},
    {"wrapping_and_caret_stops_follow_the_width_of_bold_text", wrappingAndCaretStopsFollowTheWidthOfBoldText},
    {"a_style_change_alone_re_wraps_the_next_arrange", aStyleChangeAloneReWrapsTheNextArrange},
    {"each_run_is_painted_in_its_face_color_and_underline", eachRunIsPaintedInItsFaceColorAndUnderline},
    {"aligned_rich_lines_move_their_runs_with_the_line", alignedRichLinesMoveTheirRunsWithTheLine},
    {"unknown_color_names_and_disabled_fields_use_the_plain_colors", unknownColorNamesAndDisabledFieldsUseThePlainColors},
    {"a_rich_field_without_styles_lays_out_like_a_plain_one", aRichFieldWithoutStylesLaysOutLikeAPlainOne},
    {"composition_takes_the_typing_style_and_commits_as_one_styled_undo_entry", compositionTakesTheTypingStyleAndCommitsAsOneStyledUndoEntry},
};
}

int main() { return test::run("richtext", cases); }
