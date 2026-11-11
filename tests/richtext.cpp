#include "core_internal.hpp"
// #81 rich text through the public core seams: the value type, typed editing, shortcuts, clipboard,
// data connections and the semantic layer. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
using namespace lcars;
using test::check;

namespace {
StyleSpan bold(unsigned start, unsigned length) { return {.start = start, .length = length, .bold = true}; }
StyleSpan italic(unsigned start, unsigned length) { return {.start = start, .length = length, .italic = true}; }
StyleSpan underline(unsigned start, unsigned length) { return {.start = start, .length = length, .underline = true}; }
StyleSpan colored(unsigned start, unsigned length, std::string color) { return {.start = start, .length = length, .color = std::move(color)}; }
using Spans = std::vector<StyleSpan>;

void normalization_merges_sorts_and_drops_empty_spans() {
    auto merged = normalize_rich_text({u"abcdef", {bold(0, 2), bold(2, 2)}});
    check(bool(merged) && merged->spans == Spans{bold(0, 4)}, "Two adjacent bold spans become one");
    auto sorted = normalize_rich_text({u"abcdef", {italic(4, 2), bold(0, 2)}});
    check(bool(sorted) && sorted->spans == Spans{bold(0, 2), italic(4, 2)}, "Spans are sorted by start");
    auto apart = normalize_rich_text({u"abcdef", {bold(0, 2), bold(3, 2)}});
    check(bool(apart) && apart->spans == Spans{bold(0, 2), bold(3, 2)}, "A gap keeps two spans");
    auto different = normalize_rich_text({u"abcdef", {bold(0, 2), italic(2, 2)}});
    check(bool(different) && different->spans == Spans{bold(0, 2), italic(2, 2)}, "Adjacent spans of different style stay apart");
    auto empty = normalize_rich_text({u"abc", {bold(1, 0), bold(0, 0)}});
    check(bool(empty) && empty->spans.empty(), "Zero-length spans are removed");
    auto plain = normalize_rich_text({u"abc", {{.start = 0, .length = 3}}});
    check(bool(plain) && plain->spans.empty(), "A span of plain style says nothing and is removed");
    auto colors = normalize_rich_text({u"abcd", {colored(0, 2, "red"), colored(2, 2, "red")}});
    check(bool(colors) && colors->spans == Spans{colored(0, 4, "red")}, "Equal color names merge");
    auto names = normalize_rich_text({u"abcd", {colored(0, 2, "red"), colored(2, 2, "green")}});
    check(bool(names) && names->spans.size() == 2, "Different color names do not merge");
    auto touching = normalize_rich_text({u"abcd", {{.start = 0, .length = 2, .bold = true, .underline = true}, {.start = 2, .length = 2, .bold = true, .underline = true}}});
    check(bool(touching) && touching->spans.size() == 1 && touching->spans[0].length == 4, "Combined styles merge as one style");
    check(normalize_rich_text({u"abcdef", {bold(0, 2), bold(2, 2)}})->spans == normalize_rich_text({u"abcdef", {bold(0, 4)}})->spans,
          "The same content always reads back as the same value");
}

void normalization_rejects_overlap_and_out_of_range_spans() {
    check(normalize_rich_text({u"abcdef", {bold(0, 3), italic(2, 2)}}).status == Status::invalid_value, "Overlapping spans are invalid");
    check(normalize_rich_text({u"abcdef", {bold(0, 3), bold(2, 2)}}).status == Status::invalid_value, "Overlap is invalid even when the styles are equal");
    check(normalize_rich_text({u"abc", {bold(1, 3)}}).status == Status::invalid_value, "A span past the end is invalid");
    check(normalize_rich_text({u"abc", {bold(4, 0)}}).status == Status::invalid_value, "Even an empty span must start inside the text");
    check(bool(normalize_rich_text({u"abc", {bold(3, 0)}})), "An empty span at the end is inside the text");
    check(normalize_rich_text({u"a\U0001F600b", {bold(2, 1)}}).status == Status::invalid_value, "A span may not start inside a surrogate pair");
    check(normalize_rich_text({u"a\U0001F600b", {bold(0, 2)}}).status == Status::invalid_value, "A span may not end inside a surrogate pair");
    check(bool(normalize_rich_text({u"a\U0001F600b", {bold(1, 2)}})), "A span may cover a whole pair");
    check(normalize_rich_text({u"a\rb", {}}).status == Status::invalid_value, "The text follows the multi-line rule (no CR)");
    check(normalize_rich_text({u"abc", {}}, 2).status == Status::invalid_value, "The text respects the length limit");
    check(bool(normalize_rich_text({u"a\nb", {bold(0, 3)}})), "A span may cover a line feed");
}

// A rich field with an explicit write timing, as an application would have it.
struct RichModel {
    RichText value;
    std::function<void()> notify;
    int writes = 0;
    RichText last_written, last_origin;
};
Control rich_control(std::string id, std::u16string value = {}, unsigned lines = 3) {
    return {.id = std::move(id), .kind = Kind::text, .value = std::move(value), .multiline = true, .visible_lines = lines, .rich = true};
}
TextHandle connect_rich(Ui& ui, Control control, std::shared_ptr<RichModel> model = {}, bool immediate = false) {
    if (!model) model = std::make_shared<RichModel>();
    model->value.text = control.value;
    auto query = ui.add_text(std::move(control));
    check(bool(query), "Create the rich field");
    auto field = *query;
    check(bool(field.connect_rich_source({[model] { return model->value; }, [model](std::function<void()> cb) { model->notify = cb; return [] {}; }})), "Connect a rich source");
    auto write = [model](const RichTextChange& change) { ++model->writes; model->last_written = change.value; model->last_origin = change.origin; return Result{}; };
    check(bool(immediate ? field.connect_rich_immediate(write) : field.connect_rich_confirmed(write)), "Connect a rich write");
    return field;
}
std::u16string value_of(Ui& ui, std::string_view id) { return detail::access(ui).text(id); }

void a_rich_field_is_a_multi_line_option_fixed_at_creation() {
    Ui ui;
    check(bool(ui.add_text(rich_control("rich"))), "A multi-line field can be rich");
    check(ui.add_text({.id = "single", .kind = Kind::text, .rich = true}).status == Status::invalid_value, "A single-line field cannot be rich");
}

void rich_values_are_read_and_written_in_normal_form() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note"));
    check(bool(field.set_rich_value({u"abcd", {bold(2, 1), bold(0, 2)}})), "Write a rich value");
    auto value = field.rich_value();
    check(bool(value) && value->text == u"abcd" && value->spans == Spans{bold(0, 3)}, "The value reads back normalized");
    check(field.draft().value.value() == u"abcd" && value_of(ui, "note") == u"abcd", "The plain text is the draft and the semantic value");
    check(field.set_rich_value({u"xyz", {bold(0, 2), italic(1, 2)}}).status == Status::invalid_value && field.rich_value()->text == u"abcd",
          "Overlapping spans are refused and the field is unchanged");
    check(field.set_rich_value({u"xyz", {bold(2, 2)}}).status == Status::invalid_value && field.rich_value()->spans == Spans{bold(0, 3)},
          "Spans past the text are refused and the field is unchanged");
    check(bool(field.set_value(u"plain")) && field.rich_value()->text == u"plain" && field.rich_value()->spans.empty(),
          "set_value on a rich field replaces the text and leaves it unstyled");
    Ui other;
    auto plain = *other.add_text({.id = "p", .kind = Kind::text, .multiline = true});
    check(plain.rich_value().status == Status::not_supported && plain.set_rich_value({u"x", {}}).status == Status::not_supported &&
          plain.set_style(0, 0, {.bold = true}).status == Status::not_supported && plain.typing_style().status == Status::not_supported,
          "A field that is not rich answers not_supported");
    check(field.connect_source({[] { return std::u16string(); }, [](std::function<void()>) { return [] {}; }}).status == Status::not_supported,
          "A rich field refuses a plain source");
    check(plain.connect_rich_source({[] { return RichText{}; }, [](std::function<void()>) { return [] {}; }}).status == Status::not_supported,
          "A plain field refuses a rich source");
}

void styles_apply_to_a_range_and_are_one_undo_entry() {
    Ui ui;
    auto model = std::make_shared<RichModel>();
    auto field = connect_rich(ui, rich_control("note", u"重要通知內容"), model, true);
    auto spans = [&] { return field.rich_value()->spans; };
    check(bool(field.set_style(0, 2, {.bold = true})) && spans() == Spans{bold(0, 2)}, "Bold the first two characters");
    check(model->writes == 1 && model->last_written.spans == Spans{bold(0, 2)} && model->last_origin.spans.empty(), "An immediate write carries the styled value and the origin");
    check(bool(field.set_style(1, 4, {.italic = true})) && spans() == Spans{bold(0, 1), {.start = 1, .length = 1, .bold = true, .italic = true}, italic(2, 2)},
          "Italic over a bold edge combines the styles");
    check(bool(field.set_style(0, 6, {.bold = false, .italic = false})) && spans().empty(), "Removing bold and italic from the whole text leaves no spans");
    check(bool(field.set_style(2, 4, {.color = "red", .underline = true})) && spans() == Spans{{.start = 2, .length = 2, .underline = true, .color = "red"}},
          "Color and underline set together");
    check(bool(field.set_style(2, 3, {.color = ""})) && spans() == Spans{underline(2, 1), {.start = 3, .length = 1, .underline = true, .color = "red"}},
          "An empty color name returns text to the normal color");
    check(field.set_style(3, 2, {.bold = true}).status == Status::invalid_value && field.set_style(0, 7, {.bold = true}).status == Status::invalid_value,
          "A reversed or out-of-range span is invalid_value");
    check(bool(field.set_style(0, 6, {.bold = true})), "Bold everything");
    auto before = spans();
    check(bool(field.undo()) && spans() != before && spans().size() == 2, "One undo reverts one style application");
    check(bool(field.redo()) && spans() == before, "Redo restores it");
    check(field.rich_value()->text == u"重要通知內容", "Styling never changes the text");
}

void read_only_and_unconnected_rich_fields_refuse_style_edits() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abc"));
    field.read_only(true);
    check(field.set_style(0, 1, {.bold = true}).status == Status::read_only && field.set_rich_value({u"x", {}}).status == Status::read_only,
          "A read-only field refuses style edits like any edit");
    auto bare = *ui.add_text(rich_control("bare", u"abc"));
    check(bare.set_style(0, 1, {.bold = true}).status == Status::invalid_value, "A field without a write timing refuses edits");
}

void max_length_counts_plain_characters_only() {
    Ui ui;
    auto control = rich_control("note", u"abcd");
    control.max_length = 4;
    auto field = connect_rich(ui, control);
    check(bool(field.set_style(0, 4, {.bold = true, .color = "a-very-long-color-name"})), "Styling a field at its limit is allowed");
    check(bool(field.set_rich_value({u"wxyz", {bold(0, 4)}})), "A rich value of exactly the limit is allowed");
    check(field.set_rich_value({u"vwxyz", {bold(0, 1)}}).status == Status::invalid_value, "One more character is refused, however it is styled");
}

const test::Case cases[] = {
    {"normalization_merges_sorts_and_drops_empty_spans", normalization_merges_sorts_and_drops_empty_spans},
    {"normalization_rejects_overlap_and_out_of_range_spans", normalization_rejects_overlap_and_out_of_range_spans},
    {"a_rich_field_is_a_multi_line_option_fixed_at_creation", a_rich_field_is_a_multi_line_option_fixed_at_creation},
    {"rich_values_are_read_and_written_in_normal_form", rich_values_are_read_and_written_in_normal_form},
    {"styles_apply_to_a_range_and_are_one_undo_entry", styles_apply_to_a_range_and_are_one_undo_entry},
    {"read_only_and_unconnected_rich_fields_refuse_style_edits", read_only_and_unconnected_rich_fields_refuse_style_edits},
    {"max_length_counts_plain_characters_only", max_length_counts_plain_characters_only},
};
}

int main() { return test::run("richtext", cases); }
