// Vertical writing (#82), font seam: where the real fonts put the ink of one vertical piece in its cell.
// Every expectation is measured on pixels (ink bounding boxes), never read back from the layout code.
#include "text.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
using namespace lcars;
using namespace lcars::detail;
using test::check;

namespace {
constexpr int em = 48;                 // large, so that one pixel is about 2% of the cell
constexpr double tolerance = 1.5;      // pixels: how far an ink center may sit from the cell center
const Rect cell{36, 36, em, em};

struct Ink {
    int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
    bool any() const { return right >= left; }
    double cx() const { return (left + right + 1) / 2.0; }
    double cy() const { return (top + bottom + 1) / 2.0; }
    int width() const { return right - left + 1; }
    int height() const { return bottom - top + 1; }
};
Ink ink_of(const Canvas& canvas) {
    Ink ink;
    for (int y = 0; y < canvas.height; ++y) for (int x = 0; x < canvas.width; ++x)
        if (canvas.pixels[(static_cast<std::size_t>(y) * canvas.width + x) * 4] < 160) {
            ink.left = std::min(ink.left, x); ink.right = std::max(ink.right, x);
            ink.top = std::min(ink.top, y); ink.bottom = std::max(ink.bottom, y);
        }
    return ink;
}
bool font_installed(const char* file) { return std::filesystem::exists(std::filesystem::path("C:/Windows/Fonts") / file); }

struct Weight { const char* name; const char* file; };
const Weight weights[] = {{"Regular", "msjh.ttc"}, {"Bold", "msjhbd.ttc"}, {"Light", "msjhl.ttc"}};
// The same Text the framework uses, with Microsoft JhengHei pinned to one weight.
std::unique_ptr<Text> text_with(const Weight& weight) {
    FontFile pinned{std::filesystem::path("C:/Windows/Fonts") / weight.file, 0};
    return std::make_unique<Text>([pinned](std::u16string_view name) { return name == u"Microsoft JhengHei" ? pinned : system_font_file(name); },
                                  [](std::string_view) {});
}
Canvas draw_in(Text& text, std::u16string_view value, VerticalForm form, Rect where = cell, int pixels = em) {
    Canvas canvas(2 * cell.x + where.w, 2 * cell.y + where.h, {255, 255, 255});
    text.draw_vertical(canvas, value, form, pixels, where, {0, 0, 0});
    return canvas;
}

void point_punctuation_sits_in_the_middle_of_the_cell_in_every_weight() {
    for (const auto& weight : weights) {
        if (!font_installed(weight.file)) continue;
        auto text = text_with(weight);
        for (auto mark : {u"，", u"。", u"、", u"；", u"：", u"！", u"？"}) {
            auto ink = ink_of(draw_in(*text, mark, VerticalForm::point));
            check(ink.any(), "The mark is drawn");
            check(std::abs(ink.cx() - (cell.x + em / 2.0)) <= tolerance && std::abs(ink.cy() - (cell.y + em / 2.0)) <= tolerance,
                  "The ink center is the cell center (Regular, Bold and Light alike)");
        }
    }
}
void brackets_open_up_and_down() {
    for (const auto& weight : weights) {
        if (!font_installed(weight.file)) continue;
        auto text = text_with(weight);
        for (auto bracket : {u"（", u"）", u"《", u"》"}) {
            auto ink = ink_of(draw_in(*text, bracket, VerticalForm::bracket));
            check(ink.any() && ink.width() > ink.height(), "A turned bracket is wider than tall; upright ones are the opposite");
        }
        // Not simply the horizontal glyph: the turned quote differs from the plain one.
        Canvas plain(2 * cell.x + em, 2 * cell.y + em, {255, 255, 255});
        text->draw(plain, text->shape(u"「", em), cell.x, cell.y + em, {0, 0, 0});
        check(draw_in(*text, u"「", VerticalForm::bracket).pixels != plain.pixels, "A quote is drawn in its vertical form");
    }
}
void dashes_and_ellipses_are_lines_through_the_middle_of_two_cells() {
    const Rect two{cell.x, cell.y, em, 2 * em};
    for (const auto& weight : weights) {
        if (!font_installed(weight.file)) continue;
        auto text = text_with(weight);
        for (auto [mark, form] : {std::pair{u"——", VerticalForm::line}}) {
            auto canvas = draw_in(*text, mark, form, two);
            auto ink = ink_of(canvas);
            check(ink.any() && std::abs(ink.cx() - (two.x + em / 2.0)) <= tolerance, "The dash runs through the middle of the column");
            check(ink.width() * 4 < em && ink.height() >= 2 * em - 4 && ink.height() <= 2 * em + 4, "It is a thin line that spans two cells");
            for (int y = ink.top; y <= ink.bottom; ++y) {
                bool inked = false;
                for (int x = ink.left; x <= ink.right; ++x) inked |= canvas.pixels[(static_cast<std::size_t>(y) * canvas.width + x) * 4] < 160;
                check(inked, "The two halves join without a gap");
            }
        }
        auto ellipsis = ink_of(draw_in(*text, u"……", VerticalForm::line, two));
        check(ellipsis.any() && std::abs(ellipsis.cx() - (two.x + em / 2.0)) <= tolerance && std::abs(ellipsis.cy() - (two.y + em)) <= tolerance,
              "The ellipsis is a dotted vertical line centered on its two cells");
        check(ellipsis.height() > em, "It spans more than one cell");
    }
}
void a_middle_dot_is_centered() {
    for (const auto& weight : weights) {
        if (!font_installed(weight.file)) continue;
        auto text = text_with(weight);
        for (auto dot : {u"·", u"‧", u"・"}) {
            auto ink = ink_of(draw_in(*text, dot, VerticalForm::middle_dot));
            check(ink.any() && std::abs(ink.cx() - (cell.x + em / 2.0)) <= tolerance && std::abs(ink.cy() - (cell.y + em / 2.0)) <= tolerance, "The dot sits in the middle of its cell");
        }
    }
}
// The oracle for sideways text: draw the same word horizontally, turn the picture a quarter turn clockwise,
// and the vertical drawing must show the same shape.
void sideways_text_is_the_horizontal_picture_turned_clockwise() {
    auto text = text_with(weights[0]);
    constexpr int px = 32;
    Canvas flat(260, 90, {255, 255, 255});
    text->draw(flat, text->shape(u"LcarsUI", px), 10, 60, {0, 0, 0});
    auto flat_ink = ink_of(flat);
    const Rect word{40, 30, (px * 3 + 1) / 2, 260};
    auto turned = draw_in(*text, u"LcarsUI", VerticalForm::sideways, word, px);
    auto turned_ink = ink_of(turned);
    check(turned_ink.any() && std::abs(turned_ink.height() - flat_ink.width()) <= 2 && std::abs(turned_ink.width() - flat_ink.height()) <= 2,
          "The vertical drawing is as tall as the horizontal one is wide");
    // Compare pixels: horizontal (x, y) maps to vertical (height - 1 - y, x) after a clockwise quarter turn.
    int differing = 0, total = 0;
    for (int y = 0; y < flat_ink.height(); ++y) for (int x = 0; x < flat_ink.width(); ++x) {
        auto a = flat.pixels[(static_cast<std::size_t>(flat_ink.top + y) * flat.width + flat_ink.left + x) * 4];
        int tx = turned_ink.left + (flat_ink.height() - 1 - y), ty = turned_ink.top + x;
        auto b = turned.pixels[(static_cast<std::size_t>(ty) * turned.width + tx) * 4];
        total += a < 160 || b < 160; differing += (a < 160) != (b < 160);
    }
    check(total > 0 && differing * 5 < total, "The two pictures agree (clockwise, not counter-clockwise or mirrored)");
}
void combined_digits_fit_one_cell() {
    for (const auto& weight : weights) {
        if (!font_installed(weight.file)) continue;
        auto text = text_with(weight);
        for (auto digits : {u"12", u"123"}) {
            auto ink = ink_of(draw_in(*text, digits, VerticalForm::combined));
            check(ink.any() && ink.width() <= em && std::abs(ink.cx() - (cell.x + em / 2.0)) <= tolerance && std::abs(ink.cy() - (cell.y + em / 2.0)) <= tolerance + 4,
                  "Combined digits stay inside the cell width and sit in the middle of the cell");
        }
        auto one = ink_of(draw_in(*text, u"12", VerticalForm::combined));
        auto three = ink_of(draw_in(*text, u"123", VerticalForm::combined));
        check(three.width() >= one.width() - 2, "More digits are not drawn narrower in total");
    }
}
void emoji_and_han_stay_inside_the_cell() {
    auto text = text_with(weights[0]);
    auto emoji = ink_of(draw_in(*text, u"\U0001F600", VerticalForm::upright));
    check(emoji.any() && emoji.left >= cell.x - 1 && emoji.right <= cell.x + em && emoji.top >= cell.y - 1 && emoji.bottom <= cell.y + em, "An emoji does not stick out of its cell");
    check(std::abs(emoji.cx() - (cell.x + em / 2.0)) <= 3 && std::abs(emoji.cy() - (cell.y + em / 2.0)) <= 3, "An emoji is centered");
    auto han = ink_of(draw_in(*text, u"設", VerticalForm::upright));
    check(han.any() && han.left >= cell.x - 2 && han.right <= cell.x + em + 2 && han.top >= cell.y - 4 && han.bottom <= cell.y + em + 4, "A Han character fills its cell");
    check(std::abs(han.cx() - (cell.x + em / 2.0)) <= 4 && std::abs(han.cy() - (cell.y + em / 2.0)) <= 5, "A Han character sits in the middle of its cell");
}
// The reference (CPU) canvas and the recorded draw list must show the same picture; the OpenGL backend
// draws the same list, so its screenshots compare against this one.
void the_recorded_draw_list_matches_the_direct_picture() {
    auto text = text_with(weights[0]);
    struct Piece { const char16_t* value; VerticalForm form; };
    for (auto piece : {Piece{u"LcarsUI", VerticalForm::sideways}, Piece{u"12", VerticalForm::combined}, Piece{u"（", VerticalForm::bracket},
                       Piece{u"，", VerticalForm::point}, Piece{u"—", VerticalForm::line}, Piece{u"設", VerticalForm::upright}}) {
        Rect where{36, 36, em, piece.form == VerticalForm::sideways ? 200 : em};
        auto direct = draw_in(*text, piece.value, piece.form, where);
        RecordingCanvas recording(direct.width, direct.height, {255, 255, 255});
        text->draw_vertical(recording, piece.value, piece.form, em, where, {0, 0, 0});
        auto replay = render_cpu(recording.list, direct.width, direct.height);
        int differing = 0;
        for (std::size_t i = 0; i < direct.pixels.size(); i += 4) differing += std::abs(int(direct.pixels[i]) - int(replay.pixels[i])) > 8;
        check(differing == 0, "Replaying the draw list gives the direct picture");
    }
}
}

int main() {
    static const test::Case cases[] = {
        {"point_punctuation_sits_in_the_middle_of_the_cell_in_every_weight", point_punctuation_sits_in_the_middle_of_the_cell_in_every_weight},
        {"brackets_open_up_and_down", brackets_open_up_and_down},
        {"dashes_and_ellipses_are_lines_through_the_middle_of_two_cells", dashes_and_ellipses_are_lines_through_the_middle_of_two_cells},
        {"a_middle_dot_is_centered", a_middle_dot_is_centered},
        {"sideways_text_is_the_horizontal_picture_turned_clockwise", sideways_text_is_the_horizontal_picture_turned_clockwise},
        {"combined_digits_fit_one_cell", combined_digits_fit_one_cell},
        {"emoji_and_han_stay_inside_the_cell", emoji_and_han_stay_inside_the_cell},
        {"the_recorded_draw_list_matches_the_direct_picture", the_recorded_draw_list_matches_the_direct_picture},
    };
    return test::run("vertical_text", cases);
}
