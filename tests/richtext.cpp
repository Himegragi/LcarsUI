#include "core_internal.hpp"
// #81 rich text through the public core seams: the value type, typed editing, shortcuts, clipboard,
// data connections and the semantic layer. Expected values are worked out by hand in each case.
#include "check.hpp"
#include "component_type.hpp"
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

// ---- Keyboard ------------------------------------------------------------------------------
KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
KeyPress press(Key key, bool shift = false) { return {.key = key, .shift = shift}; }
Spans spans_of(TextHandle& field) { return field.rich_value()->spans; }

void ctrl_b_toggles_the_selection_and_all_three_shortcuts_work() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"重要通知內容"));
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 0, 2);
    check(bool(host.key_down(ctrl(u'B'))) && spans_of(field) == Spans{bold(0, 2)}, "Ctrl+B bolds the selection");
    check(bool(host.key_down(ctrl(u'B'))) && spans_of(field).empty(), "A second Ctrl+B removes it");
    host.select("note", 0, 2);
    host.key_down(ctrl(u'I')); host.key_down(ctrl(u'U'));
    check(spans_of(field) == Spans{{.start = 0, .length = 2, .italic = true, .underline = true}}, "Ctrl+I and Ctrl+U apply italic and underline");
    check(field.rich_value()->text == u"重要通知內容" && value_of(ui, "note") == u"重要通知內容", "Styling never inserts characters");
}

void a_partly_styled_selection_becomes_fully_styled_first() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abcdef"));
    auto& host = detail::access(ui);
    field.set_style(0, 3, {.bold = true});
    host.focus("note");
    host.select("note", 1, 5);
    host.key_down(ctrl(u'B'));
    check(spans_of(field) == Spans{bold(0, 5)}, "Half bold, half plain: Ctrl+B makes all of it bold");
    host.key_down(ctrl(u'B'));
    check(spans_of(field) == Spans{bold(0, 1)}, "When all of the selection is bold, Ctrl+B removes bold from it");
    field.set_value(u"a\nb");
    field.set_style(0, 1, {.bold = true});
    field.set_style(2, 3, {.bold = true});
    host.select("note", 0, 3);
    host.key_down(ctrl(u'B'));
    check(spans_of(field).empty(), "A line feed between bold lines does not count as unstyled, so Ctrl+B removes bold from both");
}

void without_a_selection_ctrl_b_changes_the_next_typed_text_only() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 2, 2);
    host.key_down(ctrl(u'B'));
    check(spans_of(field).empty() && field.typing_style().value->bold, "Ctrl+B at a caret styles nothing yet but sets the typing style");
    host.text_input(u"c");
    check(spans_of(field) == Spans{bold(2, 1)}, "The next typed character is bold");
    host.text_input(u"d");
    check(spans_of(field) == Spans{bold(2, 2)}, "The following ones follow the previous character, so they stay bold");
    host.select("note", 0, 0);
    check(!field.typing_style().value->bold, "At the start of a plain paragraph the typing style is plain");
    host.key_down(ctrl(u'B'));
    host.select("note", 1, 1);
    host.text_input(u"x");
    check(spans_of(field) == Spans{bold(3, 2)}, "Moving the caret cancels the temporary typing style");
    host.select("note", 1, 1);
    host.key_down(ctrl(u'B')); host.key_down(ctrl(u'B'));
    host.text_input(u"y");
    check(spans_of(field) == Spans{bold(4, 2)}, "Pressing Ctrl+B twice leaves the style as it was");
}

void typed_text_follows_the_previous_character_or_the_next_at_a_paragraph_start() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    field.set_style(0, 1, {.bold = true});
    host.select("note", 1, 1); host.text_input(u"x");
    check(spans_of(field) == Spans{bold(0, 2)}, "After a bold character, typing is bold");
    host.select("note", 3, 3); host.text_input(u"y");
    check(spans_of(field) == Spans{bold(0, 2)}, "After a plain character, typing is plain");
    field.set_rich_value({u"ab\ncd", {bold(0, 2), italic(3, 2)}});
    host.select("note", 0, 0); host.text_input(u"1");
    check(spans_of(field) == Spans{bold(0, 3), italic(4, 2)}, "At the start of a paragraph, typing follows the next character");
    field.set_rich_value({u"ab\ncd", {bold(0, 2), italic(3, 2)}});
    host.select("note", 3, 3); host.text_input(u"2");
    check(spans_of(field) == Spans{bold(0, 2), italic(3, 3)}, "The second paragraph's start follows its own first character");
    field.set_rich_value({u"ab\n\ncd", {bold(0, 2)}});
    host.select("note", 3, 3); host.text_input(u"3");
    check(spans_of(field) == Spans{bold(0, 2)}, "An empty paragraph has no neighbor to follow, so it types plain");
    field.set_rich_value({u"abcd", {bold(0, 4)}});
    host.select("note", 1, 3); host.text_input(u"z");
    check(spans_of(field) == Spans{bold(0, 3)}, "Replacing a selection follows the character before it");
    field.set_rich_value({u"abcd", {bold(1, 3)}});
    host.select("note", 0, 2); host.text_input(u"z");
    check(spans_of(field) == Spans{bold(0, 3)}, "Replacing from the paragraph start follows the first character after the selection");
}

void each_styled_insertion_is_one_undo_entry() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"ab"));
    auto& host = detail::access(ui);
    host.focus("note");
    host.select("note", 2, 2);
    host.key_down(ctrl(u'B'));
    host.text_input(u"c");
    check(spans_of(field) == Spans{bold(2, 1)}, "A bold character");
    host.key_down(ctrl(u'Z'));
    check(field.rich_value()->text == u"ab" && spans_of(field).empty(), "One undo removes the character and its style together");
    host.key_down(ctrl(u'Y'));
    check(field.rich_value()->text == u"abc" && spans_of(field) == Spans{bold(2, 1)}, "Redo restores both");
    host.select("note", 0, 1);
    host.key_down(ctrl(u'I'));
    host.key_down(ctrl(u'Z'));
    check(spans_of(field) == Spans{bold(2, 1)} && field.rich_value()->text == u"abc", "One Ctrl+I is one undo entry");
}

void the_field_takes_ctrl_b_before_an_application_shortcut() {
    Ui ui;
    auto rich = connect_rich(ui, rich_control("note", u"abc"));
    auto plain = *ui.add_text({.id = "plain", .kind = Kind::text, .value = u"abc", .multiline = true});
    auto button = *ui.add_button({.id = "bold", .kind = Kind::button, .label = u"Bold"});
    int pressed = 0;
    button.bind([&] { ++pressed; return Result{}; });
    check(bool(ui.add_shortcut({.key = Key::other, .letter = u'B', .ctrl = true}, button)), "An application shortcut on Ctrl+B");
    auto& host = detail::access(ui);
    host.focus("note"); host.select("note", 0, 3);
    host.key_down(ctrl(u'B'));
    check(pressed == 0 && spans_of(rich) == Spans{bold(0, 3)}, "With the rich field focused, the field takes Ctrl+B");
    host.focus("plain");
    host.key_down(ctrl(u'B'));
    check(pressed == 1, "With any other field focused, the application shortcut fires");
    host.focus("bold");
    host.key_down(ctrl(u'B'));
    check(pressed == 2, "With the rich field not focused, the application shortcut fires");
    rich.read_only(true);
    host.focus("note");
    check(host.key_down(ctrl(u'B')).status == Status::read_only && pressed == 2, "A read-only rich field refuses the style but still keeps the key");
}

// ---- Colors --------------------------------------------------------------------------------
void a_color_name_resolves_per_theme_and_unknown_names_keep_their_name() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abc"));
    auto& host = detail::access(ui);
    check(field.set_colors({{"red", {200, 0, 0}, {255, 120, 120}}, {"", {1, 1, 1}, {2, 2, 2}}}).status == Status::invalid_value &&
          field.set_colors({{"red", {200, 0, 0}, {255, 120, 120}}, {"red", {1, 1, 1}, {2, 2, 2}}}).status == Status::invalid_value,
          "An empty or repeated color name is invalid_value");
    check(field.colors()->empty(), "A refused list changes nothing");
    check(bool(field.set_colors({{"red", {200, 0, 0}, {255, 120, 120}}, {"green", {0, 120, 0}, {120, 255, 120}}})), "Set the color list");
    check(field.colors()->size() == 2 && (*field.colors())[1].name == "green", "The list reads back in order");
    field.set_style(0, 2, {.color = "red"});
    field.set_style(2, 3, {.color = "teal"});
    check(host.named_color("note", "red") == Color{200, 0, 0} && host.named_color("note", "green") == Color{0, 120, 0}, "Light theme uses the light values");
    check(!host.named_color("note", "").has_value(), "No name means the normal text color");
    ui.set_presentation({.variant = ThemeVariant::dark()});
    check(host.named_color("note", "red") == Color{255, 120, 120}, "Switching theme recolors by name: dark values");
    check(!host.named_color("note", "teal").has_value(), "A name that is not in the list falls back to the normal color");
    check(field.rich_value()->spans == Spans{colored(0, 2, "red"), colored(2, 1, "teal")}, "The unknown name stays in the value");
    int repaints = 0;
    host.on_invalidate([&] { ++repaints; });
    field.set_colors({{"teal", {0, 128, 128}, {64, 224, 208}}});
    check(repaints == 1 && host.named_color("note", "teal") == Color{64, 224, 208} && !host.named_color("note", "red").has_value(),
          "Replacing the list repaints at once and applies to existing text");
    Ui other;
    auto plain = *other.add_text({.id = "p", .kind = Kind::text, .multiline = true});
    check(plain.set_colors({}).status == Status::not_supported && plain.colors().status == Status::not_supported, "A field that is not rich has no color list");
}

// ---- Data connections ---------------------------------------------------------------------
void external_source_changes_and_conflicts_cover_styles() {
    Ui ui;
    auto model = std::make_shared<RichModel>();
    auto field = connect_rich(ui, rich_control("note", u"abc"), model);
    model->value = {u"xyz", {bold(0, 1)}}; model->notify();
    check(field.rich_value()->text == u"xyz" && spans_of(field) == Spans{bold(0, 1)}, "An external change brings its text and styles");
    check(!field.source_conflict().value.value(), "A clean field follows the source without a conflict");
    field.set_style(1, 3, {.italic = true});
    check(!field.source_conflict().value.value(), "A style edit is a local edit, not a conflict");
    model->value = {u"xyz", {bold(0, 2)}}; model->notify();
    check(spans_of(field) == Spans{bold(0, 1), italic(1, 2)} && field.source_conflict().value.value(),
          "A source that changes only styles while the field is edited is a conflict");
    check(bool(field.use_latest()) && spans_of(field) == Spans{bold(0, 2)} && !field.source_conflict().value.value(),
          "use_latest adopts the source's styles too");
    field.set_style(0, 1, {.bold = false});
    check(bool(field.reset_to_source()) && spans_of(field) == Spans{bold(0, 2)}, "reset_to_source restores text and styles");
    model->value = {u"xyz", {bold(0, 5)}}; model->notify();
    check(spans_of(field) == Spans{bold(0, 2)}, "A source value that is not valid (span past the text) is ignored");
}

void confirmed_and_immediate_writes_carry_the_rich_value() {
    Ui ui;
    auto model = std::make_shared<RichModel>();
    auto field = connect_rich(ui, rich_control("note", u"abc"), model);
    field.set_style(0, 1, {.bold = true});
    check(model->writes == 0, "A confirmed field writes nothing until confirmed");
    check(bool(field.confirm()) && model->writes == 1 && model->last_written == RichText{u"abc", {bold(0, 1)}} && model->last_origin == RichText{u"abc", {}},
          "Confirm writes the value with its styles and the origin before the edit");
    field.set_style(1, 2, {.underline = true});
    field.confirm();
    check(model->last_origin == RichText{u"abc", {bold(0, 1)}}, "The next write's origin is the value last written, styles included");
    auto& tree = ui.semantics();
    check(tree.get({"note"})->verbs.end() != std::find(tree.get({"note"})->verbs.begin(), tree.get({"note"})->verbs.end(), Verb::confirm), "The semantic layer offers confirm");
}

void a_confirmation_group_takes_a_rich_field_in_the_mixed_form() {
    Ui ui;
    auto note_model = std::make_shared<RichModel>();
    auto name_value = std::make_shared<std::u16string>(u"n");
    auto note = *ui.add_text(rich_control("note", u"abc"));
    auto name = *ui.add_text({.id = "name", .kind = Kind::text, .value = u"n"});
    auto button = *ui.add_button({.id = "ok", .kind = Kind::button, .label = u"OK"});
    note_model->value.text = u"abc";
    note.connect_rich_source({[note_model] { return note_model->value; }, [](std::function<void()>) { return [] {}; }});
    name.connect_source({[name_value] { return *name_value; }, [](std::function<void()>) { return [] {}; }});
    check(ui.connect_confirmation(button, {name, note}, [](const std::vector<TextChange>&) { return ConfirmationResult{}; }).status == Status::invalid_value,
          "The plain confirmation group refuses a rich field");
    RichText got, origin; std::u16string got_name; int calls = 0;
    check(bool(ui.connect_mixed_confirmation(button, {name, note}, [&](const std::vector<FieldChange>& changes) {
        ++calls;
        got_name = std::get<TextChange>(changes[0]).value;
        const auto& rich = std::get<RichTextChange>(changes[1]);
        got = rich.value; origin = rich.origin;
        return MixedConfirmationResult{};
    })), "A rich field joins a mixed confirmation group");
    auto& host = detail::access(ui);
    host.focus("note"); host.select("note", 0, 2);
    host.key_down(ctrl(u'B'));
    check(bool(host.invoke("ok")) && calls == 1 && got == RichText{u"abc", {bold(0, 2)}} && origin == RichText{u"abc", {}} && got_name == u"n",
          "The group's submit sees the rich value and origin beside the plain field");
    host.select("note", 2, 3); host.key_down(ctrl(u'I'));
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
    bool write_rich(std::u16string_view text, std::string_view rich) override { content = std::u16string(text); payload = std::string(rich); return true; }
    Query<std::u16string> read() override {
        if (!content) return {Status::not_supported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, content};
    }
    Query<std::string> read_rich() override {
        if (!payload) return {Status::not_supported, u"Clipboard has no rich text", std::nullopt};
        return {Status::ok, {}, payload};
    }
};
struct PlainOnlyClipboard final : Clipboard {  // a host that never learned the private format
    std::optional<std::u16string> content;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override { return {Status::ok, {}, content}; }
};


TextHandle connect_plain(Ui& ui, Control control) {
    auto model = std::make_shared<std::u16string>(control.value);
    auto field = *ui.add_text(std::move(control));
    field.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    field.connect_confirmed([](const TextChange&) { return Result{}; });
    return field;
}

void copy_and_paste_keep_styles_between_rich_fields() {
    Ui ui;
    auto source = connect_rich(ui, rich_control("source", u"abc\ndef"));
    auto target = connect_rich(ui, rich_control("target", u"XY"));
    auto& host = detail::access(ui);
    MemoryClipboard clipboard;
    host.set_clipboard(&clipboard);
    source.set_style(1, 6, {.bold = true});
    source.set_style(4, 7, {.color = "teal"});
    host.focus("source"); host.select("source", 1, 6);
    host.key_down(ctrl(u'C'));
    check(clipboard.content == u"bc\r\nde" && clipboard.payload.has_value(), "Copy writes CRLF plain text and the private payload together");
    check(source.rich_value()->text == u"abc\ndef", "Copy does not change the field");
    host.focus("target"); host.select("target", 1, 1);
    host.key_down(ctrl(u'V'));
    check(target.rich_value()->text == u"Xbc\ndeY", "Paste inserts the text with its line break as LF");
    check(spans_of(target) == Spans{bold(1, 3), {.start = 4, .length = 2, .bold = true, .color = "teal"}}, "Paste keeps the copied styles, color names included, relative to the insertion");
    host.key_down(ctrl(u'Z'));
    check(target.rich_value()->text == u"XY" && spans_of(target).empty(), "One paste is one undo entry");
}

void a_color_name_missing_from_the_target_list_survives_a_paste() {
    Ui ui;
    auto source = connect_rich(ui, rich_control("source", u"abc"));
    auto target = connect_rich(ui, rich_control("target"));
    source.set_colors({{"red", {200, 0, 0}, {255, 120, 120}}});
    source.set_style(0, 3, {.color = "red"});
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.set_clipboard(&clipboard);
    host.focus("source"); host.select("source", 0, 3); host.key_down(ctrl(u'C'));
    host.focus("target"); host.key_down(ctrl(u'V'));
    check(spans_of(target) == Spans{colored(0, 3, "red")} && !host.named_color("target", "red").has_value(),
          "The name is kept in the value; the target (which has no such color) draws it in the normal color");
}

void external_plain_text_takes_the_typing_style() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"ab"));
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.set_clipboard(&clipboard);
    host.focus("note");
    field.set_style(0, 1, {.bold = true});
    clipboard.write(u"xyz");
    host.select("note", 1, 1);
    host.key_down(ctrl(u'V'));
    check(field.rich_value()->text == u"axyzb" && spans_of(field) == Spans{bold(0, 4)}, "Plain text pasted after a bold character is bold");
    host.select("note", 5, 5);
    host.key_down(ctrl(u'V'));
    check(spans_of(field) == Spans{bold(0, 4)}, "Plain text pasted after a plain character is plain");
    host.select("note", 5, 5);
    host.key_down(ctrl(u'B'));
    host.key_down(ctrl(u'V'));
    check(spans_of(field) == Spans{bold(0, 4), bold(5, 3)}, "A pending Ctrl+B styles the pasted text");
    clipboard.content = u"Q\r\nR"; clipboard.payload = std::string("stale payload from another time");
    host.select("note", 0, 0);
    host.key_down(ctrl(u'V'));
    check(field.rich_value()->text.substr(0, 4) == u"Q\nRa" && spans_of(field)[0] == bold(0, 7),
          "A private payload that does not belong to the plain text is ignored; the text is pasted with the typing style (CRLF becomes LF)");
}

void other_fields_and_hosts_get_plain_text_only() {
    Ui ui;
    auto source = connect_rich(ui, rich_control("source", u"a\nb"));
    auto multi = connect_plain(ui, {.id = "multi", .kind = Kind::text, .multiline = true, .visible_lines = 2});
    auto single = connect_plain(ui, {.id = "single", .kind = Kind::text});
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.set_clipboard(&clipboard);
    source.set_style(0, 3, {.bold = true});
    host.focus("source"); host.select("source", 0, 3); host.key_down(ctrl(u'C'));
    host.focus("multi"); host.key_down(ctrl(u'V'));
    check(multi.draft().value.value() == u"a\nb", "A plain multi-line field takes the text, with its line feed");
    host.focus("single"); host.key_down(ctrl(u'V'));
    check(single.draft().value.value() == u"a b", "A single-line field takes the text by its own rule (breaks become spaces)");
    check(!multi.rich_value() && multi.rich_value().status == Status::not_supported, "The plain field holds no styles");
    PlainOnlyClipboard plain_host;
    host.set_clipboard(&plain_host);
    host.focus("source"); host.select("source", 0, 1);
    host.key_down(ctrl(u'C'));
    check(plain_host.content == u"a", "A host that only knows plain text receives plain text");
    host.focus("single"); host.select("single", 0, 3);
    host.key_down(ctrl(u'V'));
    check(single.draft().value.value() == u"a", "A plain-only clipboard still pastes into a single-line field as before");
}

void cut_removes_styled_text_and_undo_restores_it() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abcd"));
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.set_clipboard(&clipboard);
    field.set_style(0, 2, {.bold = true});
    host.focus("note"); host.select("note", 0, 2);
    host.key_down(ctrl(u'X'));
    check(field.rich_value()->text == u"cd" && spans_of(field).empty() && clipboard.content == u"ab" && clipboard.payload.has_value(), "Cut removes the text and copies it with its styles");
    host.key_down(ctrl(u'Z'));
    check(field.rich_value()->text == u"abcd" && spans_of(field) == Spans{bold(0, 2)}, "Undo brings the styled text back");
}

void a_rich_paste_over_the_limit_is_refused_whole() {
    Ui ui;
    auto source = connect_rich(ui, rich_control("source", u"abcdef"));
    auto limited = rich_control("target", u"XY");
    limited.max_length = 4;
    auto target = connect_rich(ui, limited);
    auto& host = detail::access(ui);
    MemoryClipboard clipboard; host.set_clipboard(&clipboard);
    source.set_style(0, 6, {.bold = true});
    host.focus("source"); host.select("source", 0, 6); host.key_down(ctrl(u'C'));
    host.focus("target"); host.select("target", 2, 2);
    check(host.key_down(ctrl(u'V')).status == Status::invalid_value && target.rich_value()->text == u"XY" && spans_of(target).empty(),
          "A paste past max_length leaves text and styles untouched");
}

// ---- Semantic layer ------------------------------------------------------------------------
void the_semantic_layer_reads_and_writes_styles_like_the_typed_calls() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abcdef"));
    auto& tree = ui.semantics();
    auto node = tree.get({"note"});
    check(bool(node) && node->rich && node->styles.empty() && node->typing_style.has_value() && node->typing_style->plain(), "A rich node reports its styles and typing style");
    auto has = [&](Verb verb) { auto n = tree.get({"note"}); return std::find(n->verbs.begin(), n->verbs.end(), verb) != n->verbs.end(); };
    check(has(Verb::set_style) && has(Verb::set_typing_style) && has(Verb::set_value), "The style verbs are offered to an editable rich field");
    check(bool(tree.perform({"note"}, Verb::set_style, StyleEdit{1, 3, {.bold = true}})) && tree.get({"note"})->styles == Spans{bold(1, 2)} && spans_of(field) == Spans{bold(1, 2)},
          "set_style applies a range, as TextHandle::set_style does");
    check(tree.perform({"note"}, Verb::set_style, StyleEdit{3, 1, {.bold = true}}).status == field.set_style(3, 1, {.bold = true}).status &&
          tree.perform({"note"}, Verb::set_style, StyleEdit{3, 1, {.bold = true}}).status == Status::invalid_value, "An invalid range gives the typed call's status");
    check(tree.perform({"note"}, Verb::set_style, std::u16string(u"bold")).status == Status::invalid_value, "A wrong argument is invalid_value");
    check(bool(tree.perform({"note"}, Verb::set_value, RichText{u"xy", {italic(0, 1)}})) && tree.get({"note"})->value == u"xy" && tree.get({"note"})->styles == Spans{italic(0, 1)},
          "set_value with a rich value replaces text and styles");
    check(tree.perform({"note"}, Verb::set_value, RichText{u"xy", {italic(0, 1), bold(0, 2)}}).status == Status::invalid_value && tree.get({"note"})->styles == Spans{italic(0, 1)},
          "An overlapping value is invalid_value and changes nothing");
    check(bool(tree.perform({"note"}, Verb::set_value, std::u16string(u"plain"))) && tree.get({"note"})->styles.empty(), "A plain set_value leaves the text unstyled");
    check(bool(tree.perform({"note"}, Verb::set_typing_style, TextStyle{.bold = true})) && tree.get({"note"})->typing_style->bold && field.typing_style().value->bold,
          "The typing style set through the tree is the one the typed call reads");
    auto& host = detail::access(ui);
    host.focus("note"); host.select("note", 5, 5);
    host.select("note", 5, 5);
    check(!tree.get({"note"})->typing_style->bold, "Moving the caret ends the temporary typing style");
    field.read_only(true);
    check(tree.perform({"note"}, Verb::set_style, StyleEdit{0, 1, {.bold = true}}).status == field.set_style(0, 1, {.bold = true}).status, "A read-only field gives the same status through both doors");
    check(!has(Verb::set_style) && !has(Verb::set_typing_style), "A read-only field does not offer the style verbs");
    Ui other;
    auto plain = connect_plain(other, {.id = "p", .kind = Kind::text, .value = u"abc", .multiline = true});
    check(!other.semantics().get({"p"})->rich && other.semantics().perform({"p"}, Verb::set_style, StyleEdit{0, 1, {.bold = true}}).status == Status::not_supported,
          "A plain field is not rich and refuses the style verbs");
}

void style_changes_raise_a_property_event_and_colors_resolve_for_the_theme() {
    Ui ui;
    ui.bind_appearance(&web_appearance());
    auto field = connect_rich(ui, rich_control("note", u"abc"));
    field.set_colors({{"red", {200, 0, 0}, {255, 120, 120}}});
    auto& tree = ui.semantics();
    std::vector<std::string> properties;
    auto subscription = tree.subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::property_changed && e.ref.id == "note") properties.push_back(e.property); });
    field.set_style(0, 1, {.bold = true});
    tree.dispatch_events();
    check(std::find(properties.begin(), properties.end(), "styles") != properties.end() && std::find(properties.begin(), properties.end(), "value") == properties.end(),
          "A style change raises a 'styles' property event, and no 'value' event (the text did not change)");
    check(tree.text_color({"note"}, "red").value == std::optional<Color>(Color{200, 0, 0}), "A named color resolves to its light value");
    check(tree.text_color({"note"}, "").value == std::optional<Color>(web_appearance().palette().text), "No name resolves to the theme's normal text color");
    check(tree.text_color({"note"}, "nope").value == std::optional<Color>(web_appearance().palette().text), "An unknown name resolves to the normal text color too");
    ui.set_presentation({.variant = ThemeVariant::dark()});
    check(tree.text_color({"note"}, "red").value == std::optional<Color>(Color{255, 120, 120}), "After a theme change it resolves to the dark value");
    check(tree.text_color({"note"}, "").value == std::optional<Color>(web_appearance().palette(ThemeVariant::dark()).text), "...and the normal color to the dark text color");
    Ui other;
    check(connect_plain(other, {.id = "p", .kind = Kind::text, .multiline = true}).describe().value->rich == false && other.semantics().text_color({"p"}, "").status == Status::not_supported,
          "Only rich fields resolve colors");
}

// ---- Layout and painting -------------------------------------------------------------------
// FakeMeasure at 16 px: an ASCII character is 8 px, a bold one 10 px, a line is 24 px tall. A 200 px
// field has a 176 px editor, a 14 px scrollbar reserve and 2 px of caret slack: 160 px of text per line.
struct Laid {
    test::FakeMeasure measure; ScrollState scroll; Frame frame;
    const Placement* field = nullptr;
    Laid(Ui& ui, const TextHandle& handle, int width = 200) {
        frame = arrange(layout::control(handle), ui, web_appearance(), measure, width, 400, 1.f, scroll);
        field = frame.find(handle);
        check(field != nullptr && field->multiline, "The field is placed as a multi-line field");
    }
};
using Lines = std::vector<std::pair<unsigned, unsigned>>;
Lines lines_of(const Placement& p) {
    Lines out;
    for (const auto& line : p.view.lines) out.push_back({line.begin, line.end});
    return out;
}

void wrapping_and_caret_stops_follow_the_width_of_bold_text() {
    Ui plain_ui;
    auto plain = connect_rich(plain_ui, rich_control("note", std::u16string(20, u'a')));
    Laid unstyled(plain_ui, plain);
    check(lines_of(*unstyled.field) == Lines{{0, 20}}, "Twenty plain characters fit one 160 px line");
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", std::u16string(20, u'a')));
    field.set_style(0, 10, {.bold = true});
    Laid laid(ui, field);
    check(lines_of(*laid.field) == Lines{{0, 17}, {17, 20}}, "Ten bold (100 px) and seven plain (56 px) characters fill a line; the rest wraps");
    check(laid.field->view.lines[0].soft, "The wrapped piece continues into the next line");
    const auto& view = laid.field->view;
    check(laid.field->caret_x(5) == view.text.x + 50 && laid.field->caret_x(10) == view.text.x + 100 && laid.field->caret_x(12) == view.text.x + 116,
          "Caret stops add up the bold advances (10 px) and then the plain ones (8 px)");
    check(laid.frame.text_offset(field, view.text.x + 116, view.text.y + 3) == 12 && laid.frame.text_offset(field, view.text.x + 52, view.text.y + 3) == 5,
          "A click lands on the stop nearest to it");
    const auto& pieces = view.lines[0].pieces;
    check(pieces.size() == 2 && pieces[0].end == 10 && pieces[0].style.bold && pieces[0].x == 0 && pieces[0].width == 100 &&
          pieces[1].begin == 10 && pieces[1].end == 17 && !pieces[1].style.bold && pieces[1].x == 100,
          "The line is cut into a bold run and a plain run, with their left edges");
    check(laid.field->bounds.h == unstyled.field->bounds.h, "Styling never changes the field's height");
}

void a_style_change_alone_re_wraps_the_next_arrange() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", std::u16string(20, u'a')));
    Laid before(ui, field);
    field.set_style(0, 20, {.bold = true});
    Laid after(ui, field);
    check(lines_of(*before.field) == Lines{{0, 20}} && lines_of(*after.field) == Lines{{0, 16}, {16, 20}}, "All bold: 16 characters per 160 px line");
    check(detail::access(ui).field_view("note")->lines.size() == 2, "Core's copy of the lines (for the cursor keys) is the new wrapping");
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
    void styled_text(std::u16string_view value, int, FontStyle style, int x, int y, Color color) override { texts.push_back({std::u16string(value), style, x, y, color}); }
};
Recorder paint(Ui& ui, const Placement& field, std::string_view id) {
    auto semantics = *detail::access(ui).describe(id);
    Recorder recorder(detail::access(ui), field, semantics, web_appearance(), 1.f, true);
    detail::component_type(Kind::text).paint(recorder);
    return recorder;
}

void each_run_is_painted_in_its_face_color_and_underline() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abcdef"));
    field.set_colors({{"red", {200, 0, 0}, {255, 120, 120}}});
    field.set_style(0, 2, {.bold = true});
    field.set_style(2, 4, {.italic = true, .underline = true});
    field.set_style(4, 6, {.color = "red"});
    Laid laid(ui, field);
    const auto& view = laid.field->view;
    const auto text_color = detail::access(ui).effective_palette(web_appearance()).text;
    auto recorded = paint(ui, *laid.field, "note");
    check(recorded.texts.size() == 3, "Three runs, three draw calls");
    check(recorded.texts[0].text == u"ab" && recorded.texts[0].style == FontStyle{true, false} && recorded.texts[0].x == view.text.x && recorded.texts[0].y == view.text.y && recorded.texts[0].color == text_color,
          "The bold run, at the left edge, in the normal text color");
    check(recorded.texts[1].text == u"cd" && recorded.texts[1].style == FontStyle{false, true} && recorded.texts[1].x == view.text.x + 20 && recorded.texts[1].color == text_color,
          "The italic run starts after the 20 px of bold text");
    check(recorded.texts[2].text == u"ef" && recorded.texts[2].style == FontStyle{false, false} && recorded.texts[2].x == view.text.x + 36 && recorded.texts[2].color == Color{200, 0, 0},
          "The red run is in the light value of its name");
    auto underline = std::find_if(recorded.fills.begin(), recorded.fills.end(), [&](const auto& f) { return f.rect == Rect{view.text.x + 20, view.text.y + 20, 16, 1}; });
    check(underline != recorded.fills.end() && underline->color == text_color && std::count_if(recorded.fills.begin(), recorded.fills.end(), [&](const auto& f) { return f.rect.h == 1 && f.rect.x >= view.text.x && f.rect.y == view.text.y + 20; }) == 1,
          "Exactly one underline: 1 px thick, under the italic run only (x 20, 16 px wide, at 5/6 of the line height)");
    ui.set_presentation({.variant = ThemeVariant::dark()});
    Laid dark(ui, field);
    auto recolored = paint(ui, *dark.field, "note");
    check(recolored.texts[2].color == Color{255, 120, 120} && recolored.texts[0].color == detail::access(ui).effective_palette(web_appearance()).text,
          "After a theme change the same name paints in its dark value and plain text in the dark text color");
}

void unknown_color_names_and_disabled_fields_use_the_plain_colors() {
    Ui ui;
    auto field = connect_rich(ui, rich_control("note", u"abc"));
    field.set_style(0, 3, {.color = "missing", .underline = true});
    Laid laid(ui, field);
    const auto palette = detail::access(ui).effective_palette(web_appearance());
    auto recorded = paint(ui, *laid.field, "note");
    check(recorded.texts.size() == 1 && recorded.texts[0].color == palette.text && field.rich_value()->spans[0].color == "missing",
          "A name that is not in the list paints in the normal text color and stays in the value");
    field.set_colors({{"missing", {1, 2, 3}, {4, 5, 6}}});
    check(paint(ui, *Laid(ui, field).field, "note").texts[0].color == Color{1, 2, 3}, "Adding the color to the list recolors the same value");
    field.enable(false);
    auto disabled = paint(ui, *Laid(ui, field).field, "note");
    check(disabled.texts[0].color == palette.muted, "A disabled field paints in the muted color whatever the colors say");
}

void a_rich_field_without_styles_lays_out_like_a_plain_one() {
    Ui rich_ui, plain_ui;
    auto text = std::u16string(u"hello world again and more\n中文字\n") + std::u16string(45, u'x');
    auto rich = connect_rich(rich_ui, rich_control("note", text));
    auto plain = connect_plain(plain_ui, {.id = "note", .kind = Kind::text, .value = text, .multiline = true});
    Laid a(rich_ui, rich), b(plain_ui, plain);
    check(lines_of(*a.field) == lines_of(*b.field) && a.field->bounds == b.field->bounds && a.field->view.thumb == b.field->view.thumb,
          "Wrapping, height and scrollbar match a plain multi-line field");
    for (std::size_t i = 0; i < a.field->view.lines.size(); ++i)
        check(a.field->view.lines[i].run.stops == b.field->view.lines[i].run.stops, "Caret stops match too");
}

const test::Case cases[] = {
    {"normalization_merges_sorts_and_drops_empty_spans", normalization_merges_sorts_and_drops_empty_spans},
    {"normalization_rejects_overlap_and_out_of_range_spans", normalization_rejects_overlap_and_out_of_range_spans},
    {"a_rich_field_is_a_multi_line_option_fixed_at_creation", a_rich_field_is_a_multi_line_option_fixed_at_creation},
    {"rich_values_are_read_and_written_in_normal_form", rich_values_are_read_and_written_in_normal_form},
    {"styles_apply_to_a_range_and_are_one_undo_entry", styles_apply_to_a_range_and_are_one_undo_entry},
    {"read_only_and_unconnected_rich_fields_refuse_style_edits", read_only_and_unconnected_rich_fields_refuse_style_edits},
    {"max_length_counts_plain_characters_only", max_length_counts_plain_characters_only},
    {"ctrl_b_toggles_the_selection_and_all_three_shortcuts_work", ctrl_b_toggles_the_selection_and_all_three_shortcuts_work},
    {"a_partly_styled_selection_becomes_fully_styled_first", a_partly_styled_selection_becomes_fully_styled_first},
    {"without_a_selection_ctrl_b_changes_the_next_typed_text_only", without_a_selection_ctrl_b_changes_the_next_typed_text_only},
    {"typed_text_follows_the_previous_character_or_the_next_at_a_paragraph_start", typed_text_follows_the_previous_character_or_the_next_at_a_paragraph_start},
    {"each_styled_insertion_is_one_undo_entry", each_styled_insertion_is_one_undo_entry},
    {"the_field_takes_ctrl_b_before_an_application_shortcut", the_field_takes_ctrl_b_before_an_application_shortcut},
    {"a_color_name_resolves_per_theme_and_unknown_names_keep_their_name", a_color_name_resolves_per_theme_and_unknown_names_keep_their_name},
    {"external_source_changes_and_conflicts_cover_styles", external_source_changes_and_conflicts_cover_styles},
    {"confirmed_and_immediate_writes_carry_the_rich_value", confirmed_and_immediate_writes_carry_the_rich_value},
    {"a_confirmation_group_takes_a_rich_field_in_the_mixed_form", a_confirmation_group_takes_a_rich_field_in_the_mixed_form},
    {"copy_and_paste_keep_styles_between_rich_fields", copy_and_paste_keep_styles_between_rich_fields},
    {"a_color_name_missing_from_the_target_list_survives_a_paste", a_color_name_missing_from_the_target_list_survives_a_paste},
    {"external_plain_text_takes_the_typing_style", external_plain_text_takes_the_typing_style},
    {"other_fields_and_hosts_get_plain_text_only", other_fields_and_hosts_get_plain_text_only},
    {"cut_removes_styled_text_and_undo_restores_it", cut_removes_styled_text_and_undo_restores_it},
    {"a_rich_paste_over_the_limit_is_refused_whole", a_rich_paste_over_the_limit_is_refused_whole},
    {"the_semantic_layer_reads_and_writes_styles_like_the_typed_calls", the_semantic_layer_reads_and_writes_styles_like_the_typed_calls},
    {"style_changes_raise_a_property_event_and_colors_resolve_for_the_theme", style_changes_raise_a_property_event_and_colors_resolve_for_the_theme},
    {"wrapping_and_caret_stops_follow_the_width_of_bold_text", wrapping_and_caret_stops_follow_the_width_of_bold_text},
    {"a_style_change_alone_re_wraps_the_next_arrange", a_style_change_alone_re_wraps_the_next_arrange},
    {"each_run_is_painted_in_its_face_color_and_underline", each_run_is_painted_in_its_face_color_and_underline},
    {"unknown_color_names_and_disabled_fields_use_the_plain_colors", unknown_color_names_and_disabled_fields_use_the_plain_colors},
    {"a_rich_field_without_styles_lays_out_like_a_plain_one", a_rich_field_without_styles_lays_out_like_a_plain_one},
};
}

int main() { return test::run("richtext", cases); }
