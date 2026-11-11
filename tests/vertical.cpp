#include "vertical.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include "lcars/semantics.hpp"
// Vertical writing for labels (#82), core seam: classification, flow, properties, layout, scrolling.
using namespace lcars;
using test::check;
using detail::segment_vertical;

namespace {
struct Cut { std::u16string text; VerticalForm form; };
std::vector<Cut> cuts(std::u16string_view text, int digits = 3) {
    std::vector<Cut> result;
    for (const auto& segment : segment_vertical(text, digits))
        result.push_back({std::u16string(text.substr(segment.begin, segment.end - segment.begin)), segment.form});
    return result;
}
bool is(const std::vector<Cut>& actual, std::vector<Cut> expected) {
    if (actual.size() != expected.size()) return false;
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i].text != expected[i].text || actual[i].form != expected[i].form) return false;
    return true;
}
using F = VerticalForm;

void han_fullwidth_and_emoji_stay_upright() {
    check(is(cuts(u"設定"), {{u"設", F::upright}, {u"定", F::upright}}), "Han characters are upright, one cell each");
    check(is(cuts(u"ＡＢ"), {{u"Ａ", F::upright}, {u"Ｂ", F::upright}}), "Full-width Latin is upright");
    check(is(cuts(u"😀"), {{u"😀", F::upright}}), "An emoji (surrogate pair) is one upright cell");
}
void latin_words_and_sentences_rotate_as_one_run() {
    check(is(cuts(u"LcarsUI"), {{u"LcarsUI", F::sideways}}), "A Latin word is one sideways run");
    check(is(cuts(u"Hello World"), {{u"Hello World", F::sideways}}), "A Latin sentence is one sideways run");
    check(is(cuts(u"版 A 版"), {{u"版", F::upright}, {u" A ", F::sideways}, {u"版", F::upright}}), "A single letter is sideways, never upright");
    check(is(cuts(u"GDP"), {{u"GDP", F::sideways}}), "An abbreviation is sideways");
}
void digits_follow_the_combine_limit() {
    check(is(cuts(u"第5版"), {{u"第", F::upright}, {u"5", F::upright}, {u"版", F::upright}}), "One digit is upright");
    check(is(cuts(u"第12版"), {{u"第", F::upright}, {u"12", F::combined}, {u"版", F::upright}}), "Two digits are combined into one cell");
    check(is(cuts(u"第123版"), {{u"第", F::upright}, {u"123", F::combined}, {u"版", F::upright}}), "Three digits are combined into one cell");
    check(is(cuts(u"第1234版"), {{u"第", F::upright}, {u"1234", F::sideways}, {u"版", F::upright}}), "Four digits rotate");
    check(is(cuts(u"第123版", 2), {{u"第", F::upright}, {u"123", F::sideways}, {u"版", F::upright}}), "With a limit of 2, three digits rotate");
    check(is(cuts(u"第12版", 0), {{u"第", F::upright}, {u"12", F::sideways}, {u"版", F::upright}}), "With a limit of 0, even two digits rotate");
    check(is(cuts(u"第5版", 0), {{u"第", F::upright}, {u"5", F::upright}, {u"版", F::upright}}), "A single digit stays upright when combining is off");
    check(is(cuts(u"UI12"), {{u"UI12", F::sideways}}), "Digits touching Latin letters stay part of the sideways word");
}
void punctuation_gets_the_taiwan_placement_classes() {
    for (auto mark : {u"，", u"。", u"、", u"；", u"：", u"！", u"？"})
        check(is(cuts(mark), {{mark, F::point}}), "Point punctuation is centered, not rotated");
    for (auto mark : {u"「", u"」", u"『", u"』", u"（", u"）", u"《", u"》", u"〈", u"〉", u"【", u"】", u"〔", u"〕"})
        check(is(cuts(mark), {{mark, F::bracket}}), "Brackets and quotes turn to face up and down");
    for (auto mark : {u"—", u"…", u"─"})
        check(is(cuts(mark), {{mark, F::line}}), "Dashes and ellipses become centered lines");
    for (auto mark : {u"·", u"‧", u"・"})
        check(is(cuts(mark), {{mark, F::middle_dot}}), "Middle dots are centered upright");
    check(is(cuts(u"——"), {{u"—", F::line}, {u"—", F::line}}), "A double dash is two line cells");
}

using detail::flow_vertical;
// FakeMeasure: ASCII is half an em wide, everything else a full em.
std::vector<VerticalColumn> flow(std::u16string_view text, int height, int em = 16, int digits = 3) {
    test::FakeMeasure measure;
    return flow_vertical(measure, text, em, digits, height);
}
std::u16string column_text(std::u16string_view text, const VerticalColumn& column) { return std::u16string(text.substr(column.begin, column.end - column.begin)); }

void a_full_column_wraps_to_the_next_column() {
    std::u16string text = u"一二三四五六七";
    auto columns = flow(text, 64);
    check(columns.size() == 2 && column_text(text, columns[0]) == u"一二三四" && column_text(text, columns[1]) == u"五六七", "Four 16 px cells fill a 64 px column");
    check(columns[0].pieces.size() == 4 && columns[0].pieces[0].y == 0 && columns[0].pieces[3].y == 48 && columns[0].pieces[3].advance == 16, "Cells stack top to bottom");
    check(columns[0].bounds.h == 64 && columns[1].bounds.h == 48, "A column is as long as its content");
}
void a_line_break_starts_a_new_column() {
    std::u16string text = u"一二\n三";
    auto columns = flow(text, 640);
    check(columns.size() == 2 && column_text(text, columns[0]) == u"一二" && column_text(text, columns[1]) == u"三", "The line break ends the column and is not drawn");
    auto blank = flow(u"一\n\n二", 640);
    check(blank.size() == 3 && blank[1].pieces.empty(), "Two line breaks leave an empty column");
    check(flow(u"", 64).empty(), "Empty text has no columns");
    check(flow(u"一\n", 64).size() == 2, "A trailing line break leaves an empty last column, like horizontal text");
}
void a_zero_height_never_wraps() {
    check(flow(u"一二三四五六七八九十", 0).size() == 1, "Height 0 means one column per paragraph");
}
void latin_words_move_whole_and_long_words_are_cut() {
    std::u16string text = u"Hello World";  // 'Hello ' is 6 x 8 = 48 px, 'World' is 5 x 8 = 40 px
    auto columns = flow(text, 64);
    check(columns.size() == 2 && column_text(text, columns[0]) == u"Hello " && column_text(text, columns[1]) == u"World", "A word that does not fit moves to the next column whole");
    check(columns[0].pieces.size() == 1 && columns[0].pieces[0].form == VerticalForm::sideways && columns[0].pieces[0].advance == 48, "A sideways word advances by its text width");
    std::u16string long_word = u"abcdefghijklmnop";  // 16 letters = 128 px
    auto cut = flow(long_word, 64);
    check(cut.size() == 2 && cut[0].bounds.h == 64 && column_text(long_word, cut[0]) == u"abcdefgh", "A word longer than the column is cut where it overflows");
}
void a_digit_group_takes_one_cell() {
    std::u16string text = u"第12版";
    auto columns = flow(text, 640);
    check(columns.size() == 1 && columns[0].pieces.size() == 3 && columns[0].pieces[1].form == VerticalForm::combined &&
          columns[0].pieces[1].advance == 16 && columns[0].pieces[2].y == 32, "Combined digits advance one em, not their width");
}

// ---- properties: construction, typed handle, semantic layer ----
LabelHandle vertical_label(Ui& ui, std::string id = "vertical", std::u16string text = u"設定", WritingMode mode = WritingMode::vertical) {
    return ui.add_label({.id = id, .kind = Kind::label, .value = text, .writing_mode = mode}).value.value();
}
void a_label_is_horizontal_unless_told_otherwise() {
    Ui ui;
    auto plain = ui.add_label({.id = "plain", .kind = Kind::label, .value = u"abc"}).value.value();
    check(*plain.writing_mode() == WritingMode::horizontal && *plain.vertical_digits() == 3, "Defaults keep the current behavior: horizontal, 3 digits");
    auto vertical = vertical_label(ui);
    check(*vertical.writing_mode() == WritingMode::vertical, "A label can be created vertical");
}
void creation_rejects_a_bad_digit_limit_and_other_components() {
    Ui ui;
    check(ui.add_label({.id = "four", .kind = Kind::label, .vertical_digits = 4}).status == Status::invalid_value, "A limit of 4 is invalid");
    check(ui.add_label({.id = "negative", .kind = Kind::label, .vertical_digits = -1}).status == Status::invalid_value, "A negative limit is invalid");
    check(bool(ui.add_label({.id = "zero", .kind = Kind::label, .vertical_digits = 0})), "A limit of 0 turns combining off");
    check(ui.add_text({.id = "field", .kind = Kind::text, .writing_mode = WritingMode::vertical}).status == Status::invalid_value, "Text fields do not support vertical writing");
    check(ui.add_button({.id = "button", .kind = Kind::button, .writing_mode = WritingMode::vertical}).status == Status::invalid_value, "Only labels support vertical writing");
    check(bool(ui.add_label({.id = "four", .kind = Kind::label})), "A rejected label does not reserve its identifier");
}
void the_typed_handle_reads_and_writes_both_properties() {
    Ui ui;
    auto label = vertical_label(ui, "label", u"123", WritingMode::horizontal);
    check(bool(label.set_writing_mode(WritingMode::vertical)) && *label.writing_mode() == WritingMode::vertical, "Writing mode changes at run time");
    check(bool(label.set_vertical_digits(2)) && *label.vertical_digits() == 2, "The digit limit changes at run time");
    check(label.set_vertical_digits(4).status == Status::invalid_value && *label.vertical_digits() == 2, "A limit of 4 is rejected and leaves the value");
    check(label.set_vertical_digits(-1).status == Status::invalid_value, "A negative limit is rejected");
    check(bool(label.set_vertical_digits(0)) && *label.vertical_digits() == 0, "0 is accepted");
    LabelHandle expired;
    { Ui other; expired = other.add_label({.id = "gone", .kind = Kind::label}).value.value(); }
    check(expired.set_writing_mode(WritingMode::vertical).status == Status::not_found && expired.writing_mode().status == Status::not_found, "An expired handle reports not_found");
}
void the_semantic_layer_matches_the_typed_handle() {
    Ui ui;
    auto label = vertical_label(ui, "label", u"123", WritingMode::horizontal);
    auto& tree = ui.semantics();
    auto node = tree.get({"label"});
    check(node && node->writing_mode == WritingMode::horizontal && node->vertical_digits == 3, "The node reports both properties");
    check(std::find(node->verbs.begin(), node->verbs.end(), Verb::set_writing_mode) != node->verbs.end() &&
          std::find(node->verbs.begin(), node->verbs.end(), Verb::set_vertical_digits) != node->verbs.end(), "Both verbs are offered on a label");
    check(bool(tree.perform({"label"}, Verb::set_writing_mode, WritingMode::vertical)) && *label.writing_mode() == WritingMode::vertical, "The semantic verb writes what the handle reads");
    check(bool(label.set_vertical_digits(2)) && tree.get({"label"})->vertical_digits == 2, "The handle writes what the node reads");
    check(tree.perform({"label"}, Verb::set_vertical_digits, 4).status == label.set_vertical_digits(4).status, "Both entries give the same Status for a bad limit");
    check(tree.perform({"label"}, Verb::set_vertical_digits, std::u16string(u"x")).status == Status::invalid_value, "A wrong argument type is invalid_value");
    check(tree.perform({"label"}, Verb::set_vertical_digits, 0) && tree.get({"label"})->vertical_digits == 0, "0 through the semantic layer");
    auto field = ui.add_text({.id = "field", .kind = Kind::text}).value.value();
    check(!tree.get({"field"})->writing_mode && tree.perform({"field"}, Verb::set_writing_mode, WritingMode::vertical).status == Status::not_supported, "Other components have neither property");
}
void a_property_change_is_announced() {
    Ui ui;
    auto label = vertical_label(ui, "label", u"設定", WritingMode::horizontal);
    std::vector<std::string> changed;
    auto subscription = ui.semantics().subscribe([&](const SemanticEvent& e) { if (e.kind == SemanticEventKind::property_changed) changed.push_back(e.property); });
    label.set_writing_mode(WritingMode::vertical);
    ui.semantics().dispatch_events();
    check(std::find(changed.begin(), changed.end(), "writing_mode") != changed.end(), "Changing the writing mode raises property_changed");
}
}

int main() {
    static const test::Case cases[] = {
        {"han_fullwidth_and_emoji_stay_upright", han_fullwidth_and_emoji_stay_upright},
        {"latin_words_and_sentences_rotate_as_one_run", latin_words_and_sentences_rotate_as_one_run},
        {"digits_follow_the_combine_limit", digits_follow_the_combine_limit},
        {"punctuation_gets_the_taiwan_placement_classes", punctuation_gets_the_taiwan_placement_classes},
        {"a_full_column_wraps_to_the_next_column", a_full_column_wraps_to_the_next_column},
        {"a_line_break_starts_a_new_column", a_line_break_starts_a_new_column},
        {"a_zero_height_never_wraps", a_zero_height_never_wraps},
        {"latin_words_move_whole_and_long_words_are_cut", latin_words_move_whole_and_long_words_are_cut},
        {"a_digit_group_takes_one_cell", a_digit_group_takes_one_cell},
        {"a_label_is_horizontal_unless_told_otherwise", a_label_is_horizontal_unless_told_otherwise},
        {"creation_rejects_a_bad_digit_limit_and_other_components", creation_rejects_a_bad_digit_limit_and_other_components},
        {"the_typed_handle_reads_and_writes_both_properties", the_typed_handle_reads_and_writes_both_properties},
        {"the_semantic_layer_matches_the_typed_handle", the_semantic_layer_matches_the_typed_handle},
        {"a_property_change_is_announced", a_property_change_is_announced},
    };
    return test::run("vertical", cases);
}
