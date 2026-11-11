#include "core_internal.hpp"
// #80 text alignment, line spacing and paragraph spacing through the public core, semantic and layout
// seams, with a deterministic fake text backend. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include <limits>
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
        {"a run-time change reflows the next frame", a_run_time_change_reflows_the_next_frame},
    };
    return test::run("text_flow", cases);
}
