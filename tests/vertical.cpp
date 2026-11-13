#include "core_internal.hpp"
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
    return ui.add_label({.id = id, .value = text, .writing_mode = mode}).value.value();
}
void a_label_is_horizontal_unless_told_otherwise() {
    Ui ui;
    auto plain = ui.add_label({.id = "plain", .value = u"abc"}).value.value();
    check(*plain.writing_mode() == WritingMode::horizontal && *plain.vertical_digits() == 3, "Defaults keep the current behavior: horizontal, 3 digits");
    auto vertical = vertical_label(ui);
    check(*vertical.writing_mode() == WritingMode::vertical, "A label can be created vertical");
}
void creation_rejects_a_bad_digit_limit_and_other_components() {
    Ui ui;
    check(ui.add_label({.id = "four", .vertical_digits = 4}).status == Status::invalid_value, "A limit of 4 is invalid");
    check(ui.add_label({.id = "negative", .vertical_digits = -1}).status == Status::invalid_value, "A negative limit is invalid");
    check(bool(ui.add_label({.id = "zero", .vertical_digits = 0})), "A limit of 0 turns combining off");
    check(ui.add_text({.id = "field", .writing_mode = WritingMode::vertical}).status == Status::invalid_value, "Text fields do not support vertical writing");
    check(ui.add_button({.id = "button", .writing_mode = WritingMode::vertical}).status == Status::invalid_value, "Only labels support vertical writing");
    check(bool(ui.add_label({.id = "four"})), "A rejected label does not reserve its identifier");
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
    { Ui other; expired = other.add_label({.id = "gone"}).value.value(); }
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
    auto field = ui.add_text({.id = "field"}).value.value();
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

// ---- layout: height, columns right to left, horizontal scrolling ----
const Appearance& look = web_appearance();
std::u16string cjk(int count) { return std::u16string(static_cast<std::size_t>(count), u'字'); }
struct Page {
    Ui ui; test::FakeMeasure measure; ScrollState outer;
    LabelHandle label;
    explicit Page(std::u16string text, WritingMode mode = WritingMode::vertical) { label = vertical_label(ui, "label", std::move(text), mode); }
    Frame arrange_in(int width, int height_dip, int window_height = 400) {
        return arrange(layout::vertical_text(label, height_dip), ui, look, measure, width, window_height, 1.f, outer);
    }
};
const Placement& placed(const Frame& frame, const LabelHandle& label) { return *frame.find(label); }

void a_vertical_label_has_the_given_height_and_its_width_follows_its_columns() {
    Page page(cjk(25));  // 16 px text: 12 cells of 16 px fit in 200 px, so 25 characters make 3 columns
    auto frame = page.arrange_in(400, 200);
    const auto& label = placed(frame, page.label);
    check(label.vertical && label.bounds.h == 200, "The height is the one the application gave");
    check(label.column_layout.columns.size() == 3 && label.column_layout.pitch == 24, "Three columns of 24 px (line height of 16 px text)");
    check(label.bounds.w == 3 * 24, "The width is columns times the column width");
    check(label.column_layout.columns[0].pieces.size() == 12 && label.column_layout.columns[2].pieces.size() == 1, "Characters fill a column, then the next");
}
void columns_run_from_right_to_left() {
    Page page(cjk(25));
    auto frame = page.arrange_in(400, 200);
    const auto& columns = placed(frame, page.label).column_layout.columns;
    const auto& label = placed(frame, page.label);
    check(columns[0].bounds.x + columns[0].bounds.w == label.bounds.x + label.bounds.w, "The first column touches the right edge");
    check(columns[1].bounds.x + 24 == columns[0].bounds.x && columns[2].bounds.x + 24 == columns[1].bounds.x, "Later columns follow to the left");
    check(columns[0].begin == 0 && columns[1].begin == 12 && columns[2].begin == 24, "Reading order is text order");
}
void a_line_break_opens_a_new_column_in_the_layout() {
    Page page(u"一二\n三");
    auto frame = page.arrange_in(400, 200);
    check(placed(frame, page.label).column_layout.columns.size() == 2, "The line break starts column two");
}
void a_horizontal_label_in_the_same_slot_lays_out_as_before() {
    Page page(cjk(25), WritingMode::horizontal);
    auto frame = page.arrange_in(400, 200);
    const auto& label = placed(frame, page.label);
    check(!label.vertical && label.lines.size() == 1 && label.bounds.w == 400, "A horizontal label ignores the vertical slot");
    check(bool(page.label.set_writing_mode(WritingMode::vertical)), "Switch at run time");
    frame = page.arrange_in(400, 200);
    check(placed(frame, page.label).vertical && placed(frame, page.label).column_layout.columns.size() == 3, "The next arrangement is vertical");
}
// A scrolling label is framed by the focus ring (3 DIP, 3 px at scale 1) at the edge of its box, so its columns keep that
// room from both sides: the view is the slot minus the two bands, and the text scrolls that much further.
constexpr int ring = 3;
void too_many_columns_make_the_label_scroll_sideways() {
    Page page(cjk(25));  // 72 px of columns in a 48 px slot
    auto frame = page.arrange_in(48, 200);
    const auto& label = placed(frame, page.label);
    const int out_of_view = 24 + 2 * ring;
    check(label.bounds.w == 48 && label.column_layout.maximum == out_of_view, "The label keeps the slot width; 24 px of columns plus the two ring bands are out of view");
    check(frame.regions.size() == 1 && frame.regions[0].horizontal && frame.regions[0].maximum == out_of_view && frame.regions[0].offset == 0, "A horizontal scroll region starts at the beginning");
    const auto& first = label.column_layout.columns[0];
    check(first.bounds.x >= label.bounds.x && first.bounds.x + first.bounds.w == label.bounds.x + label.bounds.w - ring, "The start of the text (the right edge) is shown first, a ring's thickness in from the edge");
    check(label.column_layout.columns[2].bounds.x < label.bounds.x, "The last column waits off to the left");
    check(label.column_layout.thumb.w > 0 && label.column_layout.thumb.x + label.column_layout.thumb.w == label.column_layout.track.x + label.column_layout.track.w, "The thumb starts at the right end of the bar");
    check(label.column_layout.track.x == ring && label.column_layout.track.x + label.column_layout.track.w == label.bounds.w - ring &&
          label.column_layout.track.y + label.column_layout.track.h == label.bounds.h - ring, "The bar lies inside the ring");
    check(label.bounds.h == 200, "The bar does not change the label height");
}
void the_wheel_moves_a_sideways_label_toward_the_end_of_the_text() {
    Page page(cjk(25));
    auto frame = page.arrange_in(48, 200);
    int x = placed(frame, page.label).bounds.x + 10, y = 50;
    check(frame.wheel(page.ui, x, y, -10) && page.outer.offset == 0, "A wheel step down scrolls the label, not the page");
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 10, "The view moved 10 px toward the later columns");
    check(frame.hwheel(page.ui, x, y, 4), "A horizontal wheel to the right moves back toward the start");
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 6, "The horizontal wheel moved the view 4 px back");
    frame.wheel(page.ui, x, y, -1000);
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 24 + 2 * ring, "Scrolling stops at the last column");
    check(!frame.wheel(page.ui, x, y, -10), "At the end the step is not consumed");
}
void the_scrollbar_thumb_maps_back_to_an_offset() {
    Page page(cjk(25));
    auto frame = page.arrange_in(48, 200);
    const auto& region = frame.regions[0];
    check(region.offset_for_thumb(region.thumb.x) == 0, "The thumb at the right end is the start");
    check(region.offset_for_thumb(region.track.x) == region.maximum, "The thumb at the left end is the last column");
}
void keyboard_arrows_scroll_a_focused_sideways_label() {
    Page page(cjk(25));
    auto frame = page.arrange_in(48, 200);
    check(page.label.describe()->focusable, "A label that scrolls can take focus");
    check(bool(detail::access(page.ui).focus("label")), "Focus it");
    check(bool(detail::access(page.ui).key_down({.key = Key::left})), "Left arrow");
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 24, "Left scrolls toward later columns by a column");
    detail::access(page.ui).key_down({.key = Key::right});
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 0, "Right scrolls back toward the start");
    detail::access(page.ui).key_down({.key = Key::end});
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 24 + 2 * ring, "End shows the last column");
    detail::access(page.ui).key_down({.key = Key::home});
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 0, "Home returns to the first column");
    auto wide = page.arrange_in(400, 200);
    check(!page.label.describe()->focusable && wide.regions.empty(), "When everything fits, the label neither scrolls nor takes focus");
}
void the_digit_limit_and_the_mode_change_the_next_arrangement_from_either_entry() {
    Page page(u"第123版", WritingMode::horizontal);
    auto forms = [&] {
        auto frame = page.arrange_in(400, 200);
        std::vector<VerticalForm> result;
        if (frame.find(page.label)->vertical) for (const auto& piece : frame.find(page.label)->column_layout.columns[0].pieces) result.push_back(piece.form);
        return result;
    };
    check(forms().empty(), "Horizontal: no vertical pieces");
    check(bool(page.ui.semantics().perform({"label"}, Verb::set_writing_mode, WritingMode::vertical)), "Switch through the semantic layer");
    check(forms() == std::vector<VerticalForm>{VerticalForm::upright, VerticalForm::combined, VerticalForm::upright}, "Three digits are combined with the default limit");
    check(bool(page.ui.semantics().perform({"label"}, Verb::set_vertical_digits, 2)), "Limit 2 through the semantic layer");
    check(forms() == std::vector<VerticalForm>{VerticalForm::upright, VerticalForm::sideways, VerticalForm::upright}, "Three digits turn when the limit is 2");
    check(bool(page.label.set_vertical_digits(0)) && bool(page.label.set_vertical_digits(3)), "Back through the typed handle");
    check(forms().size() == 3 && forms()[1] == VerticalForm::combined, "Combined again at 3");
    check(bool(page.label.set_writing_mode(WritingMode::horizontal)) && forms().empty(), "Back to horizontal at once");
}
void the_page_can_reveal_a_vertical_label_like_any_control() {
    Ui ui; test::FakeMeasure measure; ScrollState outer;
    auto tall = ui.add_label({.id = "tall", .value = std::u16string(300, u'x')}).value.value();
    auto label = vertical_label(ui, "label", cjk(25));
    auto page = layout::scroll(layout::column({layout::component(tall), layout::vertical_text(label, 200)}));
    auto frame = arrange(page, ui, look, measure, 300, 150, 1.f, outer);
    const auto* item = frame.find(label);
    check(item->bounds.y + item->bounds.h > 150, "The label starts below the fold");
    frame = arrange(page, ui, look, measure, 300, 150, 1.f, outer, "label");
    item = frame.find(label);
    check(outer.offset > 0 && item->bounds.y == 0, "Revealing a control taller than the window aligns its top, as for any control");
    auto short_label = vertical_label(ui, "short", cjk(8));
    auto short_page = layout::scroll(layout::column({layout::component(tall), layout::vertical_text(short_label, 100)}));
    ScrollState second;
    frame = arrange(short_page, ui, look, measure, 300, 150, 1.f, second, "short");
    item = frame.find(short_label);
    check(item->bounds.y >= 0 && item->bounds.y + item->bounds.h <= 150, "A label that fits is brought fully into view");
}
void a_very_long_word_flows_without_reshaping_every_candidate() {
    test::FakeMeasure measure;
    auto columns = flow_vertical(measure, std::u16string(4000, u'a'), 16, 3, 64);
    check(columns.size() == 4000 * 8 / 64 && columns[0].bounds.h == 64, "4000 letters of 8 px fill 500 columns of 64 px");
}
void shift_wheel_reaches_only_the_sideways_label() {
    Page page(cjk(25));
    auto frame = page.arrange_in(48, 200);
    int x = placed(frame, page.label).bounds.x + 10;
    check(!frame.wheel(page.ui, x, 390, -10, true), "Shift+wheel outside the label scrolls nothing");
    check(frame.wheel(page.ui, x, 50, -10, true), "Shift+wheel over the label scrolls it");
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 10, "...toward the later columns, like the plain wheel");
}
void a_vertical_label_inside_a_scrolling_page_chains_the_wheel_to_the_page() {
    Ui ui; test::FakeMeasure measure; ScrollState outer;
    auto tall = ui.add_label({.id = "tall", .value = std::u16string(20, u'x')}).value.value();
    auto label = vertical_label(ui, "label", cjk(25));
    auto page = layout::scroll(layout::column({layout::component(tall), layout::vertical_text(label, 200)}));
    auto frame = arrange(page, ui, look, measure, 48 + 16, 200, 1.f, outer);
    check(frame.regions.size() == 2 && !frame.regions[0].horizontal && frame.regions[1].horizontal, "The page region comes first, the label's region last");
    check(frame.maximum > 0 && frame.viewport == frame.regions[0].viewport, "The frame's page fields describe the page, not the label");
    const auto* item = frame.find(label);
    int x = item->bounds.x + 10, y = item->bounds.y + 10;
    check(item->clip.contains(x, y), "The label is on screen");
    check(frame.wheel(ui, x, y, -10) && outer.offset == 0, "The label takes the step first");
    frame = arrange(page, ui, look, measure, 48 + 16, 200, 1.f, outer);
    frame.wheel(ui, x, y, -1000);
    frame = arrange(page, ui, look, measure, 48 + 16, 200, 1.f, outer);
    item = frame.find(label);
    check(frame.regions[1].offset == frame.regions[1].maximum, "The label is scrolled to its end");
    check(frame.wheel(ui, item->bounds.x + 10, item->bounds.y + 10, -20) && outer.offset > 0, "Then the page takes it");
    frame = arrange(page, ui, look, measure, 48 + 16, 200, 1.f, outer);
    item = frame.find(label);
    check(frame.regions[1].viewport == item->bounds && frame.regions[1].track.y + frame.regions[1].track.h + ring == item->bounds.y + item->bounds.h,
          "The label's region moves with the page");
}
void text_size_and_scale_change_the_columns() {
    Page page(cjk(25));
    auto frame = arrange(layout::vertical_text(page.label, 200), page.ui, look, page.measure, 600, 400, 1.5f, page.outer);
    const auto& label = placed(frame, page.label);
    check(label.bounds.h == 300 && label.column_layout.em == 24 && label.column_layout.pitch == 36, "At 150% the height and the cells scale together");
    check(label.column_layout.columns[0].pieces.size() == 12, "300 px hold 12 cells of 24 px (288)");
    PresentationSettings settings; settings.text_size = 24;
    check(bool(page.ui.set_presentation(settings)), "A larger text size");
    frame = page.arrange_in(600, 200);
    check(placed(frame, page.label).column_layout.em == 24 && placed(frame, page.label).column_layout.columns[0].pieces.size() == 8, "Fewer, larger cells per column: 8 x 24 = 192");
}
// ---- #80: alignment, line spacing and paragraph spacing of vertical labels ----
void the_text_flow_properties_map_onto_columns() {
    using Align = detail::VerticalParams::Align;
    ComponentConfig control{.id = "l"};
    auto params = detail::vertical_params(control);
    check(params.align == Align::top && params.line_spacing == 1.0 && params.paragraph_spacing == 0, "The defaults are top, 1.0 and 0");
    control.text_align = TextAlign::center; control.line_spacing = 1.5; control.paragraph_spacing = 8;
    params = detail::vertical_params(control);
    check(params.align == Align::center && params.line_spacing == 1.5 && params.paragraph_spacing == 8, "Center, 1.5 and 8 DIP at scale 1");
    check(detail::vertical_params(control, 2.f).paragraph_spacing == 16, "The paragraph spacing is physical pixels at scale 2");
    control.text_align = TextAlign::right;
    check(detail::vertical_params(control).align == Align::bottom, "Right reads as the bottom of the column");
    control.text_align = TextAlign::left;
    check(detail::vertical_params(control).align == Align::top, "Left reads as the top of the column");
}
void alignment_places_each_column_at_the_top_center_or_bottom() {
    Page page(cjk(25));  // columns of 12, 12 and 1 cells in a 200 px area
    auto aligned = [&](TextAlign align) {
        check(bool(page.label.set_text_align(align)), "The alignment is accepted");
        auto frame = page.arrange_in(400, 200);
        const auto& label = placed(frame, page.label);
        std::vector<int> tops;
        bool lines_agree = label.lines.size() == label.column_layout.columns.size();
        for (std::size_t i = 0; i < label.column_layout.columns.size(); ++i) {
            tops.push_back(label.column_layout.columns[i].bounds.y);
            lines_agree = lines_agree && label.lines[i].bounds.y == tops.back();
        }
        check(lines_agree, "The lines follow the columns");
        return std::make_pair(tops, label.column_layout.columns);
    };
    auto [top, columns] = aligned(TextAlign::left);
    check(top == std::vector<int>{0, 0, 0}, "Left: every column starts at the top");
    const int full = columns[0].bounds.h, last = columns[2].bounds.h;
    check(full == 192 && last == 16, "The columns are 12 and 1 cells long");
    check(aligned(TextAlign::center).first == std::vector<int>{(200 - full) / 2, (200 - full) / 2, (200 - last) / 2}, "Center: each column is centered in the label's height");
    check(aligned(TextAlign::right).first == std::vector<int>{200 - full, 200 - full, 200 - last}, "Right: each column ends at the bottom");
    check(aligned(TextAlign::left).first == top, "Back to left");
}
void line_spacing_is_the_distance_between_columns() {
    Page page(cjk(25));
    check(bool(page.label.set_line_spacing(1.5)), "A line spacing of 1.5 is accepted");
    auto frame = page.arrange_in(400, 200);
    const auto& label = placed(frame, page.label);
    const auto& columns = label.column_layout.columns;
    check(label.column_layout.pitch == 36 && label.bounds.w == 3 * 36, "The column pitch is 1.5 times the 24 px line height");
    check(columns[1].bounds.x + 36 == columns[0].bounds.x && columns[2].bounds.x + 36 == columns[1].bounds.x, "Columns are 36 px apart");
    check(columns[0].bounds.x + columns[0].bounds.w == label.bounds.x + label.bounds.w, "The first column still touches the right edge");
    check(bool(page.ui.semantics().perform({"label"}, Verb::set_line_spacing, 2.0)), "The semantic layer sets 2.0");
    frame = page.arrange_in(400, 200);
    check(placed(frame, page.label).column_layout.pitch == 48, "The next arrangement uses the pitch 48");
}
void paragraph_spacing_separates_paragraphs_not_columns_of_one_paragraph() {
    Page page(cjk(13) + u"\n" + cjk(13));  // two paragraphs of two columns each
    check(bool(page.label.set_paragraph_spacing(8)), "A paragraph spacing of 8 DIP is accepted");
    auto frame = page.arrange_in(800, 200);
    const auto& label = placed(frame, page.label);
    const auto& columns = label.column_layout.columns;
    check(columns.size() == 4 && !columns[0].last_in_paragraph && columns[1].last_in_paragraph, "Two paragraphs of two columns");
    check(columns[0].bounds.x - columns[1].bounds.x == 24, "Columns of one paragraph stay 24 px apart");
    check(columns[1].bounds.x - columns[2].bounds.x == 24 + 8, "The paragraph gap is added between the paragraphs");
    check(columns[2].bounds.x - columns[3].bounds.x == 24, "And not inside the second paragraph");
    check(label.bounds.w == 4 * 24 + 8, "The width includes the gap");
    check(bool(page.ui.semantics().perform({"label"}, Verb::set_paragraph_spacing, 0)), "The semantic layer resets it");
    frame = page.arrange_in(800, 200);
    check(placed(frame, page.label).bounds.w == 4 * 24, "No gap at 0");
    // The spacing is DIP: at scale 2 it doubles together with the text.
    check(bool(page.label.set_paragraph_spacing(8)), "Set again");
    auto doubled = arrange(layout::vertical_text(page.label, 200), page.ui, look, page.measure, 1600, 800, 2.f, page.outer);
    const auto& scaled_label = placed(doubled, page.label);
    const int pitch = scaled_label.column_layout.pitch;
    check(scaled_label.column_layout.columns[1].bounds.x - scaled_label.column_layout.columns[2].bounds.x == pitch + 16, "Eight DIP are 16 px at scale 2");
}
void the_three_properties_leave_a_horizontal_label_to_the_horizontal_rules() {
    Page page(cjk(25), WritingMode::horizontal);
    check(bool(page.label.set_text_align(TextAlign::center)) && bool(page.label.set_line_spacing(1.5)), "Set on a horizontal label");
    auto frame = page.arrange_in(400, 200);
    check(!placed(frame, page.label).vertical && placed(frame, page.label).column_layout.columns.empty(), "Still laid out as horizontal text");
    check(bool(page.label.set_writing_mode(WritingMode::vertical)), "Switch to vertical");
    frame = page.arrange_in(400, 200);
    check(placed(frame, page.label).column_layout.pitch == 36, "The same properties now space the columns");
}
void the_scroll_position_follows_the_content_when_it_shrinks() {
    Page page(cjk(25));
    std::u16string shown = cjk(25);
    page.label.connect_source([&] { return shown; });
    auto frame = page.arrange_in(48, 200);
    detail::access(page.ui).focus("label");
    detail::access(page.ui).key_down({.key = Key::end});
    frame = page.arrange_in(48, 200);
    check(frame.regions[0].offset == 24 + 2 * ring, "Scrolled to the end");
    shown = cjk(14);  // two columns: 48 px, exactly the slot
    frame = page.arrange_in(48, 200);
    check(frame.regions.empty() && !page.label.describe()->focusable, "When the text fits again the label stops scrolling");
    shown = cjk(25);
    frame = page.arrange_in(48, 200);
    check(frame.regions.size() == 1 && frame.regions[0].offset == 0, "Growing again starts from the beginning, not the old position");
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
        {"a_vertical_label_has_the_given_height_and_its_width_follows_its_columns", a_vertical_label_has_the_given_height_and_its_width_follows_its_columns},
        {"columns_run_from_right_to_left", columns_run_from_right_to_left},
        {"a_line_break_opens_a_new_column_in_the_layout", a_line_break_opens_a_new_column_in_the_layout},
        {"a_horizontal_label_in_the_same_slot_lays_out_as_before", a_horizontal_label_in_the_same_slot_lays_out_as_before},
        {"too_many_columns_make_the_label_scroll_sideways", too_many_columns_make_the_label_scroll_sideways},
        {"the_wheel_moves_a_sideways_label_toward_the_end_of_the_text", the_wheel_moves_a_sideways_label_toward_the_end_of_the_text},
        {"the_scrollbar_thumb_maps_back_to_an_offset", the_scrollbar_thumb_maps_back_to_an_offset},
        {"keyboard_arrows_scroll_a_focused_sideways_label", keyboard_arrows_scroll_a_focused_sideways_label},
        {"the_digit_limit_and_the_mode_change_the_next_arrangement_from_either_entry", the_digit_limit_and_the_mode_change_the_next_arrangement_from_either_entry},
        {"the_page_can_reveal_a_vertical_label_like_any_control", the_page_can_reveal_a_vertical_label_like_any_control},
        {"a_very_long_word_flows_without_reshaping_every_candidate", a_very_long_word_flows_without_reshaping_every_candidate},
        {"shift_wheel_reaches_only_the_sideways_label", shift_wheel_reaches_only_the_sideways_label},
        {"a_vertical_label_inside_a_scrolling_page_chains_the_wheel_to_the_page", a_vertical_label_inside_a_scrolling_page_chains_the_wheel_to_the_page},
        {"text_size_and_scale_change_the_columns", text_size_and_scale_change_the_columns},
        {"the_scroll_position_follows_the_content_when_it_shrinks", the_scroll_position_follows_the_content_when_it_shrinks},
        {"the_text_flow_properties_map_onto_columns", the_text_flow_properties_map_onto_columns},
        {"alignment_places_each_column_at_the_top_center_or_bottom", alignment_places_each_column_at_the_top_center_or_bottom},
        {"line_spacing_is_the_distance_between_columns", line_spacing_is_the_distance_between_columns},
        {"paragraph_spacing_separates_paragraphs_not_columns_of_one_paragraph", paragraph_spacing_separates_paragraphs_not_columns_of_one_paragraph},
        {"the_three_properties_leave_a_horizontal_label_to_the_horizontal_rules", the_three_properties_leave_a_horizontal_label_to_the_horizontal_rules},
    };
    return test::run("vertical", cases);
}
