// B01-B03 arrangement through the public layout seam with a deterministic fake text backend.
#include "check.hpp"
#include "fake_measure.hpp"
using namespace lcars;
using test::check;
using test::FakeMeasure;

namespace {
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void long_label_wraps_and_pushes_following_controls_down() {
    Ui ui({{.id = "intro", .kind = Kind::label, .value = u"這是一段很長的說明文字會自動換行\n第二段"},
           {.id = "apply", .kind = Kind::button, .label = u"套用"}});
    auto page = layout::column({layout::control("intro"), layout::control("apply")}, 12);
    FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(page, ui, web_appearance(), measure, 200, 600, 1.f, scroll);
    auto intro = frame.find("intro");
    auto apply = frame.find("apply");
    check(intro && apply, "Both controls are placed");
    check(intro->lines.size() == 3 && intro->lines[0].text == u"這是一段很長的說明文字會" && intro->lines[2].text == u"第二段",
          "16 px CJK wraps at 12 characters per 200 px line and at the explicit break");
    check(intro->bounds.h == 3 * 24, "Label height follows its wrapped line count");
    check(apply->bounds.y == intro->bounds.y + intro->bounds.h + 12, "Next control starts one gap below the label");
    check(!overlap(intro->bounds, apply->bounds), "Wrapped label never covers its neighbour");
}

void latin_words_wrap_at_spaces() {
    FakeMeasure measure;
    // 16 px ASCII is 8 px per character: 80 px holds 10 characters.
    auto lines = wrap(measure, u"Ctrl+Z and Ctrl+Shift+Z", 16, 80);
    check((lines == std::vector<std::u16string>{u"Ctrl+Z ", u"and ", u"Ctrl+Shift", u"+Z"}),
          "Break after spaces; only a word longer than the line is cut");
    lines = wrap(measure, u"中文Ctrl說明", 16, 72);
    check((lines == std::vector<std::u16string>{u"中文Ctrl", u"說明"}), "CJK characters remain break opportunities");
}

std::vector<Control> field_controls() {
    return {{.id = "name.label", .kind = Kind::label, .value = u"顯示名稱", .target = "name", .role = TextRole::name},
            {.id = "name", .kind = Kind::text, .value = u"測試使用者"},
            {.id = "name.error", .kind = Kind::label, .target = "name", .role = TextRole::error},
            {.id = "apply", .kind = Kind::button, .label = u"套用"}};
}
const Appearance* appearances[] = {&web_appearance(), &material_appearance()};
bool inside(Rect inner, Rect outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

void field_roles_stay_with_their_field_in_both_appearances() {
    for (const auto* appearance : appearances) {
        Ui ui(field_controls());
        auto page = layout::column({layout::control("name"), layout::control("apply")}, 12);
        FakeMeasure measure;
        ScrollState scroll;
        auto before = arrange(page, ui, *appearance, measure, 240, 600, 1.f, scroll);
        ui.set_error("name", u"名稱不能空白，也不能只有空格或全形空白字元");
        auto after = arrange(page, ui, *appearance, measure, 240, 600, 1.f, scroll);
        auto field = after.find("name");
        auto apply = after.find("apply");
        check(field->label_lines.size() == 1 && field->label_lines[0].text == u"顯示名稱", "Name label is carried by its field");
        check(before.find("name")->error_lines.empty() && field->error_lines.size() >= 2, "Error appears and wraps over lines");
        for (const auto& line : field->error_lines)
            check(inside(line.bounds, field->bounds), "Every error line stays inside its field");
        check(apply->bounds.y >= field->bounds.y + field->bounds.h && apply->bounds.y > before.find("apply")->bounds.y,
              "Re-measured error pushes the next control down");
        check(!after.find("name.error") && !after.find("name.label"), "Role labels are not placed twice");
    }
}

std::vector<Control> page_controls() {
    auto controls = field_controls();
    controls.push_back({.id = "long", .kind = Kind::label, .value = std::u16string(400, u'長')});
    controls.push_back({.id = "result", .kind = Kind::label, .value = u"上次套用結果：測試使用者"});
    controls.push_back({.id = "reset", .kind = Kind::button, .label = u"重設"});
    return controls;
}
layout::Node page_layout() {
    using namespace layout;
    return scroll(column({control("long"),
                          grid({column({control("name"), control("apply")}), control("result")}, 380, 20),
                          control("reset")},
                         12, 24));
}

void narrow_window_moves_results_below() {
    Ui ui(page_controls());
    FakeMeasure measure;
    for (float scale : {1.f, 1.5f}) {
        ScrollState scroll;
        auto d = [&](int v) { return static_cast<int>(v * scale); };
        auto wide = arrange(page_layout(), ui, web_appearance(), measure, d(960), d(720), scale, scroll);
        auto narrow = arrange(page_layout(), ui, web_appearance(), measure, d(640), d(480), scale, scroll);
        auto beside = wide.find("result")->bounds, below = narrow.find("result")->bounds;
        check(beside.x > wide.find("name")->bounds.x && beside.y == wide.find("name")->bounds.y, "960 DIP: results sit beside the form");
        check(below.x == narrow.find("name")->bounds.x && below.y > narrow.find("apply")->bounds.y, "640 DIP: results move below the form");
    }
}

void scrolling_reveals_focus_and_clips_hits() {
    Ui ui(page_controls());
    FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(page_layout(), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    check(frame.maximum > 0 && frame.thumb.h > 0, "Overflowing content gets a scroll range and a thumb");
    auto hidden = frame.find("reset")->bounds;
    check(hidden.y >= frame.viewport.y + frame.viewport.h, "Reset starts outside the viewport");
    check(frame.hit(hidden.x + 2, hidden.y + 2) == nullptr, "Nothing outside the viewport can be hit");
    frame = arrange(page_layout(), ui, web_appearance(), measure, 640, 480, 1.f, scroll, "reset");
    auto shown = frame.find("reset")->bounds;
    check(shown.y >= 0 && shown.y + shown.h == 480 && frame.offset > 0, "Revealing reset scrolls just enough to show all of it");
    auto hit = frame.hit(shown.x + 2, shown.y + 2);
    check(hit && hit->id == "reset", "Hit testing uses the scrolled positions");
    scroll.offset = 1 << 20;
    frame = arrange(page_layout(), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    check(frame.offset == frame.maximum && scroll.offset == frame.maximum, "Wheel overshoot is clamped before placement");
    check(frame.offset_for_thumb(frame.track.y) == 0 && frame.offset_for_thumb(frame.track.y + frame.track.h) == frame.maximum,
          "The scrollbar thumb maps onto the whole range");
}

void resize_after_deep_scroll_settles_in_the_first_frame() {
    Ui ui(page_controls());
    FakeMeasure measure;
    ScrollState scroll;
    arrange(page_layout(), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    scroll.offset = 1 << 20;
    arrange(page_layout(), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    auto first = arrange(page_layout(), ui, web_appearance(), measure, 960, 900, 1.f, scroll);
    auto second = arrange(page_layout(), ui, web_appearance(), measure, 960, 900, 1.f, scroll);
    check(first.offset == second.offset && first.offset <= first.maximum, "Scroll offset is settled before the first frame");
    check(first.items.size() == second.items.size(), "Same items in both frames");
    for (std::size_t i = 0; i < first.items.size(); ++i)
        check(first.items[i].bounds == second.items[i].bounds, "Every control sits where the next frame puts it");
    auto name = first.find("name")->bounds;
    auto hit = second.hit(name.x + 4, name.y + name.h - 4);
    check(name.y + name.h <= 900 ? hit && hit->id == "name" : true, "Hit target matches the first painted position");
}

void pointer_hits_map_to_the_painted_caret_positions() {
    for (float scale : {1.f, 1.5f}) {
        Ui ui({{.id = "name", .kind = Kind::text, .value = u"Ab中\U00020000文"}});
        FakeMeasure measure;
        ScrollState scroll;
        auto frame = arrange(layout::column({layout::control("name")}), ui, material_appearance(), measure, 400, 200, scale, scroll);
        auto field = frame.find("name");
        int origin = field->bounds.x + field->field.editor.x;
        for (auto [offset, x] : field->run.stops) {
            int painted = origin + static_cast<int>(x - field->offset);
            check(frame.text_offset("name", painted) == offset, "Pointer at a painted caret returns that caret");
        }
        check(frame.text_offset("name", origin + static_cast<int>(field->run.x(3) + field->text_size * 0.4f)) == 3,
              "A pointer inside the left part of the surrogate pair snaps to its start");
        check(field->text_size == static_cast<int>(16 * scale + 0.5f), "Text size follows the render scale");
    }
}

void long_text_keeps_caret_visible_and_reset_clears_offset() {
    Ui ui({{.id = "name", .kind = Kind::text, .value = std::u16string(60, u'A')}, {.id = "apply", .kind = Kind::button, .label = u"套用"}});
    FakeMeasure measure;
    ScrollState scroll;
    auto page = layout::column({layout::control("name"), layout::control("apply")});
    ui.focus("name");
    ui.select("name", 60, 60);
    ui.text_input(u"Z");
    auto frame = arrange(page, ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    auto field = frame.find("name");
    float caret = field->run.x(61) - field->offset;
    check(field->offset > 0 && caret >= 0 && caret <= field->field.editor.w, "Caret at the end stays inside the editor");
    ui.focus("apply");
    arrange(page, ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    check(ui.text_offset("name") > 0, "Leaving the field keeps its display offset");
    ui.replace_text("name", u"測試使用者");
    check(ui.text_offset("name") == 0 && ui.undo("name").status == Status::not_supported, "Owner reset clears offset and history");
}
}

int main() {
    const test::Case cases[] = {
        {"AC05 long label wraps and pushes following controls down", long_label_wraps_and_pushes_following_controls_down},
        {"AC05 Latin words wrap at spaces", latin_words_wrap_at_spaces},
        {"AC12 field roles stay with their field in both appearances", field_roles_stay_with_their_field_in_both_appearances},
        {"AC13 narrow window moves results below", narrow_window_moves_results_below},
        {"AC13 scrolling reveals focus and clips hits", scrolling_reveals_focus_and_clips_hits},
        {"R06 resize after deep scroll settles in the first frame", resize_after_deep_scroll_settles_in_the_first_frame},
        {"AC08 pointer hits map to the painted caret positions", pointer_hits_map_to_the_painted_caret_positions},
        {"AC04 long text keeps caret visible and reset clears offset", long_text_keeps_caret_visible_and_reset_clears_offset},
    };
    return test::run("layout", cases);
}
