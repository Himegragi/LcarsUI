#include "core_internal.hpp"
#include "test_component.hpp"
// B01-B03 arrangement through the public layout seam with a deterministic fake text backend.
#include "check.hpp"
#include "fake_measure.hpp"
using namespace lcars;
using test::check;
using test::FakeMeasure;

namespace {
layout::Node host_control(Ui& ui, std::string_view id, int text_size = 0) {
    return layout::control(detail::access(ui).layout_handle(id), text_size);
}
void registered_component_reaches_layout_hit_and_input() {
    constexpr auto test_kind = static_cast<Kind>(100);
    TestComponent component;
    detail::ScopedComponentType registration(test_kind, component);
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        Ui ui({{.id = "probe", .kind = test_kind, .value = u"Test component"}});
        auto& host = detail::access(ui);
        int activations = 0;
        host.bind("probe", [&] { ++activations; return Result{}; });
        FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(host_control(ui, "probe"), ui, *appearance, measure, 200, 100, 1.f, scroll);
        const auto* hit = frame.hit(4, 4);
        check(hit && hit->id == "probe" && hit->lines[0].text == u"Test component", "Registered component is arranged and hit");
        check(hit->bounds.w == 80 && hit->bounds.h == 30 && !frame.hit(60, 4), "Registered geometry and hit policy are honored");
        check(static_cast<bool>(host.pointer_down(hit->id)) && static_cast<bool>(host.pointer_up(hit->id)), "Pointer reaches registered behavior");
        check(static_cast<bool>(host.key_down({.key = Key::enter})), "Keyboard reaches registered behavior");
        check(activations == 2, "Pointer and keyboard each activate once");
    }
}
void layout_rejects_foreign_or_expired_handles() {
    Ui first({}), second({});
    auto first_button = first.add_button({.id = "same", .kind = Kind::button});
    auto second_button = second.add_button({.id = "same", .kind = Kind::button});
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::control(first_button), first, web_appearance(), measure, 200, 100, 1.f, scroll);
    check(frame.find(first_button) && !frame.find(second_button), "Placement uses component identity, not its string ID");
    bool rejected = false;
    try { arrange(layout::control(second_button), first, web_appearance(), measure, 200, 100, 1.f, scroll); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "A foreign component cannot enter another tree's layout");
    layout::Node expired;
    { Ui temporary({}); expired = layout::control(temporary.add_button({.id = "gone", .kind = Kind::button})); }
    rejected = false;
    try { arrange(expired, first, web_appearance(), measure, 200, 100, 1.f, scroll); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "An expired component cannot enter a layout");
}
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void long_label_wraps_and_pushes_following_controls_down() {
    Ui ui({{.id = "intro", .kind = Kind::label, .value = u"這是一段很長的說明文字會自動換行\n第二段"},
           {.id = "apply", .kind = Kind::button, .label = u"套用"}});
    auto page = layout::column({host_control(ui, "intro"), host_control(ui, "apply")}, 12);
    FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(page, ui, web_appearance(), measure, 200, 600, 1.f, scroll);
    auto intro = detail::find_placement(frame, "intro");
    auto apply = detail::find_placement(frame, "apply");
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

void command_kinds_are_placed_like_buttons() {
    Ui ui({{.id = "tips", .kind = Kind::toggle, .label = u"啟用提示"}, {.id = "next", .kind = Kind::navigation, .label = u"下一頁"}});
    FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(layout::column({host_control(ui, "tips"), host_control(ui, "next")}), ui, web_appearance(), measure, 400, 300, 1.f, scroll);
    auto tips = detail::find_placement(frame, "tips"), next = detail::find_placement(frame, "next");
    check(tips && next && tips->lines.size() == 1 && tips->lines[0].text == u"啟用提示" && next->lines[0].text == u"下一頁",
          "Toggle and navigation carry their titles instead of being treated as text fields");
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
        auto page = layout::column({host_control(ui, "name"), host_control(ui, "apply")}, 12);
        FakeMeasure measure;
        ScrollState scroll;
        auto before = arrange(page, ui, *appearance, measure, 240, 600, 1.f, scroll);
        detail::access(ui).set_error("name", u"名稱不能空白，也不能只有空格或全形空白字元");
        auto after = arrange(page, ui, *appearance, measure, 240, 600, 1.f, scroll);
        auto field = detail::find_placement(after, "name");
        auto apply = detail::find_placement(after, "apply");
        check(field->label_lines.size() == 1 && field->label_lines[0].text == u"顯示名稱", "Name label is carried by its field");
        check(detail::find_placement(before, "name")->error_lines.empty() && field->error_lines.size() >= 2, "Error appears and wraps over lines");
        for (const auto& line : field->error_lines)
            check(inside(line.bounds, field->bounds), "Every error line stays inside its field");
        check(apply->bounds.y >= field->bounds.y + field->bounds.h && apply->bounds.y > detail::find_placement(before, "apply")->bounds.y,
              "Re-measured error pushes the next control down");
        check(!detail::find_placement(after, "name.error") && !detail::find_placement(after, "name.label"), "Role labels are not placed twice");
    }
}

std::vector<Control> page_controls() {
    auto controls = field_controls();
    controls.push_back({.id = "long", .kind = Kind::label, .value = std::u16string(400, u'長')});
    controls.push_back({.id = "result", .kind = Kind::label, .value = u"上次套用結果：測試使用者"});
    controls.push_back({.id = "reset", .kind = Kind::button, .label = u"重設"});
    return controls;
}
layout::Node page_layout(Ui& ui) {
    using namespace layout;
    return scroll(column({host_control(ui, "long"),
                          grid({column({host_control(ui, "name"), host_control(ui, "apply")}), host_control(ui, "result")}, 380, 20),
                          host_control(ui, "reset")},
                         12, 24));
}

void narrow_window_moves_results_below() {
    Ui ui(page_controls());
    FakeMeasure measure;
    for (const auto* appearance : appearances)
    for (float scale : {1.f, 1.5f}) {
        ScrollState scroll;
        auto d = [&](int v) { return static_cast<int>(v * scale); };
        auto wide = arrange(page_layout(ui), ui, *appearance, measure, d(960), d(720), scale, scroll);
        auto narrow = arrange(page_layout(ui), ui, *appearance, measure, d(640), d(480), scale, scroll);
        auto beside = detail::find_placement(wide, "result")->bounds, below = detail::find_placement(narrow, "result")->bounds;
        check(beside.x > detail::find_placement(wide, "name")->bounds.x && beside.y == detail::find_placement(wide, "name")->bounds.y, "960 DIP: results sit beside the form");
        check(below.x == detail::find_placement(narrow, "name")->bounds.x && below.y > detail::find_placement(narrow, "apply")->bounds.y, "640 DIP: results move below the form");
    }
}

void scrolling_reveals_focus_and_clips_hits() {
    Ui ui(page_controls());
    FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(page_layout(ui), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    check(frame.maximum > 0 && frame.thumb.h > 0, "Overflowing content gets a scroll range and a thumb");
    auto hidden = detail::find_placement(frame, "reset")->bounds;
    check(hidden.y >= frame.viewport.y + frame.viewport.h, "Reset starts outside the viewport");
    check(frame.hit(hidden.x + 2, hidden.y + 2) == nullptr, "Nothing outside the viewport can be hit");
    frame = arrange(page_layout(ui), ui, web_appearance(), measure, 640, 480, 1.f, scroll, "reset");
    auto shown = detail::find_placement(frame, "reset")->bounds;
    check(shown.y >= 0 && shown.y + shown.h == 480 && frame.offset > 0, "Revealing reset scrolls just enough to show all of it");
    auto hit = frame.hit(shown.x + 2, shown.y + 2);
    check(hit && hit->id == "reset", "Hit testing uses the scrolled positions");
    scroll.offset = 1 << 20;
    frame = arrange(page_layout(ui), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    check(frame.offset == frame.maximum && scroll.offset == frame.maximum, "Wheel overshoot is clamped before placement");
    check(frame.offset_for_thumb(frame.track.y) == 0 && frame.offset_for_thumb(frame.track.y + frame.track.h) == frame.maximum,
          "The scrollbar thumb maps onto the whole range");
}

void resize_after_deep_scroll_settles_in_the_first_frame() {
    Ui ui(page_controls());
    FakeMeasure measure;
    ScrollState scroll;
    arrange(page_layout(ui), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    scroll.offset = 1 << 20;
    arrange(page_layout(ui), ui, web_appearance(), measure, 640, 480, 1.f, scroll);
    auto first = arrange(page_layout(ui), ui, web_appearance(), measure, 960, 900, 1.f, scroll);
    auto second = arrange(page_layout(ui), ui, web_appearance(), measure, 960, 900, 1.f, scroll);
    check(first.offset == second.offset && first.offset <= first.maximum, "Scroll offset is settled before the first frame");
    check(first.items.size() == second.items.size(), "Same items in both frames");
    for (std::size_t i = 0; i < first.items.size(); ++i)
        check(first.items[i].bounds == second.items[i].bounds, "Every control sits where the next frame puts it");
    auto name = detail::find_placement(first, "name")->bounds;
    auto hit = second.hit(name.x + 4, name.y + name.h - 4);
    check(name.y >= 0 && name.y + name.h <= 900 && hit && hit->id == "name", "Name is visible and hit where the first frame painted it");
}

void pointer_hits_map_to_the_painted_caret_positions() {
    for (float scale : {1.f, 1.5f}) {
        Ui ui({{.id = "name", .kind = Kind::text, .value = u"Ab中\U00020000文"}});
        FakeMeasure measure;
        ScrollState scroll;
        auto frame = arrange(layout::column({host_control(ui, "name")}), ui, material_appearance(), measure, 400, 200, scale, scroll);
        auto field = detail::find_placement(frame, "name");
        int origin = field->bounds.x + field->field.editor.x;
        for (auto [offset, x] : field->run.stops) {
            int painted = origin + static_cast<int>(x - field->offset);
            check(detail::placement_text_offset(frame, "name", painted) == offset, "Pointer at a painted caret returns that caret");
        }
        check(detail::placement_text_offset(frame, "name", origin + static_cast<int>(field->run.x(3) + field->text_size * 0.4f)) == 3,
              "A pointer inside the left part of the surrogate pair snaps to its start");
        check(field->text_size == static_cast<int>(16 * scale + 0.5f), "Text size follows the render scale");
    }
}

void long_text_keeps_caret_visible_and_reset_clears_offset() {
    Ui ui({});
    auto name_field = ui.add_text({.id = "name", .kind = Kind::text, .value = std::u16string(60, u'A')});
    ui.add_button({.id = "apply", .kind = Kind::button});
    name_field.connect_source({[] { return std::u16string(60, u'A'); }, [](std::function<void()>) { return [] {}; }});
    name_field.connect_confirmed([](const TextChange&) { return Result{}; });
    FakeMeasure measure;
    ScrollState scroll;
    auto page = layout::column({host_control(ui, "name"), host_control(ui, "apply")});
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 60, 60);
    detail::access(ui).text_input(u"Z");
    auto frame = arrange(page, ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    auto field = detail::find_placement(frame, "name");
    float caret = field->run.x(61) - field->offset;
    check(field->offset > 0 && caret >= 0 && caret <= field->field.editor.w, "Caret at the end stays inside the editor");
    detail::access(ui).focus("apply");
    arrange(page, ui, web_appearance(), measure, 300, 200, 1.f, scroll);
    check(detail::access(ui).text_offset("name") > 0, "Leaving the field keeps its display offset");
    detail::access(ui).replace_text("name", u"測試使用者");
    check(detail::access(ui).text_offset("name") == 0 && detail::access(ui).undo("name").status == Status::empty_history, "Owner reset clears offset and history");
}
}

void radio_options_share_geometry_with_pointer_input() {
    for (auto* look : {&web_appearance(), &material_appearance()}) {
        std::optional<OptionValue> model;
        Ui ui({});
        auto group = ui.add_radio_group({.id = "mode", .kind = Kind::radio_group, .label = u"Mode"},
            {{{"normal"}, u"Normal"}, {{"focus"}, u"Focus"}, {{"offline"}, u"Offline", false}});
        group.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
        group.connect_immediate([&](const OptionChange& c) { model = c.value; return Result{}; });
        FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::control(group), ui, *look, measure, 320, 240, 1.f, scroll);
        auto item = frame.find(group);
        check(item && item->options.size() == 3, "All options have independently placed hit targets");
        auto rect = item->options[1];
        auto& host = detail::access(ui);
        host.pointer_down(item->id, frame.option_at(*item, rect.x + 2, rect.y + 2));
        host.pointer_up(item->id, frame.option_at(*item, rect.x + 2, rect.y + 2));
        check(model == OptionValue{"focus"}, "Clicking the second painted option selects it");
        rect = item->options[2];
        host.pointer_down(item->id, frame.option_at(*item, rect.x + 2, rect.y + 2));
        host.pointer_up(item->id, frame.option_at(*item, rect.x + 2, rect.y + 2));
        check(model == OptionValue{"focus"}, "Clicking a disabled option leaves the selection unchanged");
    }
}

void slider_track_and_drag_share_geometry() {
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        Ui ui({});
        auto slider = ui.add_slider({.id = "volume", .kind = Kind::slider, .label = u"Volume"}, {0, 100, 5});
        slider.connect_source({[] { return 40; }, [](std::function<void()>) { return [] {}; }});
        slider.connect_confirmed([](const NumericChange&) { return Result{}; });
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::control(slider), ui, *appearance, measure, 400, 200, 1.f, scroll);
        const auto* item = frame.find(slider);
        check(item && item->slider_track.w > 0 && frame.hit(item->slider_track.x, item->slider_track.y)->id == "volume", "The drawn track is a pointer target");
        detail::access(ui).pointer_down(item->id);
        detail::access(ui).slider_pointer(item->id, item->slider_track.x, item->slider_track);
        check(*slider.draft() == 0, "Track start reaches minimum");
        detail::access(ui).slider_pointer(item->id, item->slider_track.x + item->slider_track.w, item->slider_track);
        check(*slider.draft() == 100, "Dragging to the track end reaches maximum");
    }
}

void dropdown_popup_overlaps_and_uses_topmost_hit() {
    for (const auto* look : {&web_appearance(), &material_appearance()}) {
        Ui ui({});
        auto field = ui.add_dropdown({.id = "region", .kind = Kind::dropdown, .label = u"Region"},
            {{{"north"}, u"North"}, {{"south"}, u"South"}});
        auto behind = ui.add_button({.id = "behind", .kind = Kind::button, .label = u"Behind"});
        field.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
        field.connect_confirmed([](const DropdownChange&) { return Result{}; });
        field.open();
        FakeMeasure measure; ScrollState scroll;
        auto page = layout::column({layout::control(field), layout::control(behind)}, 0);
        auto frame = arrange(page, ui, *look, measure, 240, 160, 1.f, scroll);
        check(frame.overlays.size() == 1 && frame.overlays[0].options.size() == 2, "Popup has two shared overlay targets");
        auto row = frame.overlays[0].options[0];
        auto hit = frame.hit(row.x + 2, row.y + 2);
        check(hit && hit->popup && hit->id == "region" && frame.option_at(*hit, row.x + 2, row.y + 2) == 0,
              "Popup is hit above the underlying control");
        check(row.x >= 0 && row.y >= 0 && row.x + row.w <= 240 && row.y + row.h <= 160, "Popup fits the window");
    }
}

void dropdown_states_are_readable_without_color() {
    Ui ui({});
    auto field = ui.add_dropdown({.id = "region", .kind = Kind::dropdown, .label = u"Region"}, {});
    field.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    field.connect_confirmed([](const DropdownChange&) { return Result{}; });
    FakeMeasure measure; ScrollState scroll;
    field.open();
    auto frame = arrange(layout::control(field), ui, web_appearance(), measure, 200, 160, 1.f, scroll);
    check(frame.find(field) && frame.find(field)->lines.size() >= 2 && !frame.overlays.empty() && !frame.overlays[0].lines.empty() &&
          frame.find(field)->lines[1].text == u"\u672a\u9078\u53d6" && frame.overlays[0].lines[0].text == u"\u6c92\u6709\u53ef\u7528\u9078\u9805", "Empty dropdown explains its state in text");
    field.enable(false);
    frame = arrange(layout::control(field), ui, web_appearance(), measure, 200, 160, 1.f, scroll);
    check(frame.find(field)->lines[0].text.find(u"\u505c\u7528") != std::u16string::npos, "Disabled dropdown is labeled in text");
}

void stack_and_popup_edges_follow_paint_order() {
    for (auto* look : {&web_appearance(), &material_appearance()}) {
        for (float scale : {1.f, 1.5f}) {
            Ui ui({});
            auto lower = ui.add_button({.id = "lower", .kind = Kind::button, .label = u"Lower"});
            auto upper = ui.add_button({.id = "upper", .kind = Kind::button, .label = u"Upper"});
            FakeMeasure measure; ScrollState scroll;
            auto stacked = arrange(layout::stack({layout::control(lower), layout::control(upper)}), ui, *look, measure, 200, 180, scale, scroll);
            auto p = stacked.find(upper);
            check(p && stacked.hit(p->bounds.x + 2, p->bounds.y + 2)->id == "upper", "Later stack child wins overlapping hits");
            Ui other({});
            auto filler = other.add_label({.id = "filler", .kind = Kind::label, .value = u"One\nTwo\nThree\nFour"});
            auto dropdown = other.add_dropdown({.id = "region", .kind = Kind::dropdown, .label = u"Region"},
                {{{"north"}, u"North"}, {{"central"}, u"Central"}, {{"south"}, u"South"}});
            dropdown.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
            dropdown.connect_confirmed([](const DropdownChange&) { return Result{}; });
            dropdown.open();
            auto page = layout::scroll(layout::column({layout::control(filler), layout::control(dropdown)}, 0));
            auto frame = arrange(page, other, *look, measure, static_cast<int>(240 * scale), static_cast<int>(180 * scale), scale, scroll, "region");
            check(frame.overlays.size() == 1 && frame.overlays[0].bounds.y < frame.find(dropdown)->bounds.y &&
                  inside(frame.overlays[0].bounds, {0, 0, static_cast<int>(240 * scale), static_cast<int>(180 * scale)}),
                  "Popup flips above a bottom-edge anchor and stays in the window");
        }
    }
}

void list_reveals_the_keyboard_row_and_clips_hits() {
    for (const auto* look : appearances) {
        Ui ui({}); std::vector<RadioOption> items;
        for (int i = 1; i <= 100; ++i) items.push_back({{std::to_string(i)}, u"Row"});
        auto list = ui.add_list({.id = "list", .kind = Kind::list}, std::move(items));
        std::optional<OptionValue> selected;
        list.connect_source({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
        list.connect_immediate([&](std::optional<OptionValue> value) { selected = value; return Result{}; });
        list.focus(); detail::access(ui).key_down({.key = Key::end});
        FakeMeasure measure; ScrollState scroll;
        auto page = layout::scroll(layout::control(list));
        auto frame = arrange(page, ui, *look, measure, 640, 480, 1.f, scroll, "list");
        auto item = frame.find(list);
        check(item && item->options.size() == 100 && inside(item->options.back(), frame.viewport), "End reveals the whole last row");
        check(!frame.option_at(*item, item->options.front().x + 2, item->options.front().y + 2), "Clipped rows cannot be hit");
    }
}
void assembled_layout_handles_long_text_and_larger_type() {
    for (const auto* look : appearances) for (int font : {16, 24}) {
        Ui ui({});
        auto left = ui.add_label({.id = "left", .kind = Kind::label, .value = u"很長的說明文字會在狹窄空間中重新換行並保留間距"});
        auto right = ui.add_label({.id = "right", .kind = Kind::label, .value = u"右側標籤"});
        auto below = ui.add_label({.id = "below", .kind = Kind::label, .value = u"下方結果"});
        auto page = layout::column({layout::row({layout::control(left, font), layout::control(right, font)}, 12),
                                    layout::grid({layout::control(below, font)}, 160)}, 14, 8);
        FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(page, ui, *look, measure, 240, 600, 1.f, scroll);
        auto a = frame.find(left)->bounds, b = frame.find(right)->bounds, c = frame.find(below)->bounds;
        check(a.x + a.w + 12 <= b.x && c.y >= std::max(a.y + a.h, b.y + b.h) + 14 && c.x >= 8 && c.x + c.w <= 232,
              "Row, column and grid retain their gaps and bounds at a narrow width and larger type");
    }
}

int main() {
    const test::Case cases[] = {
        {"R02 list keyboard row reveal and clipped hit", list_reveals_the_keyboard_row_and_clips_hits},
        {"N06 assembled layout with long text and type size", assembled_layout_handles_long_text_and_larger_type},
        {"B04 popup topmost hit and placement", dropdown_popup_overlaps_and_uses_topmost_hit},
        {"R05 dropdown states are readable", dropdown_states_are_readable_without_color},
        {"B04 stack and popup edge placement", stack_and_popup_edges_follow_paint_order},
        {"C07 slider track and drag", slider_track_and_drag_share_geometry},
        {"C05 radio pointer geometry", radio_options_share_geometry_with_pointer_input},
        {"Registered component reaches layout, hit and input", registered_component_reaches_layout_hit_and_input},
        {"Layout rejects foreign and expired handles", layout_rejects_foreign_or_expired_handles},
        {"AC05 long label wraps and pushes following controls down", long_label_wraps_and_pushes_following_controls_down},
        {"AC05 Latin words wrap at spaces", latin_words_wrap_at_spaces},
        {"B01 command kinds are placed like buttons", command_kinds_are_placed_like_buttons},
        {"AC12 field roles stay with their field in both appearances", field_roles_stay_with_their_field_in_both_appearances},
        {"AC13 narrow window moves results below", narrow_window_moves_results_below},
        {"AC13 scrolling reveals focus and clips hits", scrolling_reveals_focus_and_clips_hits},
        {"R06 resize after deep scroll settles in the first frame", resize_after_deep_scroll_settles_in_the_first_frame},
        {"AC08 pointer hits map to the painted caret positions", pointer_hits_map_to_the_painted_caret_positions},
        {"AC04 long text keeps caret visible and reset clears offset", long_text_keeps_caret_visible_and_reset_clears_offset},
    };
    return test::run("layout", cases);
}
