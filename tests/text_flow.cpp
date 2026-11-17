#include "core_internal.hpp"
// #80 text alignment, line spacing and paragraph spacing through the public core, semantic and layout
// seams, with a deterministic fake text backend. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"
#include "isoduo/semantics.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
using namespace isoduo;
using test::check;
using test::FakeMeasure;

// FakeMeasure at 16 px text: an ASCII character is 8 px wide and a line is 24 px tall.
namespace {
struct Laid {
    FakeMeasure measure; ScrollState scroll; Frame frame;
    template<class Handle> Laid(Ui& ui, const Handle& handle, int width = 200, float scale = 1.f, const Appearance& appearance = webAppearance()) {
        frame = arrange(layout::component(handle), ui, appearance, measure, width, 400, scale, scroll);
    }
};
ComponentConfig label(std::string id, std::u16string value, TextAlign align = TextAlign::left, double spacing = 1.0, int paragraph = 0) {
    return {.id = std::move(id), .value = std::move(value), .textAlign = align, .lineSpacing = spacing, .paragraphSpacing = paragraph};
}

void aLabelAlignsEachLineInsideItsOwnWidth() {
    struct Case { TextAlign align; int first, second; };
    // "abcd" is 32 px and "ab" 16 px wide in a 200 px label.
    for (const auto& c : {Case{TextAlign::left, 0, 0}, Case{TextAlign::center, 84, 92}, Case{TextAlign::right, 168, 184}}) {
        Ui ui;
        auto handle = *ui.addLabel(label("l", u"abcd\nab", c.align));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p && p->lines.size() == 2, "Two lines");
        check(p->lines[0].bounds.x == c.first && p->lines[1].bounds.x == c.second, "Each line is aligned on its own width");
        check(p->bounds.x == 0 && p->bounds.w == 200 && p->bounds.h == 48, "The label's own bounds never change");
        check(p->lines[0].bounds.y == 0 && p->lines[1].bounds.y == 24, "Lines keep their rows");
    }
}

void lineSpacingMultipliesTheFontLineHeightAndCentersTheGlyphs() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"ab\ncd", TextAlign::left, 1.5));
    {
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        // 16 px text: the font line is 24, a row 36, the spare 12 splits 6 above and 6 below.
        check(p->lines[0].bounds.y == 6 && p->lines[1].bounds.y == 42 && p->lines[0].bounds.h == 24, "Rows are 1.5 times the line and the glyphs sit in the middle");
        check(p->bounds.h == 72, "The label is two full rows tall");
    }
    check(bool(ui.setPresentation({.textSize = 28})), "Raise the text size");
    {
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        // 28 px text: the font line is 42, a row 63; (63 - 42) / 2 = 10 above.
        check(p->lines[0].bounds.y == 10 && p->lines[1].bounds.y == 73 && p->bounds.h == 126, "Spacing follows the text size proportionally");
    }
    check(bool(ui.setPresentation({.textSize = 28, .density = Density::compact})), "Switch to compact");
    Laid compact(ui, handle);
    check(compact.frame.find(handle)->bounds.h == 126, "Density does not change the line spacing");
}

void paragraphSpacingAddsSpaceOnlyAfterALineFeed() {
    Ui ui;
    // 100 px wide at 16 px text: 12 characters per line, so 20 characters wrap into 12 + 8.
    auto handle = *ui.addLabel(label("l", u"aaaaaaaaaaaaaaaaaaaa\nb", TextAlign::left, 1.0, 8));
    {
        Laid laid(ui, handle, 100);
        const auto* p = laid.frame.find(handle);
        check(p->lines.size() == 3 && p->lines[0].bounds.y == 0 && p->lines[1].bounds.y == 24, "A wrapped line adds nothing");
        check(p->lines[2].bounds.y == 56 && p->bounds.h == 80, "The LF adds 8 DIP: 3 rows of 24 plus 8");
    }
    {
        Laid laid(ui, handle, 150, 1.5f);
        const auto* p = laid.frame.find(handle);
        // At 150 % the text is 24 px (line 36), an 'a' is 12 px, a 150 px label holds 12 of them, and the gap is 12 px.
        check(p->lines.size() == 3 && p->lines[2].bounds.y == 36 * 2 + 12, "The gap scales with the DPI");
    }
    Ui other;
    auto small = *other.addLabel(label("l", u"a\nb", TextAlign::left, 1.0, 8));
    check(bool(other.setPresentation({.textSize = 28})), "Raise the text size");
    Laid big(other, small);
    check(big.frame.find(small)->lines[1].bounds.y == 42 + 8, "The gap stays 8 DIP when the text size changes");
    check(bool(other.setPresentation({.textSize = 28, .density = Density::compact})), "Compact density");
    Laid compact(other, small);
    check(compact.frame.find(small)->lines[1].bounds.y == 42 + 8, "Density does not change the gap");
}

void outOfRangeValuesAreInvalidAndChangeNothing() {
    Ui ui;
    for (double bad : {0.9, 3.1, -1.0, std::numeric_limits<double>::quiet_NaN()})
        check(ui.addLabel(label("bad", u"x", TextAlign::left, bad)).status == Status::invalidValue, "A label is not created with a bad line spacing");
    for (int bad : {-1, 49})
        check(ui.addLabel(label("bad", u"x", TextAlign::left, 1.0, bad)).status == Status::invalidValue, "A label is not created with a bad paragraph spacing");
    check(ui.addTextField({.id = "bad", .lineSpacing = 3.1}).status == Status::invalidValue, "Nor a text field");
    check(ui.addButton({.id = "b", .textAlign = TextAlign::center}).status == Status::invalidValue, "A button has no alignment");
    check(ui.addCheckbox({.id = "c", .lineSpacing = 1.5}).status == Status::invalidValue, "A checkbox has no line spacing");
    check(ui.addProgress({.id = "p", .paragraphSpacing = 8}).status == Status::invalidValue, "A progress bar has no paragraph spacing");
    check(bool(ui.addButton({.id = "ok"})), "Defaults are fine everywhere");

    auto labelHandle = *ui.addLabel(label("l", u"x"));
    auto field = *ui.addTextField({.id = "t"});
    auto exercise = [](auto& h, const char* what) {
        check(*h.textAlign() == TextAlign::left && *h.lineSpacing() == 1.0 && *h.paragraphSpacing() == 0, what);
        check(h.setLineSpacing(0.9).status == Status::invalidValue && h.setLineSpacing(3.1).status == Status::invalidValue, what);
        check(h.setLineSpacing(std::numeric_limits<double>::quiet_NaN()).status == Status::invalidValue, what);
        check(h.setParagraphSpacing(-1).status == Status::invalidValue && h.setParagraphSpacing(49).status == Status::invalidValue, what);
        check(*h.lineSpacing() == 1.0 && *h.paragraphSpacing() == 0, "A rejected value leaves the component unchanged");
        check(bool(h.setLineSpacing(1.0)) && bool(h.setLineSpacing(3.0)) && *h.lineSpacing() == 3.0, "The limits are valid");
        check(bool(h.setParagraphSpacing(0)) && bool(h.setParagraphSpacing(48)) && *h.paragraphSpacing() == 48, "The limits are valid");
        check(bool(h.setTextAlign(TextAlign::right)) && *h.textAlign() == TextAlign::right, "Alignment is settable");
        auto described = h.describe();
        check(described->textAlign == TextAlign::right && described->lineSpacing == 3.0 && described->paragraphSpacing == 48, "describe reports the three properties");
    };
    exercise(labelHandle, "A label starts with the defaults");
    exercise(field, "A text field starts with the defaults");
}

void aScrollableLabelStepsOneActualRow() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"1\n2\n3\n4\n5\n6\n7\n8\n9\n10", TextAlign::left, 1.5));
    ScrollState outer, inner;
    auto page = layout::scroll(layout::column({layout::scrollText(handle, inner, 100)}));
    FakeMeasure measure;
    arrange(page, ui, webAppearance(), measure, 300, 400, 1.f, outer);
    auto& host = detail::access(ui);
    check(bool(host.focus("l")), "The overflowing label takes focus");
    check(bool(host.keyDown({.key = Key::down})) && inner.offset == 36, "Down scrolls by one row: 24 * 1.5");
    check(bool(host.keyDown({.key = Key::down})) && inner.offset == 72, "And again");
    check(bool(host.keyDown({.key = Key::up})) && inner.offset == 36, "Up scrolls back one row");
    check(bool(handle.setParagraphSpacing(8)), "Add paragraph spacing");
    arrange(page, ui, webAppearance(), measure, 300, 400, 1.f, outer);
    check(bool(host.keyDown({.key = Key::down})) && inner.offset == 72, "A step is still one row, not a paragraph");
}

// A text field, connected so it accepts edits like an application's would.
TextFieldHandle fieldOf(Ui& ui, ComponentConfig control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto field = *ui.addTextField(std::move(control));
    check(bool(field.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; })), "Connect a write");
    return field;
}
ComponentConfig single(std::string id, std::u16string value, TextAlign align = TextAlign::left, double spacing = 1.0) {
    return {.id = std::move(id), .value = std::move(value), .textAlign = align, .lineSpacing = spacing};
}

// The web appearance puts the editor 12 px in and 176 px wide in a 200 px field; the caret keeps 2 px
// of room at the right, so text up to 174 px wide is placed by its alignment and longer text scrolls.
void aSingleLineFieldPlacesShortTextByItsAlignment() {
    struct Case { TextAlign align; int start, end; };
    // "abcd" is 32 px wide: centered it starts (176 - 32) / 2 = 72 px into the editor, right-aligned 174 - 32 = 142.
    for (const auto& c : {Case{TextAlign::left, 12, 44}, Case{TextAlign::center, 84, 116}, Case{TextAlign::right, 154, 186}}) {
        Ui ui;
        auto handle = fieldOf(ui, single("t", u"abcd", c.align));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p->caretX(0) == c.start && p->caretX(4) == c.end, "The text starts and ends where its alignment puts it");
        check(p->bounds.x == 0 && p->bounds.w == 200, "The field itself does not move");
        check(laid.frame.textOffset(handle, c.start - 30) == 0 && laid.frame.textOffset(handle, c.end + 30) == 4, "A click outside the text goes to its nearest end");
        check(laid.frame.textOffset(handle, c.start + 17) == 2 && laid.frame.textOffset(handle, c.start + 9) == 1, "A click inside maps through the drawn position");
    }
}

void anEmptyCenteredFieldPutsTheCaretInTheMiddle() {
    Ui ui;
    auto handle = fieldOf(ui, single("t", u"", TextAlign::center));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const int middle = p->bounds.x + p->textField.editor.x + p->textField.editor.w / 2;
    check(p->caretX(0) == middle && middle == 100, "The caret is in the middle of the editor");
    auto& host = detail::access(ui);
    check(bool(host.focus("t")) && bool(host.textInput(u"ab")), "Type two characters");
    Laid typed(ui, handle);
    check(typed.frame.find(handle)->caretX(2) == 108 && typed.frame.find(handle)->caretX(0) == 92, "Typed text grows around the middle");
}

void textWiderThanTheFieldScrollsWhateverTheAlignment() {
    const std::u16string wide(32, u'a');  // 256 px, wider than the 174 px room
    std::vector<int> ends;
    for (auto align : {TextAlign::left, TextAlign::center, TextAlign::right}) {
        Ui ui;
        auto handle = fieldOf(ui, single("t", wide, align));
        auto& host = detail::access(ui);
        check(bool(host.focus("t")), "Focus the field");
        host.select("t", 32, 32);
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        ends.push_back(p->caretX(32));
        check(p->caretX(0) < 12, "The start has scrolled out of view");
    }
    check(ends[0] == 186 && ends[1] == ends[0] && ends[2] == ends[0], "The caret at the end sits at the right edge, as for a left-aligned field");
}

void lineSpacingMakesASingleLineFieldTallerAndCentersTheText() {
    Ui plainUi, spacedUi;
    auto plain = fieldOf(plainUi, single("t", u"abcd"));
    auto spaced = fieldOf(spacedUi, single("t", u"abcd", TextAlign::left, 1.5));
    Laid a(plainUi, plain), b(spacedUi, spaced);
    const auto* pa = a.frame.find(plain); const auto* pb = b.frame.find(spaced);
    check(pa->textField.editor.h == 24 && pb->textField.editor.h == 36, "The editor is one 1.5 row tall");
    check(pb->bounds.h == pa->bounds.h + 12, "The field grows by the spare 12 px");
    check(pb->caretRect(2).h == 36 && pb->caretRect(2).y == pb->textField.editor.y, "The caret is as tall as the row");
    check(pb->textTop == 6, "The glyphs sit in the middle of the row");
}

ComponentConfig multi(std::string id, std::u16string value, unsigned lines = 3, TextAlign align = TextAlign::left, double spacing = 1.0, int paragraph = 0) {
    return {.id = std::move(id), .value = std::move(value), .multiline = true, .visibleLines = lines,
            .textAlign = align, .lineSpacing = spacing, .paragraphSpacing = paragraph};
}
// A 200 px multi-line field: 176 px editor at x = 12, 14 px scrollbar reserve, so the text area is 162 px
// wide and a line wraps at 160 px (20 characters of 8 px). Centered text starts (162 - w) / 2 in, right-aligned 160 - w.
void aMultiLineFieldAlignsEveryDisplayLine() {
    struct Case { TextAlign align; int line0, line1, line2; };
    // Lines "ab" (16 px), "cde" (24 px), "" (0 px).
    for (const auto& c : {Case{TextAlign::left, 12, 12, 12}, Case{TextAlign::center, 85, 81, 93}, Case{TextAlign::right, 156, 148, 172}}) {
        Ui ui;
        auto handle = fieldOf(ui, multi("t", u"ab\ncde\n", 3, c.align));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p->multiline && p->view.lines.size() == 3, "Three lines");
        check(p->caretX(0) == c.line0 && p->caretX(3) == c.line1 && p->caretX(7) == c.line2, "Every line starts where its alignment puts it");
        check(p->caretX(2) == c.line0 + 16 && p->caretX(6) == c.line1 + 24, "And ends one text width later");
        check(p->bounds.x == 0 && p->bounds.w == 200, "The field itself does not move");
    }
}

void pointerHitsFollowTheDrawnPositionOfAnAlignedLine() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"ab\ncde\n", 3, TextAlign::right));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const int y0 = p->view.text.y + 5, y1 = y0 + 24;
    // Line 1 "cde" is drawn from x = 148: stops at 148, 156, 164, 172.
    check(laid.frame.textOffset(handle, 148, y1) == 3 && laid.frame.textOffset(handle, 161, y1) == 5 && laid.frame.textOffset(handle, 190, y1) == 6, "A click maps through the drawn position");
    check(laid.frame.textOffset(handle, 20, y1) == 3, "A click left of the text goes to the line start");
    check(laid.frame.textOffset(handle, 172, y0) == 2 && laid.frame.textOffset(handle, 164, y0) == 1 && laid.frame.textOffset(handle, 150, y0) == 0, "Line 0 starts at x = 156");
}

void verticalKeysKeepTheDrawnHorizontalPositionOfAlignedLines() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"abcdefgh\nab", 3, TextAlign::right));
    auto& host = detail::access(ui);
    Laid first(ui, handle);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 8, 8);
    Laid laid(ui, handle);
    const int x = laid.frame.find(handle)->caretX(8);   // line 0 is 64 px wide, drawn from 108: the end is at 172
    check(x == 172, "The caret is at the end of the right-aligned line");
    check(bool(host.keyDown({.key = Key::down})), "Down");
    Laid below(ui, handle);
    const auto caret = host.edit("t")->selection.caret;
    check(caret == 11 && below.frame.find(handle)->caretX(caret) == 172, "The caret lands under the same drawn x, at the end of the shorter line");
    check(bool(host.keyDown({.key = Key::up})), "Up");
    Laid above(ui, handle);
    check(host.edit("t")->selection.caret == 8 && above.frame.find(handle)->caretX(8) == 172, "And back");
}

// Code review 2026-11-12 item 9: the lines cursor keys use are the last arrange's. A property change without a
// host relayout in between must make them stale, so the next key asks the host to arrange again.
void verticalKeysAfterAPropertyChangeDoNotUseTheOldLines() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"abcdefgh\nab"));
    auto& host = detail::access(ui);
    std::optional<Laid> latest;
    int relayouts = 0;
    host.onRelayout([&] { ++relayouts; latest.emplace(ui, handle); });
    Laid first(ui, handle);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 8, 8);
    Laid placed(ui, handle);
    check(bool(handle.setTextAlign(TextAlign::right)), "Right-align after the last arrange");
    check(bool(host.keyDown({.key = Key::down})), "Down");
    check(relayouts == 1, "The changed alignment made the old lines stale: one relayout");
    // Line 0 is 64 px wide and drawn from 108, so the caret at its end is at x = 172. Line 1 "ab" is drawn from 144
    // (16 px wide, flush right): x = 172 is past its end, so the caret lands at the end of "ab". Up then returns under
    // the same drawn x, at the end of the long line.
    check(host.edit("t")->selection.caret == 11, "Down lands at the end of the shorter line");
    check(bool(host.keyDown({.key = Key::up})), "Up");
    check(host.edit("t")->selection.caret == 8, "And back to the end of the long line");
    // The same for every other property that moves lines: each makes the next key relayout exactly once.
    struct Change { const char* name; std::function<Result()> apply; };
    const Change changes[] = {
        {"justify method", [&] { return handle.setJustifyMethod(JustifyMethod::perCharacter); }},
        {"line spacing", [&] { return handle.setLineSpacing(1.5); }},
        {"paragraph spacing", [&] { return handle.setParagraphSpacing(8); }},
        {"semantic text align", [&] { return ui.semantics().perform({"t"}, Verb::setTextAlign, TextAlign::center); }},
        {"justify as left", [&] { auto s = ui.presentation(); s.justifyAsLeft = true; return ui.setPresentation(s); }},
    };
    for (const auto& change : changes) {
        latest.emplace(ui, handle);  // an arrange settles the view, so it is fresh again
        const int before = relayouts;
        check(bool(change.apply()), change.name);
        host.keyDown({.key = Key::down});
        check(relayouts == before + 1, (std::string(change.name) + ": a key after the change relays out").c_str());
    }
}

void multiLineHeightCountsTheSpacedRowsAndParagraphGaps() {
    Ui ui;
    auto plain = fieldOf(ui, multi("plain", u"a\nb\nc\nd"));
    auto spaced = fieldOf(ui, multi("spaced", u"a\nb\nc\nd", 3, TextAlign::left, 1.5));
    auto gapped = fieldOf(ui, multi("gapped", u"a\nb\nc\nd", 3, TextAlign::left, 1.0, 8));
    auto both = fieldOf(ui, multi("both", u"a\nb\nc\nd", 3, TextAlign::left, 1.5, 8));
    auto editorH = [&](const TextFieldHandle& h) { Laid laid(ui, h); return laid.frame.find(h)->textField.editor.h; };
    check(editorH(plain) == 72, "Three rows of 24");
    check(editorH(spaced) == 108, "Three rows of 36");
    check(editorH(gapped) == 72 + 2 * 8, "Three rows and two paragraph gaps");
    check(editorH(both) == 108 + 2 * 8, "Spaced rows and gaps together");
    Laid laid(ui, both);
    const auto* p = laid.frame.find(both);
    check(p->view.rowHeight == 36 && p->view.lineHeight == 24, "The view keeps the font line and the row apart");
    const int top = p->view.text.y;
    check(p->caretRect(0).y == top && p->caretRect(2).y == top + 36 + 8 && p->caretRect(4).y == top + 2 * (36 + 8), "Each paragraph row starts one row and one gap below the last");
    check(p->caretRect(2).h == 36, "The caret is as tall as the row");
}

void softWrappedLinesAddNoParagraphGapInAField() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nb", 3, TextAlign::left, 1.0, 8));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    // 40 characters wrap into 20 + 20; the LF then starts "b" after one gap.
    check(p->view.lines.size() == 3 && p->view.lines[0].soft && !p->view.lines[1].soft, "Two wrapped pieces then a new paragraph");
    const int top = p->view.text.y;
    check(p->caretRect(0).y == top && p->caretRect(20).y == top + 24 && p->caretRect(41).y == top + 48 + 8, "The gap is only after the LF");
}

void aClickBetweenRowsBelongsToTheRowAbove() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"ab\ncd\nef", 3, TextAlign::left, 1.0, 8));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const int top = p->view.text.y;
    check(laid.frame.textOffset(handle, 12, top + 12) == 0 && laid.frame.textOffset(handle, 12, top + 24 + 3) == 0, "Row 0 and the gap below it");
    check(laid.frame.textOffset(handle, 12, top + 24 + 8 + 2) == 3 && laid.frame.textOffset(handle, 12, top + 2 * 32 + 4) == 6, "The next rows");
}

bool hasVerb(const NodeSnapshot& node, Verb verb) { return std::find(node.verbs.begin(), node.verbs.end(), verb) != node.verbs.end(); }

void theSemanticLayerReadsAndWritesTheThreePropertiesLikeTheHandles() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"x", TextAlign::center, 1.4, 8));
    auto field = fieldOf(ui, multi("t", u"x", 3, TextAlign::right, 2.0, 16));
    auto button = *ui.addButton({.id = "b"});
    auto& tree = ui.semantics();
    auto node = tree.get({"l"});
    check(node && node->textAlign == TextAlign::center && node->lineSpacing == 1.4 && node->paragraphSpacing == 8, "A label reports its properties");
    check(hasVerb(*node, Verb::setTextAlign) && hasVerb(*node, Verb::setLineSpacing) && hasVerb(*node, Verb::setParagraphSpacing), "A label offers the three verbs");
    auto text = tree.get({"t"});
    check(text->textAlign == TextAlign::right && text->lineSpacing == 2.0 && text->paragraphSpacing == 16 && hasVerb(*text, Verb::setTextAlign), "A text field too");
    check(!hasVerb(*tree.get({"b"}), Verb::setTextAlign) && !hasVerb(*tree.get({"b"}), Verb::setLineSpacing), "A button offers none");
    check(tree.perform({"b"}, Verb::setTextAlign, TextAlign::center).status == Status::notSupported, "A button refuses the verb");

    std::vector<std::string> changed;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::propertyChanged) changed.push_back(e.property); });
    check(bool(tree.perform({"l"}, Verb::setTextAlign, TextAlign::left)) && *handle.textAlign() == TextAlign::left, "Semantic alignment reaches the typed read");
    check(bool(tree.perform({"l"}, Verb::setLineSpacing, 1.0)) && *handle.lineSpacing() == 1.0, "Semantic line spacing reaches the typed read");
    check(bool(tree.perform({"l"}, Verb::setParagraphSpacing, 0)) && *handle.paragraphSpacing() == 0, "Semantic paragraph spacing reaches the typed read");
    check(bool(field.setTextAlign(TextAlign::center)) && tree.get({"t"})->textAlign == TextAlign::center, "A typed change reaches the semantic read");
    tree.dispatchEvents();
    for (const char* name : {"textAlign", "lineSpacing", "paragraphSpacing"})
        check(std::count(changed.begin(), changed.end(), name) >= 1, "A change raises property_changed");

    check(tree.perform({"l"}, Verb::setLineSpacing, 3.1).status == field.setLineSpacing(3.1).status, "The same status for an out-of-range line spacing");
    check(tree.perform({"l"}, Verb::setLineSpacing, 0.9).status == Status::invalidValue && handle.lineSpacing().value.value() == 1.0, "Out of range, unchanged");
    check(tree.perform({"l"}, Verb::setParagraphSpacing, 49).status == Status::invalidValue && tree.perform({"l"}, Verb::setParagraphSpacing, -1).status == Status::invalidValue, "Paragraph spacing limits");
    check(tree.perform({"l"}, Verb::setTextAlign, 1).status == Status::invalidValue, "The wrong argument type is invalid");
    check(tree.perform({"l"}, Verb::setLineSpacing, std::u16string(u"2")).status == Status::invalidValue, "As is a string");
    check(tree.perform({"missing"}, Verb::setTextAlign, TextAlign::left).status == Status::notFound, "An unknown component is not found");
}

// ---- One generic driver: every text-flow subject x alignment x appearance x density ---------------
// What a subject lays out, read the same way for every kind: where each display line starts, and the
// area the alignment works in (a label's own width, a field's editor or text area). Text is "ab" and
// "cde" at 16 px, so 16 and 24 px wide.
struct Drawn { std::vector<int> starts; int areaX = 0, areaW = 0, room = 0; Rect bounds; };
struct Subject {
    const char* name;
    bool field;      // fields keep a 2 px caret slack at the right
    Drawn (*lay)(Ui&, TextAlign, const Appearance&);
};
Drawn drawnLabel(Ui& ui, TextAlign align, const Appearance& look, bool scrollable) {
    auto handle = *ui.addLabel(label("subject", u"ab\ncde", align));
    FakeMeasure measure; ScrollState outer, inner;
    auto node = scrollable ? layout::scroll(layout::column({layout::scrollText(handle, inner, 400)})) : layout::component(handle);
    auto frame = arrange(node, ui, look, measure, 300, 400, 1.f, outer);
    const auto* p = frame.find(handle);
    Drawn out{{}, p->bounds.x, p->bounds.w, p->bounds.w, p->bounds};
    for (const auto& line : p->lines) out.starts.push_back(line.bounds.x);  // frame coordinates already
    return out;
}
Drawn drawnField(Ui& ui, ComponentConfig control, const Appearance& look) {
    auto handle = fieldOf(ui, std::move(control));
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::component(handle), ui, look, measure, 300, 400, 1.f, scroll);
    const auto* p = frame.find(handle);
    Drawn out;
    out.bounds = p->bounds;
    if (p->multiline) {
        out.areaX = p->view.text.x; out.areaW = p->view.text.w;
        for (const auto& line : p->view.lines) out.starts.push_back(p->caretX(line.begin));
    } else {
        out.areaX = p->bounds.x + p->textField.editor.x; out.areaW = p->textField.editor.w;
        out.starts.push_back(p->caretX(0));
    }
    return out;
}
const Subject subjects[] = {
    {"label", false, [](Ui& ui, TextAlign a, const Appearance& l) { return drawnLabel(ui, a, l, false); }},
    {"scrollable label", false, [](Ui& ui, TextAlign a, const Appearance& l) { return drawnLabel(ui, a, l, true); }},
    {"single-line field", true, [](Ui& ui, TextAlign a, const Appearance& l) { return drawnField(ui, single("subject", u"cde", a), l); }},
    {"multi-line field", true, [](Ui& ui, TextAlign a, const Appearance& l) { return drawnField(ui, multi("subject", u"ab\ncde", 3, a), l); }},
};
void everySubjectAlignsInEveryAppearanceAndDensity() {
    for (const auto& subject : subjects) for (const auto* look : {&webAppearance(), &materialAppearance()}) for (auto density : {Density::standard, Density::compact}) {
        Ui baselineUi;
        check(bool(baselineUi.setPresentation({.density = density})), "Set the density");
        const auto baseline = subject.lay(baselineUi, TextAlign::left, *look);
        check(!baseline.starts.empty(), subject.name);
        for (auto align : {TextAlign::left, TextAlign::center, TextAlign::right}) {
            Ui ui;
            check(bool(ui.setPresentation({.density = density})), "Set the density");
            const auto drawn = subject.lay(ui, align, *look);
            check(drawn.bounds == baseline.bounds, "The component's own bounds do not depend on the alignment");
            check(drawn.starts.size() == baseline.starts.size(), subject.name);
            const int inset = subject.field ? look->componentPresentation(density).inset : 0;
            const int room = drawn.areaW - inset;
            // Hand-computed widths: "ab" 16 px, "cde" 24 px; a single-line field holds only "cde".
            const bool singleLine = std::string_view(subject.name) == "single-line field";
            std::vector<int> widths = singleLine ? std::vector<int>{24} : std::vector<int>{16, 24};
            for (std::size_t i = 0; i < widths.size(); ++i) {
                // Centered text sits in the middle of the area, right-aligned text ends at its room (the caret slack).
                const int expected = align == TextAlign::left ? drawn.areaX
                    : align == TextAlign::center ? drawn.areaX + (drawn.areaW - widths[i]) / 2 : drawn.areaX + room - widths[i];
                check(std::abs(drawn.starts[i] - expected) <= 1, subject.name);
            }
        }
    }
}

void scrollingASpacedFieldMovesWholeLinesAndPageKeysUseTheVisibleCount() {
    const std::u16string ten = u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9";
    Ui ui;
    auto handle = fieldOf(ui, multi("t", ten, 3, TextAlign::center, 1.5, 8));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const ScrollRegion* region = nullptr;
    for (const auto& r : laid.frame.regions) if (r.textField == "t") region = &r;
    check(region && region->step == 36 && region->extent == 10 * 36 && region->maximum == 7 * 36, "The scrollbar counts rows of 36, not the paragraph gaps");
    const int x = p->view.text.x + 10, y = p->view.text.y + 10;
    check(laid.frame.wheel(ui, x, y, -36) && *handle.scrollLine() == 1, "One wheel step of a row scrolls one line");
    check(laid.frame.wheel(ui, x, y, 2 * 36) && *handle.scrollLine() == 0 && !laid.frame.wheel(ui, x, y, 36), "Back up, and no further");
    auto& host = detail::access(ui);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 0, 0);
    check(bool(host.keyDown({.key = Key::pageDown})) && host.edit("t")->selection.caret == 6, "Page Down moves by the three visible lines");
    Laid after(ui, handle);
    check(*handle.scrollLine() == 3, "The first visible line follows the page");
    check(after.frame.find(handle)->caretRect(6).y == after.frame.find(handle)->view.text.y, "The caret's line is now the top row");
    check(bool(host.keyDown({.key = Key::pageUp})) && host.edit("t")->selection.caret == 0, "Page Up goes back");
}

void aLargerTextSizeScalesTheRowOfAFieldProportionally() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"a\nb\nc\nd", 3, TextAlign::left, 1.5));
    {
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p->caretRect(2).y - p->caretRect(0).y == 36, "Rows are 24 * 1.5 apart at 16 px");
    }
    check(bool(ui.setPresentation({.textSize = 28})), "Raise the text size");
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p->caretRect(2).y - p->caretRect(0).y == 63 && p->view.rowHeight == 63 && p->textField.editor.h == 3 * 63, "Rows are the new line height 42 * 1.5 apart");
    check(bool(ui.setPresentation({.textSize = 28, .density = Density::compact})), "Compact density");
    Laid compact(ui, handle);
    check(compact.frame.find(handle)->view.rowHeight == 63, "Density leaves the spacing alone");
}

void draggingSelectsByTheDrawnPositionOfAlignedLines() {
    Ui ui;
    auto handle = fieldOf(ui, multi("t", u"abcd\nef", 3, TextAlign::right));
    auto& host = detail::access(ui);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 0, 0);
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    // Line 1 "ef" (16 px) is right-aligned: drawn from 12 + 144 = 156, stops at 156, 164, 172.
    check(laid.frame.selectTo(ui, handle, 164, p->view.text.y + 24 + 5), "Drag into the second row");
    check(host.edit("t")->selection.anchor == 0 && host.edit("t")->selection.caret == 6, "The caret is one character into the drawn line");
    check(laid.frame.selectTo(ui, handle, 20, p->view.text.y + 24 + 5) && host.edit("t")->selection.caret == 5, "Dragging left of the text selects up to its start");
}

void theInputMethodAnchorsAtTheDrawnCaretOfAnAlignedField() {
    for (bool multiline : {false, true}) {
        Ui ui;
        auto handle = fieldOf(ui, multiline ? multi("t", u"", 3, TextAlign::center, 1.5) : single("t", u"", TextAlign::center, 1.5));
        auto& host = detail::access(ui);
        check(bool(host.focus("t")), "Focus the field");
        check(bool(host.compositionUpdate(u"ㄅ", 1)), "Start composing one wide character (16 px)");
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        const auto caret = host.edit("t")->visibleCaret();
        // The composition is 16 px wide: centered it starts (area - 16) / 2 in, and the caret follows it.
        const int start = multiline ? p->view.text.x + (p->view.text.w - 16) / 2 : p->bounds.x + p->textField.editor.x + (p->textField.editor.w - 16) / 2;
        const auto rect = p->caretRect(caret);
        check(rect.x == start + 16, "The input method's anchor is where the caret is drawn");
        check(rect.h == 36 && rect.y == (multiline ? p->view.text.y : p->bounds.y + p->textField.editor.y), "A full spaced row tall, at the top of the line");
    }
}

void aRunTimeChangeReflowsTheNextFrame() {
    Ui ui;
    auto handle = *ui.addLabel(label("l", u"abcd"));
    Laid before(ui, handle);
    check(before.frame.find(handle)->lines[0].bounds.x == 0, "Left at first");
    check(bool(handle.setTextAlign(TextAlign::right)), "Change the alignment");
    Laid after(ui, handle);
    check(after.frame.find(handle)->lines[0].bounds.x == 168, "The next arrange is right-aligned");
}
}

int main() {
    const test::Case cases[] = {
        {"a label aligns each line inside its own width", aLabelAlignsEachLineInsideItsOwnWidth},
        {"line spacing multiplies the font line height", lineSpacingMultipliesTheFontLineHeightAndCentersTheGlyphs},
        {"paragraph spacing adds space only after an LF", paragraphSpacingAddsSpaceOnlyAfterALineFeed},
        {"out-of-range values are invalid and change nothing", outOfRangeValuesAreInvalidAndChangeNothing},
        {"a scrollable label steps one actual row", aScrollableLabelStepsOneActualRow},
        {"a single-line field places short text by its alignment", aSingleLineFieldPlacesShortTextByItsAlignment},
        {"an empty centered field puts the caret in the middle", anEmptyCenteredFieldPutsTheCaretInTheMiddle},
        {"text wider than the field scrolls whatever the alignment", textWiderThanTheFieldScrollsWhateverTheAlignment},
        {"line spacing makes a single-line field taller", lineSpacingMakesASingleLineFieldTallerAndCentersTheText},
        {"a multi-line field aligns every display line", aMultiLineFieldAlignsEveryDisplayLine},
        {"vertical keys after a property change relay out", verticalKeysAfterAPropertyChangeDoNotUseTheOldLines},
        {"pointer hits follow the drawn position",pointerHitsFollowTheDrawnPositionOfAnAlignedLine},
        {"vertical keys keep the drawn x of aligned lines", verticalKeysKeepTheDrawnHorizontalPositionOfAlignedLines},
        {"multi-line height counts spaced rows and gaps", multiLineHeightCountsTheSpacedRowsAndParagraphGaps},
        {"wrapped lines add no paragraph gap in a field", softWrappedLinesAddNoParagraphGapInAField},
        {"a click between rows belongs to the row above", aClickBetweenRowsBelongsToTheRowAbove},
        {"the semantic layer matches the typed handles", theSemanticLayerReadsAndWritesTheThreePropertiesLikeTheHandles},
        {"every subject aligns in every appearance and density", everySubjectAlignsInEveryAppearanceAndDensity},
        {"a spaced field scrolls whole lines; page keys use the visible count", scrollingASpacedFieldMovesWholeLinesAndPageKeysUseTheVisibleCount},
        {"a larger text size scales the row proportionally", aLargerTextSizeScalesTheRowOfAFieldProportionally},
        {"dragging selects by the drawn position", draggingSelectsByTheDrawnPositionOfAlignedLines},
        {"the input method anchors at the drawn caret", theInputMethodAnchorsAtTheDrawnCaretOfAnAlignedField},
        {"a run-time change reflows the next frame", aRunTimeChangeReflowsTheNextFrame},
    };
    return test::run("text_flow", cases);
}
