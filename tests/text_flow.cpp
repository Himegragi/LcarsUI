#include "core_internal.hpp"
// #80 text alignment, line spacing and paragraph spacing through the public core, semantic and layout
// seams, with a deterministic fake text backend. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
using namespace lcars;
using test::check;
using test::FakeMeasure;

// FakeMeasure at 16 px text: an ASCII character is 8 px wide and a line is 24 px tall.
namespace {
struct Laid {
    FakeMeasure measure; ScrollState scroll; Frame frame;
    template<class Handle> Laid(Ui& ui, const Handle& handle, int width = 200, float scale = 1.f, const Appearance& appearance = web_appearance()) {
        frame = arrange(layout::control(handle), ui, appearance, measure, width, 400, scale, scroll);
    }
};
Control label(std::string id, std::u16string value, TextAlign align = TextAlign::left, double spacing = 1.0, int paragraph = 0) {
    return {.id = std::move(id), .kind = Kind::label, .value = std::move(value), .text_align = align, .line_spacing = spacing, .paragraph_spacing = paragraph};
}

void a_label_aligns_each_line_inside_its_own_width() {
    struct Case { TextAlign align; int first, second; };
    // "abcd" is 32 px and "ab" 16 px wide in a 200 px label.
    for (const auto& c : {Case{TextAlign::left, 0, 0}, Case{TextAlign::center, 84, 92}, Case{TextAlign::right, 168, 184}}) {
        Ui ui;
        auto handle = *ui.add_label(label("l", u"abcd\nab", c.align));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p && p->lines.size() == 2, "Two lines");
        check(p->lines[0].bounds.x == c.first && p->lines[1].bounds.x == c.second, "Each line is aligned on its own width");
        check(p->bounds.x == 0 && p->bounds.w == 200 && p->bounds.h == 48, "The label's own bounds never change");
        check(p->lines[0].bounds.y == 0 && p->lines[1].bounds.y == 24, "Lines keep their rows");
    }
}

void line_spacing_multiplies_the_font_line_height_and_centers_the_glyphs() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"ab\ncd", TextAlign::left, 1.5));
    {
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        // 16 px text: the font line is 24, a row 36, the spare 12 splits 6 above and 6 below.
        check(p->lines[0].bounds.y == 6 && p->lines[1].bounds.y == 42 && p->lines[0].bounds.h == 24, "Rows are 1.5 times the line and the glyphs sit in the middle");
        check(p->bounds.h == 72, "The label is two full rows tall");
    }
    check(bool(ui.set_presentation({.text_size = 28})), "Raise the text size");
    {
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        // 28 px text: the font line is 42, a row 63; (63 - 42) / 2 = 10 above.
        check(p->lines[0].bounds.y == 10 && p->lines[1].bounds.y == 73 && p->bounds.h == 126, "Spacing follows the text size proportionally");
    }
    check(bool(ui.set_presentation({.text_size = 28, .density = Density::compact})), "Switch to compact");
    Laid compact(ui, handle);
    check(compact.frame.find(handle)->bounds.h == 126, "Density does not change the line spacing");
}

void paragraph_spacing_adds_space_only_after_a_line_feed() {
    Ui ui;
    // 100 px wide at 16 px text: 12 characters per line, so 20 characters wrap into 12 + 8.
    auto handle = *ui.add_label(label("l", u"aaaaaaaaaaaaaaaaaaaa\nb", TextAlign::left, 1.0, 8));
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
    auto small = *other.add_label(label("l", u"a\nb", TextAlign::left, 1.0, 8));
    check(bool(other.set_presentation({.text_size = 28})), "Raise the text size");
    Laid big(other, small);
    check(big.frame.find(small)->lines[1].bounds.y == 42 + 8, "The gap stays 8 DIP when the text size changes");
    check(bool(other.set_presentation({.text_size = 28, .density = Density::compact})), "Compact density");
    Laid compact(other, small);
    check(compact.frame.find(small)->lines[1].bounds.y == 42 + 8, "Density does not change the gap");
}

void out_of_range_values_are_invalid_and_change_nothing() {
    Ui ui;
    for (double bad : {0.9, 3.1, -1.0, std::numeric_limits<double>::quiet_NaN()})
        check(ui.add_label(label("bad", u"x", TextAlign::left, bad)).status == Status::invalid_value, "A label is not created with a bad line spacing");
    for (int bad : {-1, 49})
        check(ui.add_label(label("bad", u"x", TextAlign::left, 1.0, bad)).status == Status::invalid_value, "A label is not created with a bad paragraph spacing");
    check(ui.add_text({.id = "bad", .kind = Kind::text, .line_spacing = 3.1}).status == Status::invalid_value, "Nor a text field");
    check(ui.add_button({.id = "b", .kind = Kind::button, .text_align = TextAlign::center}).status == Status::invalid_value, "A button has no alignment");
    check(ui.add_checkbox({.id = "c", .kind = Kind::checkbox, .line_spacing = 1.5}).status == Status::invalid_value, "A checkbox has no line spacing");
    check(ui.add_progress({.id = "p", .kind = Kind::progress, .paragraph_spacing = 8}).status == Status::invalid_value, "A progress bar has no paragraph spacing");
    check(bool(ui.add_button({.id = "ok", .kind = Kind::button})), "Defaults are fine everywhere");

    auto label_handle = *ui.add_label(label("l", u"x"));
    auto field = *ui.add_text({.id = "t", .kind = Kind::text});
    auto exercise = [](auto& h, const char* what) {
        check(*h.text_align() == TextAlign::left && *h.line_spacing() == 1.0 && *h.paragraph_spacing() == 0, what);
        check(h.set_line_spacing(0.9).status == Status::invalid_value && h.set_line_spacing(3.1).status == Status::invalid_value, what);
        check(h.set_line_spacing(std::numeric_limits<double>::quiet_NaN()).status == Status::invalid_value, what);
        check(h.set_paragraph_spacing(-1).status == Status::invalid_value && h.set_paragraph_spacing(49).status == Status::invalid_value, what);
        check(*h.line_spacing() == 1.0 && *h.paragraph_spacing() == 0, "A rejected value leaves the component unchanged");
        check(bool(h.set_line_spacing(1.0)) && bool(h.set_line_spacing(3.0)) && *h.line_spacing() == 3.0, "The limits are valid");
        check(bool(h.set_paragraph_spacing(0)) && bool(h.set_paragraph_spacing(48)) && *h.paragraph_spacing() == 48, "The limits are valid");
        check(bool(h.set_text_align(TextAlign::right)) && *h.text_align() == TextAlign::right, "Alignment is settable");
        auto described = h.describe();
        check(described->text_align == TextAlign::right && described->line_spacing == 3.0 && described->paragraph_spacing == 48, "describe reports the three properties");
    };
    exercise(label_handle, "A label starts with the defaults");
    exercise(field, "A text field starts with the defaults");
}

void a_scrollable_label_steps_one_actual_row() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"1\n2\n3\n4\n5\n6\n7\n8\n9\n10", TextAlign::left, 1.5));
    ScrollState outer, inner;
    auto page = layout::scroll(layout::column({layout::scroll_text(handle, inner, 100)}));
    FakeMeasure measure;
    arrange(page, ui, web_appearance(), measure, 300, 400, 1.f, outer);
    auto& host = detail::access(ui);
    check(bool(host.focus("l")), "The overflowing label takes focus");
    check(bool(host.key_down({.key = Key::down})) && inner.offset == 36, "Down scrolls by one row: 24 * 1.5");
    check(bool(host.key_down({.key = Key::down})) && inner.offset == 72, "And again");
    check(bool(host.key_down({.key = Key::up})) && inner.offset == 36, "Up scrolls back one row");
    check(bool(handle.set_paragraph_spacing(8)), "Add paragraph spacing");
    arrange(page, ui, web_appearance(), measure, 300, 400, 1.f, outer);
    check(bool(host.key_down({.key = Key::down})) && inner.offset == 72, "A step is still one row, not a paragraph");
}

// A text field, connected so it accepts edits like an application's would.
TextHandle field_of(Ui& ui, Control control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto field = *ui.add_text(std::move(control));
    check(bool(field.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect a source");
    check(bool(field.connect_confirmed([](const TextChange&) { return Result{}; })), "Connect a write");
    return field;
}
Control single(std::string id, std::u16string value, TextAlign align = TextAlign::left, double spacing = 1.0) {
    return {.id = std::move(id), .kind = Kind::text, .value = std::move(value), .text_align = align, .line_spacing = spacing};
}

// The web appearance puts the editor 12 px in and 176 px wide in a 200 px field; the caret keeps 2 px
// of room at the right, so text up to 174 px wide is placed by its alignment and longer text scrolls.
void a_single_line_field_places_short_text_by_its_alignment() {
    struct Case { TextAlign align; int start, end; };
    // "abcd" is 32 px wide: centered it starts (176 - 32) / 2 = 72 px into the editor, right-aligned 174 - 32 = 142.
    for (const auto& c : {Case{TextAlign::left, 12, 44}, Case{TextAlign::center, 84, 116}, Case{TextAlign::right, 154, 186}}) {
        Ui ui;
        auto handle = field_of(ui, single("t", u"abcd", c.align));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p->caret_x(0) == c.start && p->caret_x(4) == c.end, "The text starts and ends where its alignment puts it");
        check(p->bounds.x == 0 && p->bounds.w == 200, "The field itself does not move");
        check(laid.frame.text_offset(handle, c.start - 30) == 0 && laid.frame.text_offset(handle, c.end + 30) == 4, "A click outside the text goes to its nearest end");
        check(laid.frame.text_offset(handle, c.start + 17) == 2 && laid.frame.text_offset(handle, c.start + 9) == 1, "A click inside maps through the drawn position");
    }
}

void an_empty_centered_field_puts_the_caret_in_the_middle() {
    Ui ui;
    auto handle = field_of(ui, single("t", u"", TextAlign::center));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const int middle = p->bounds.x + p->field.editor.x + p->field.editor.w / 2;
    check(p->caret_x(0) == middle && middle == 100, "The caret is in the middle of the editor");
    auto& host = detail::access(ui);
    check(bool(host.focus("t")) && bool(host.text_input(u"ab")), "Type two characters");
    Laid typed(ui, handle);
    check(typed.frame.find(handle)->caret_x(2) == 108 && typed.frame.find(handle)->caret_x(0) == 92, "Typed text grows around the middle");
}

void text_wider_than_the_field_scrolls_whatever_the_alignment() {
    const std::u16string wide(32, u'a');  // 256 px, wider than the 174 px room
    std::vector<int> ends;
    for (auto align : {TextAlign::left, TextAlign::center, TextAlign::right}) {
        Ui ui;
        auto handle = field_of(ui, single("t", wide, align));
        auto& host = detail::access(ui);
        check(bool(host.focus("t")), "Focus the field");
        host.select("t", 32, 32);
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        ends.push_back(p->caret_x(32));
        check(p->caret_x(0) < 12, "The start has scrolled out of view");
    }
    check(ends[0] == 186 && ends[1] == ends[0] && ends[2] == ends[0], "The caret at the end sits at the right edge, as for a left-aligned field");
}

void line_spacing_makes_a_single_line_field_taller_and_centers_the_text() {
    Ui plain_ui, spaced_ui;
    auto plain = field_of(plain_ui, single("t", u"abcd"));
    auto spaced = field_of(spaced_ui, single("t", u"abcd", TextAlign::left, 1.5));
    Laid a(plain_ui, plain), b(spaced_ui, spaced);
    const auto* pa = a.frame.find(plain); const auto* pb = b.frame.find(spaced);
    check(pa->field.editor.h == 24 && pb->field.editor.h == 36, "The editor is one 1.5 row tall");
    check(pb->bounds.h == pa->bounds.h + 12, "The field grows by the spare 12 px");
    check(pb->caret_rect(2).h == 36 && pb->caret_rect(2).y == pb->field.editor.y, "The caret is as tall as the row");
    check(pb->text_top == 6, "The glyphs sit in the middle of the row");
}

Control multi(std::string id, std::u16string value, unsigned lines = 3, TextAlign align = TextAlign::left, double spacing = 1.0, int paragraph = 0) {
    return {.id = std::move(id), .kind = Kind::text, .value = std::move(value), .multiline = true, .visible_lines = lines,
            .text_align = align, .line_spacing = spacing, .paragraph_spacing = paragraph};
}
// A 200 px multi-line field: 176 px editor at x = 12, 14 px scrollbar reserve, so the text area is 162 px
// wide and a line wraps at 160 px (20 characters of 8 px). Centered text starts (162 - w) / 2 in, right-aligned 160 - w.
void a_multi_line_field_aligns_every_display_line() {
    struct Case { TextAlign align; int line0, line1, line2; };
    // Lines "ab" (16 px), "cde" (24 px), "" (0 px).
    for (const auto& c : {Case{TextAlign::left, 12, 12, 12}, Case{TextAlign::center, 85, 81, 93}, Case{TextAlign::right, 156, 148, 172}}) {
        Ui ui;
        auto handle = field_of(ui, multi("t", u"ab\ncde\n", 3, c.align));
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p->multiline && p->view.lines.size() == 3, "Three lines");
        check(p->caret_x(0) == c.line0 && p->caret_x(3) == c.line1 && p->caret_x(7) == c.line2, "Every line starts where its alignment puts it");
        check(p->caret_x(2) == c.line0 + 16 && p->caret_x(6) == c.line1 + 24, "And ends one text width later");
        check(p->bounds.x == 0 && p->bounds.w == 200, "The field itself does not move");
    }
}

void pointer_hits_follow_the_drawn_position_of_an_aligned_line() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"ab\ncde\n", 3, TextAlign::right));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const int y0 = p->view.text.y + 5, y1 = y0 + 24;
    // Line 1 "cde" is drawn from x = 148: stops at 148, 156, 164, 172.
    check(laid.frame.text_offset(handle, 148, y1) == 3 && laid.frame.text_offset(handle, 161, y1) == 5 && laid.frame.text_offset(handle, 190, y1) == 6, "A click maps through the drawn position");
    check(laid.frame.text_offset(handle, 20, y1) == 3, "A click left of the text goes to the line start");
    check(laid.frame.text_offset(handle, 172, y0) == 2 && laid.frame.text_offset(handle, 164, y0) == 1 && laid.frame.text_offset(handle, 150, y0) == 0, "Line 0 starts at x = 156");
}

void vertical_keys_keep_the_drawn_horizontal_position_of_aligned_lines() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"abcdefgh\nab", 3, TextAlign::right));
    auto& host = detail::access(ui);
    Laid first(ui, handle);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 8, 8);
    Laid laid(ui, handle);
    const int x = laid.frame.find(handle)->caret_x(8);   // line 0 is 64 px wide, drawn from 108: the end is at 172
    check(x == 172, "The caret is at the end of the right-aligned line");
    check(bool(host.key_down({.key = Key::down})), "Down");
    Laid below(ui, handle);
    const auto caret = host.edit("t")->selection.caret;
    check(caret == 11 && below.frame.find(handle)->caret_x(caret) == 172, "The caret lands under the same drawn x, at the end of the shorter line");
    check(bool(host.key_down({.key = Key::up})), "Up");
    Laid above(ui, handle);
    check(host.edit("t")->selection.caret == 8 && above.frame.find(handle)->caret_x(8) == 172, "And back");
}

// Code review 2026-11-12 item 9: the lines cursor keys use are the last arrange's. A property change without a
// host relayout in between must make them stale, so the next key asks the host to arrange again.
void vertical_keys_after_a_property_change_do_not_use_the_old_lines() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"abcdefgh\nab"));
    auto& host = detail::access(ui);
    std::optional<Laid> latest;
    int relayouts = 0;
    host.on_relayout([&] { ++relayouts; latest.emplace(ui, handle); });
    Laid first(ui, handle);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 8, 8);
    Laid placed(ui, handle);
    check(bool(handle.set_text_align(TextAlign::right)), "Right-align after the last arrange");
    check(bool(host.key_down({.key = Key::down})), "Down");
    check(relayouts == 1, "The changed alignment made the old lines stale: one relayout");
    // Line 0 is 64 px wide and drawn from 108, so the caret at its end is at x = 172. Line 1 "ab" is drawn from 144
    // (16 px wide, flush right): x = 172 is past its end, so the caret lands at the end of "ab". Up then returns under
    // the same drawn x, at the end of the long line.
    check(host.edit("t")->selection.caret == 11, "Down lands at the end of the shorter line");
    check(bool(host.key_down({.key = Key::up})), "Up");
    check(host.edit("t")->selection.caret == 8, "And back to the end of the long line");
    // The same for every other property that moves lines: each makes the next key relayout exactly once.
    struct Change { const char* name; std::function<Result()> apply; };
    const Change changes[] = {
        {"justify method", [&] { return handle.set_justify_method(JustifyMethod::per_character); }},
        {"line spacing", [&] { return handle.set_line_spacing(1.5); }},
        {"paragraph spacing", [&] { return handle.set_paragraph_spacing(8); }},
        {"semantic text align", [&] { return ui.semantics().perform({"t"}, Verb::set_text_align, TextAlign::center); }},
        {"justify as left", [&] { auto s = ui.presentation(); s.justify_as_left = true; return ui.set_presentation(s); }},
    };
    for (const auto& change : changes) {
        latest.emplace(ui, handle);  // an arrange settles the view, so it is fresh again
        const int before = relayouts;
        check(bool(change.apply()), change.name);
        host.key_down({.key = Key::down});
        check(relayouts == before + 1, (std::string(change.name) + ": a key after the change relays out").c_str());
    }
}

void multi_line_height_counts_the_spaced_rows_and_paragraph_gaps() {
    Ui ui;
    auto plain = field_of(ui, multi("plain", u"a\nb\nc\nd"));
    auto spaced = field_of(ui, multi("spaced", u"a\nb\nc\nd", 3, TextAlign::left, 1.5));
    auto gapped = field_of(ui, multi("gapped", u"a\nb\nc\nd", 3, TextAlign::left, 1.0, 8));
    auto both = field_of(ui, multi("both", u"a\nb\nc\nd", 3, TextAlign::left, 1.5, 8));
    auto editor_h = [&](const TextHandle& h) { Laid laid(ui, h); return laid.frame.find(h)->field.editor.h; };
    check(editor_h(plain) == 72, "Three rows of 24");
    check(editor_h(spaced) == 108, "Three rows of 36");
    check(editor_h(gapped) == 72 + 2 * 8, "Three rows and two paragraph gaps");
    check(editor_h(both) == 108 + 2 * 8, "Spaced rows and gaps together");
    Laid laid(ui, both);
    const auto* p = laid.frame.find(both);
    check(p->view.row_height == 36 && p->view.line_height == 24, "The view keeps the font line and the row apart");
    const int top = p->view.text.y;
    check(p->caret_rect(0).y == top && p->caret_rect(2).y == top + 36 + 8 && p->caret_rect(4).y == top + 2 * (36 + 8), "Each paragraph row starts one row and one gap below the last");
    check(p->caret_rect(2).h == 36, "The caret is as tall as the row");
}

void soft_wrapped_lines_add_no_paragraph_gap_in_a_field() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nb", 3, TextAlign::left, 1.0, 8));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    // 40 characters wrap into 20 + 20; the LF then starts "b" after one gap.
    check(p->view.lines.size() == 3 && p->view.lines[0].soft && !p->view.lines[1].soft, "Two wrapped pieces then a new paragraph");
    const int top = p->view.text.y;
    check(p->caret_rect(0).y == top && p->caret_rect(20).y == top + 24 && p->caret_rect(41).y == top + 48 + 8, "The gap is only after the LF");
}

void a_click_between_rows_belongs_to_the_row_above() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"ab\ncd\nef", 3, TextAlign::left, 1.0, 8));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const int top = p->view.text.y;
    check(laid.frame.text_offset(handle, 12, top + 12) == 0 && laid.frame.text_offset(handle, 12, top + 24 + 3) == 0, "Row 0 and the gap below it");
    check(laid.frame.text_offset(handle, 12, top + 24 + 8 + 2) == 3 && laid.frame.text_offset(handle, 12, top + 2 * 32 + 4) == 6, "The next rows");
}

bool has_verb(const SemanticNode& node, Verb verb) { return std::find(node.verbs.begin(), node.verbs.end(), verb) != node.verbs.end(); }

void the_semantic_layer_reads_and_writes_the_three_properties_like_the_handles() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"x", TextAlign::center, 1.4, 8));
    auto field = field_of(ui, multi("t", u"x", 3, TextAlign::right, 2.0, 16));
    auto button = *ui.add_button({.id = "b", .kind = Kind::button});
    auto& tree = ui.semantics();
    auto node = tree.get({"l"});
    check(node && node->text_align == TextAlign::center && node->line_spacing == 1.4 && node->paragraph_spacing == 8, "A label reports its properties");
    check(has_verb(*node, Verb::set_text_align) && has_verb(*node, Verb::set_line_spacing) && has_verb(*node, Verb::set_paragraph_spacing), "A label offers the three verbs");
    auto text = tree.get({"t"});
    check(text->text_align == TextAlign::right && text->line_spacing == 2.0 && text->paragraph_spacing == 16 && has_verb(*text, Verb::set_text_align), "A text field too");
    check(!has_verb(*tree.get({"b"}), Verb::set_text_align) && !has_verb(*tree.get({"b"}), Verb::set_line_spacing), "A button offers none");
    check(tree.perform({"b"}, Verb::set_text_align, TextAlign::center).status == Status::not_supported, "A button refuses the verb");

    std::vector<std::string> changed;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::property_changed) changed.push_back(e.property); });
    check(bool(tree.perform({"l"}, Verb::set_text_align, TextAlign::left)) && *handle.text_align() == TextAlign::left, "Semantic alignment reaches the typed read");
    check(bool(tree.perform({"l"}, Verb::set_line_spacing, 1.0)) && *handle.line_spacing() == 1.0, "Semantic line spacing reaches the typed read");
    check(bool(tree.perform({"l"}, Verb::set_paragraph_spacing, 0)) && *handle.paragraph_spacing() == 0, "Semantic paragraph spacing reaches the typed read");
    check(bool(field.set_text_align(TextAlign::center)) && tree.get({"t"})->text_align == TextAlign::center, "A typed change reaches the semantic read");
    tree.dispatch_events();
    for (const char* name : {"text_align", "line_spacing", "paragraph_spacing"})
        check(std::count(changed.begin(), changed.end(), name) >= 1, "A change raises property_changed");

    check(tree.perform({"l"}, Verb::set_line_spacing, 3.1).status == field.set_line_spacing(3.1).status, "The same status for an out-of-range line spacing");
    check(tree.perform({"l"}, Verb::set_line_spacing, 0.9).status == Status::invalid_value && handle.line_spacing().value.value() == 1.0, "Out of range, unchanged");
    check(tree.perform({"l"}, Verb::set_paragraph_spacing, 49).status == Status::invalid_value && tree.perform({"l"}, Verb::set_paragraph_spacing, -1).status == Status::invalid_value, "Paragraph spacing limits");
    check(tree.perform({"l"}, Verb::set_text_align, 1).status == Status::invalid_value, "The wrong argument type is invalid");
    check(tree.perform({"l"}, Verb::set_line_spacing, std::u16string(u"2")).status == Status::invalid_value, "As is a string");
    check(tree.perform({"missing"}, Verb::set_text_align, TextAlign::left).status == Status::not_found, "An unknown component is not found");
}

// ---- One generic driver: every text-flow subject x alignment x appearance x density ---------------
// What a subject lays out, read the same way for every kind: where each display line starts, and the
// area the alignment works in (a label's own width, a field's editor or text area). Text is "ab" and
// "cde" at 16 px, so 16 and 24 px wide.
struct Drawn { std::vector<int> starts; int area_x = 0, area_w = 0, room = 0; Rect bounds; };
struct Subject {
    const char* name;
    bool field;      // fields keep a 2 px caret slack at the right
    Drawn (*lay)(Ui&, TextAlign, const Appearance&);
};
Drawn drawn_label(Ui& ui, TextAlign align, const Appearance& look, bool scrollable) {
    auto handle = *ui.add_label(label("subject", u"ab\ncde", align));
    FakeMeasure measure; ScrollState outer, inner;
    auto node = scrollable ? layout::scroll(layout::column({layout::scroll_text(handle, inner, 400)})) : layout::control(handle);
    auto frame = arrange(node, ui, look, measure, 300, 400, 1.f, outer);
    const auto* p = frame.find(handle);
    Drawn out{{}, p->bounds.x, p->bounds.w, p->bounds.w, p->bounds};
    for (const auto& line : p->lines) out.starts.push_back(line.bounds.x);  // frame coordinates already
    return out;
}
Drawn drawn_field(Ui& ui, Control control, const Appearance& look) {
    auto handle = field_of(ui, std::move(control));
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::control(handle), ui, look, measure, 300, 400, 1.f, scroll);
    const auto* p = frame.find(handle);
    Drawn out;
    out.bounds = p->bounds;
    if (p->multiline) {
        out.area_x = p->view.text.x; out.area_w = p->view.text.w;
        for (const auto& line : p->view.lines) out.starts.push_back(p->caret_x(line.begin));
    } else {
        out.area_x = p->bounds.x + p->field.editor.x; out.area_w = p->field.editor.w;
        out.starts.push_back(p->caret_x(0));
    }
    return out;
}
const Subject subjects[] = {
    {"label", false, [](Ui& ui, TextAlign a, const Appearance& l) { return drawn_label(ui, a, l, false); }},
    {"scrollable label", false, [](Ui& ui, TextAlign a, const Appearance& l) { return drawn_label(ui, a, l, true); }},
    {"single-line field", true, [](Ui& ui, TextAlign a, const Appearance& l) { return drawn_field(ui, single("subject", u"cde", a), l); }},
    {"multi-line field", true, [](Ui& ui, TextAlign a, const Appearance& l) { return drawn_field(ui, multi("subject", u"ab\ncde", 3, a), l); }},
};
void every_subject_aligns_in_every_appearance_and_density() {
    for (const auto& subject : subjects) for (const auto* look : {&web_appearance(), &material_appearance()}) for (auto density : {Density::standard, Density::compact}) {
        Ui baseline_ui;
        check(bool(baseline_ui.set_presentation({.density = density})), "Set the density");
        const auto baseline = subject.lay(baseline_ui, TextAlign::left, *look);
        check(!baseline.starts.empty(), subject.name);
        for (auto align : {TextAlign::left, TextAlign::center, TextAlign::right}) {
            Ui ui;
            check(bool(ui.set_presentation({.density = density})), "Set the density");
            const auto drawn = subject.lay(ui, align, *look);
            check(drawn.bounds == baseline.bounds, "The component's own bounds do not depend on the alignment");
            check(drawn.starts.size() == baseline.starts.size(), subject.name);
            const int inset = subject.field ? look->component_presentation(density).inset : 0;
            const int room = drawn.area_w - inset;
            // Hand-computed widths: "ab" 16 px, "cde" 24 px; a single-line field holds only "cde".
            const bool single_line = std::string_view(subject.name) == "single-line field";
            std::vector<int> widths = single_line ? std::vector<int>{24} : std::vector<int>{16, 24};
            for (std::size_t i = 0; i < widths.size(); ++i) {
                // Centered text sits in the middle of the area, right-aligned text ends at its room (the caret slack).
                const int expected = align == TextAlign::left ? drawn.area_x
                    : align == TextAlign::center ? drawn.area_x + (drawn.area_w - widths[i]) / 2 : drawn.area_x + room - widths[i];
                check(std::abs(drawn.starts[i] - expected) <= 1, subject.name);
            }
        }
    }
}

void scrolling_a_spaced_field_moves_whole_lines_and_page_keys_use_the_visible_count() {
    const std::u16string ten = u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9";
    Ui ui;
    auto handle = field_of(ui, multi("t", ten, 3, TextAlign::center, 1.5, 8));
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    const ScrollRegion* region = nullptr;
    for (const auto& r : laid.frame.regions) if (r.field == "t") region = &r;
    check(region && region->step == 36 && region->extent == 10 * 36 && region->maximum == 7 * 36, "The scrollbar counts rows of 36, not the paragraph gaps");
    const int x = p->view.text.x + 10, y = p->view.text.y + 10;
    check(laid.frame.wheel(ui, x, y, -36) && *handle.scroll_line() == 1, "One wheel step of a row scrolls one line");
    check(laid.frame.wheel(ui, x, y, 2 * 36) && *handle.scroll_line() == 0 && !laid.frame.wheel(ui, x, y, 36), "Back up, and no further");
    auto& host = detail::access(ui);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 0, 0);
    check(bool(host.key_down({.key = Key::page_down})) && host.edit("t")->selection.caret == 6, "Page Down moves by the three visible lines");
    Laid after(ui, handle);
    check(*handle.scroll_line() == 3, "The first visible line follows the page");
    check(after.frame.find(handle)->caret_rect(6).y == after.frame.find(handle)->view.text.y, "The caret's line is now the top row");
    check(bool(host.key_down({.key = Key::page_up})) && host.edit("t")->selection.caret == 0, "Page Up goes back");
}

void a_larger_text_size_scales_the_row_of_a_field_proportionally() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"a\nb\nc\nd", 3, TextAlign::left, 1.5));
    {
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        check(p->caret_rect(2).y - p->caret_rect(0).y == 36, "Rows are 24 * 1.5 apart at 16 px");
    }
    check(bool(ui.set_presentation({.text_size = 28})), "Raise the text size");
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    check(p->caret_rect(2).y - p->caret_rect(0).y == 63 && p->view.row_height == 63 && p->field.editor.h == 3 * 63, "Rows are the new line height 42 * 1.5 apart");
    check(bool(ui.set_presentation({.text_size = 28, .density = Density::compact})), "Compact density");
    Laid compact(ui, handle);
    check(compact.frame.find(handle)->view.row_height == 63, "Density leaves the spacing alone");
}

void dragging_selects_by_the_drawn_position_of_aligned_lines() {
    Ui ui;
    auto handle = field_of(ui, multi("t", u"abcd\nef", 3, TextAlign::right));
    auto& host = detail::access(ui);
    check(bool(host.focus("t")), "Focus the field");
    host.select("t", 0, 0);
    Laid laid(ui, handle);
    const auto* p = laid.frame.find(handle);
    // Line 1 "ef" (16 px) is right-aligned: drawn from 12 + 144 = 156, stops at 156, 164, 172.
    check(laid.frame.select_to(ui, handle, 164, p->view.text.y + 24 + 5), "Drag into the second row");
    check(host.edit("t")->selection.anchor == 0 && host.edit("t")->selection.caret == 6, "The caret is one character into the drawn line");
    check(laid.frame.select_to(ui, handle, 20, p->view.text.y + 24 + 5) && host.edit("t")->selection.caret == 5, "Dragging left of the text selects up to its start");
}

void the_input_method_anchors_at_the_drawn_caret_of_an_aligned_field() {
    for (bool multiline : {false, true}) {
        Ui ui;
        auto handle = field_of(ui, multiline ? multi("t", u"", 3, TextAlign::center, 1.5) : single("t", u"", TextAlign::center, 1.5));
        auto& host = detail::access(ui);
        check(bool(host.focus("t")), "Focus the field");
        check(bool(host.composition_update(u"ㄅ", 1)), "Start composing one wide character (16 px)");
        Laid laid(ui, handle);
        const auto* p = laid.frame.find(handle);
        const auto caret = host.edit("t")->visible_caret();
        // The composition is 16 px wide: centered it starts (area - 16) / 2 in, and the caret follows it.
        const int start = multiline ? p->view.text.x + (p->view.text.w - 16) / 2 : p->bounds.x + p->field.editor.x + (p->field.editor.w - 16) / 2;
        const auto rect = p->caret_rect(caret);
        check(rect.x == start + 16, "The input method's anchor is where the caret is drawn");
        check(rect.h == 36 && rect.y == (multiline ? p->view.text.y : p->bounds.y + p->field.editor.y), "A full spaced row tall, at the top of the line");
    }
}

void a_run_time_change_reflows_the_next_frame() {
    Ui ui;
    auto handle = *ui.add_label(label("l", u"abcd"));
    Laid before(ui, handle);
    check(before.frame.find(handle)->lines[0].bounds.x == 0, "Left at first");
    check(bool(handle.set_text_align(TextAlign::right)), "Change the alignment");
    Laid after(ui, handle);
    check(after.frame.find(handle)->lines[0].bounds.x == 168, "The next arrange is right-aligned");
}
}

int main() {
    const test::Case cases[] = {
        {"a label aligns each line inside its own width", a_label_aligns_each_line_inside_its_own_width},
        {"line spacing multiplies the font line height", line_spacing_multiplies_the_font_line_height_and_centers_the_glyphs},
        {"paragraph spacing adds space only after an LF", paragraph_spacing_adds_space_only_after_a_line_feed},
        {"out-of-range values are invalid and change nothing", out_of_range_values_are_invalid_and_change_nothing},
        {"a scrollable label steps one actual row", a_scrollable_label_steps_one_actual_row},
        {"a single-line field places short text by its alignment", a_single_line_field_places_short_text_by_its_alignment},
        {"an empty centered field puts the caret in the middle", an_empty_centered_field_puts_the_caret_in_the_middle},
        {"text wider than the field scrolls whatever the alignment", text_wider_than_the_field_scrolls_whatever_the_alignment},
        {"line spacing makes a single-line field taller", line_spacing_makes_a_single_line_field_taller_and_centers_the_text},
        {"a multi-line field aligns every display line", a_multi_line_field_aligns_every_display_line},
        {"vertical keys after a property change relay out", vertical_keys_after_a_property_change_do_not_use_the_old_lines},
        {"pointer hits follow the drawn position",pointer_hits_follow_the_drawn_position_of_an_aligned_line},
        {"vertical keys keep the drawn x of aligned lines", vertical_keys_keep_the_drawn_horizontal_position_of_aligned_lines},
        {"multi-line height counts spaced rows and gaps", multi_line_height_counts_the_spaced_rows_and_paragraph_gaps},
        {"wrapped lines add no paragraph gap in a field", soft_wrapped_lines_add_no_paragraph_gap_in_a_field},
        {"a click between rows belongs to the row above", a_click_between_rows_belongs_to_the_row_above},
        {"the semantic layer matches the typed handles", the_semantic_layer_reads_and_writes_the_three_properties_like_the_handles},
        {"every subject aligns in every appearance and density", every_subject_aligns_in_every_appearance_and_density},
        {"a spaced field scrolls whole lines; page keys use the visible count", scrolling_a_spaced_field_moves_whole_lines_and_page_keys_use_the_visible_count},
        {"a larger text size scales the row proportionally", a_larger_text_size_scales_the_row_of_a_field_proportionally},
        {"dragging selects by the drawn position", dragging_selects_by_the_drawn_position_of_aligned_lines},
        {"the input method anchors at the drawn caret", the_input_method_anchors_at_the_drawn_caret_of_an_aligned_field},
        {"a run-time change reflows the next frame", a_run_time_change_reflows_the_next_frame},
    };
    return test::run("text_flow", cases);
}
