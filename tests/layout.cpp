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
struct AlternatePresentation final : Appearance {
    const Appearance& base = web_appearance();
    std::string_view name() const override { return "alternate"; }
    Palette palette() const override { return base.palette(); }
    FieldParts field(Rect r, int size) const override { return base.field(r, size); }
    int radius() const override { return base.radius(); }
    FieldLayout arrange_field(int w, const FieldText& text, float scale) const override { return base.arrange_field(w, text, scale); }
    int button_height(int line, float scale) const override { return base.button_height(line, scale); }
    int button_padding(float scale) const override { return base.button_padding(scale); }
    ComponentPresentation presentation = [] {
        ComponentPresentation value;
        value.disabled_suffix = u" [off]";
        value.radio_label_offset = 42;
        return value;
    }();
    const ComponentPresentation& component_presentation() const override { return presentation; }
};
void appearance_controls_component_words_and_geometry() {
    Ui ui;
    auto radio = ui.add_radio_group({.id = "radio", .label = u"Mode"},
                                  {{{"a"}, u"Alpha", false}}).value.value();
    FakeMeasure measure; ScrollState scroll;
    AlternatePresentation alternate;
    auto frame = arrange(layout::control(radio), ui, alternate, measure, 300, 200, 1.f, scroll);
    auto* placed = frame.find(radio);
    check(placed && placed->lines.size() == 2 && placed->lines[1].text == u"Alpha [off]" &&
          placed->lines[1].bounds.x == 42, "Appearance controls disabled wording and option geometry");
}

void registered_component_reaches_layout_hit_and_input() {
    constexpr auto test_kind = static_cast<ComponentKind>(100);
    TestComponent component;
    detail::ScopedComponentType registration(test_kind, component);
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        Ui ui; detail::access(ui).seed_test_controls({{test_kind, {.id = "probe", .value = u"Test component"}}});
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
    Ui first, second;
    auto first_button = first.add_button({.id = "same"}).value.value();
    auto second_button = second.add_button({.id = "same"}).value.value();
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::control(first_button), first, web_appearance(), measure, 200, 100, 1.f, scroll);
    check(frame.find(first_button) && !frame.find(second_button), "Placement uses component identity, not its string ID");
    bool rejected = false;
    try { arrange(layout::control(second_button), first, web_appearance(), measure, 200, 100, 1.f, scroll); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "A foreign component cannot enter another tree's layout");
    layout::Node expired;
    { Ui temporary; expired = layout::control(temporary.add_button({.id = "gone"}).value.value()); }
    rejected = false;
    try { arrange(expired, first, web_appearance(), measure, 200, 100, 1.f, scroll); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "An expired component cannot enter a layout");
}
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void long_label_wraps_and_pushes_following_controls_down() {
    Ui ui; detail::access(ui).seed_test_controls({{ComponentKind::label, {.id = "intro", .value = u"這是一段很長的說明文字會自動換行\n第二段"}},
           {ComponentKind::button, {.id = "apply", .label = u"套用"}}});
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

void disabled_button_cue_fits_narrow_bounds() {
    for (const auto* look : {&web_appearance(), &material_appearance()}) {
        Ui ui;
        auto button = ui.add_button({.id = "button", .label = u"A long control name", .enabled = false}).value.value();
        auto next = ui.add_label({.id = "next", .value = u"Next"}).value.value();
        FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::column({layout::control(button), layout::control(next)}, 8), ui, *look, measure, 120, 400, 1.f, scroll);
        auto* item = frame.find(button); auto* following = frame.find(next);
        check(item && following && item->lines.size() > 1, "Narrow disabled button wraps its text");
        std::u16string text;
        for (const auto& line : item->lines) {
            text += line.text;
            check(intersect(line.bounds, item->bounds) == line.bounds, "Each disabled button line stays inside its bounds");
        }
        check(text.find(u"A long control name") != std::u16string::npos &&
              text.find(look->component_presentation().disabled_suffix) != std::u16string::npos &&
              following->bounds.y >= item->bounds.y + item->bounds.h + 8,
              "Name and disabled cue fit without covering the following control");
    }
}

void command_kinds_are_placed_like_buttons() {
    Ui ui; detail::access(ui).seed_test_controls({{ComponentKind::button, {.id = "tips", .label = u"啟用提示"}}, {ComponentKind::button, {.id = "next", .label = u"下一頁"}}});
    FakeMeasure measure;
    ScrollState scroll;
    auto frame = arrange(layout::column({host_control(ui, "tips"), host_control(ui, "next")}), ui, web_appearance(), measure, 400, 300, 1.f, scroll);
    auto tips = detail::find_placement(frame, "tips"), next = detail::find_placement(frame, "next");
    check(tips && next && tips->lines.size() == 1 && tips->lines[0].text == u"啟用提示" && next->lines[0].text == u"下一頁",
          "Toggle and navigation carry their titles instead of being treated as text fields");
}

std::vector<detail::Component> field_controls() {
    return {{ComponentKind::label, {.id = "name.label", .value = u"顯示名稱", .target = "name", .label_kind = LabelKind::name}},
            {ComponentKind::text, {.id = "name", .value = u"測試使用者"}},
            {ComponentKind::label, {.id = "name.error", .target = "name", .label_kind = LabelKind::error}},
            {ComponentKind::button, {.id = "apply", .label = u"套用"}}};
}
const Appearance* appearances[] = {&web_appearance(), &material_appearance()};
bool inside(Rect inner, Rect outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

void field_roles_stay_with_their_field_in_both_appearances() {
    for (const auto* appearance : appearances) {
        Ui ui; detail::access(ui).seed_test_controls(field_controls());
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

std::vector<detail::Component> page_controls() {
    auto controls = field_controls();
    controls.push_back({ComponentKind::label, {.id = "long", .value = std::u16string(400, u'長')}});
    controls.push_back({ComponentKind::label, {.id = "result", .value = u"上次套用結果：測試使用者"}});
    controls.push_back({ComponentKind::button, {.id = "reset", .label = u"重設"}});
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
    Ui ui; detail::access(ui).seed_test_controls(page_controls());
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
    Ui ui; detail::access(ui).seed_test_controls(page_controls());
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
    Ui ui; detail::access(ui).seed_test_controls(page_controls());
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
        Ui ui; detail::access(ui).seed_test_controls({{ComponentKind::text, {.id = "name", .value = u"Ab中\U00020000文"}}});
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
    Ui ui;
    auto name_field = ui.add_text({.id = "name", .value = std::u16string(60, u'A')}).value.value();
    ui.add_button({.id = "apply"}).value.value();
    name_field.connect_source({[] { return std::u16string(60, u'A'); }, [](std::function<void()>) { return [] {}; }});
    name_field.connect_confirmed([](const Change<TextHandle>&) { return Result{}; });
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
        Ui ui;
        auto group = ui.add_radio_group({.id = "mode", .label = u"Mode"},
            {{{"normal"}, u"Normal"}, {{"focus"}, u"Focus"}, {{"offline"}, u"Offline", false}}).value.value();
        group.connect_source({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
        group.connect_immediate([&](const Change<RadioGroupHandle>& c) { model = c.value; return Result{}; });
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
        Ui ui;
        auto slider = ui.add_slider({.id = "volume", .label = u"Volume"}, {0, 100, 5}).value.value();
        slider.connect_source({[] { return 40; }, [](std::function<void()>) { return [] {}; }});
        slider.connect_confirmed([](const Change<SliderHandle>&) { return Result{}; });
        test::FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(layout::control(slider), ui, *appearance, measure, 400, 200, 1.f, scroll);
        const auto* item = frame.find(slider);
        check(item && item->slider_track.w > 0 && frame.hit(item->slider_track.x, item->slider_track.y)->id == "volume", "The drawn track is a pointer target");
        detail::access(ui).pointer_down(item->id);
        detail::access(ui).slider_pointer_down(item->id, item->slider_track.x, item->slider_track);
        check(*slider.draft() == 0, "Track start reaches minimum");
        detail::access(ui).slider_pointer(item->id, item->slider_track.x + item->slider_track.w);
        check(*slider.draft() == 100, "Dragging to the track end reaches maximum");
    }
}

void dropdown_popup_overlaps_and_uses_topmost_hit() {
    for (const auto* look : {&web_appearance(), &material_appearance()}) {
        Ui ui;
        auto field = ui.add_dropdown({.id = "region", .label = u"Region"},
            {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
        auto behind = ui.add_button({.id = "behind", .label = u"Behind"}).value.value();
        field.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
        field.connect_confirmed([](const Change<DropdownHandle>&) { return Result{}; });
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
    Ui ui;
    auto field = ui.add_dropdown({.id = "region", .label = u"Region"}, {}).value.value();
    field.connect_source({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    field.connect_confirmed([](const Change<DropdownHandle>&) { return Result{}; });
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
            Ui ui;
            auto lower = ui.add_button({.id = "lower", .label = u"Lower"}).value.value();
            auto upper = ui.add_button({.id = "upper", .label = u"Upper"}).value.value();
            FakeMeasure measure; ScrollState scroll;
            auto stacked = arrange(layout::stack({layout::control(lower), layout::control(upper)}), ui, *look, measure, 200, 180, scale, scroll);
            auto p = stacked.find(upper);
            check(p && stacked.hit(p->bounds.x + 2, p->bounds.y + 2)->id == "upper", "Later stack child wins overlapping hits");
            Ui other;
            auto filler = other.add_label({.id = "filler", .value = u"One\nTwo\nThree\nFour"}).value.value();
            auto dropdown = other.add_dropdown({.id = "region", .label = u"Region"},
                {{{"north"}, u"North"}, {{"central"}, u"Central"}, {{"south"}, u"South"}}).value.value();
            dropdown.connect_source({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
            dropdown.connect_confirmed([](const Change<DropdownHandle>&) { return Result{}; });
            dropdown.open();
            auto page = layout::scroll(layout::column({layout::control(filler), layout::control(dropdown)}, 0));
            auto frame = arrange(page, other, *look, measure, static_cast<int>(240 * scale), static_cast<int>(180 * scale), scale, scroll, "region");
            check(frame.overlays.size() == 1 && frame.overlays[0].bounds.y < frame.find(dropdown)->bounds.y &&
                  inside(frame.overlays[0].bounds, {0, 0, static_cast<int>(240 * scale), static_cast<int>(180 * scale)}),
                  "Popup flips above a bottom-edge anchor and stays in the window");
        }
    }
}

void cross_page_error_is_revealed_in_first_frame() {
    for (const auto* look : {&web_appearance(), &material_appearance()}) {
        Ui ui;
        auto current = ui.add_text({.id = "current"}).value.value();
        auto remote = ui.add_text({.id = "remote"}).value.value();
        auto filler = ui.add_label({.id = "filler", .value = u"1\n2\n3\n4\n5\n6\n7\n8\n9\n10"}).value.value();
        auto submit = ui.add_button({.id = "apply"}).value.value();
        auto source = Source<std::u16string>{[] { return std::u16string(u"value"); }, [](std::function<void()>) { return [] {}; }};
        current.connect_source(source); remote.connect_source(source);
        auto tabs = ui.add_tabs({.id = "tabs"}, {
            {{"one"}, u"One", true, {current, submit}}, {{"two"}, u"Two", true, {filler, remote}}}).value.value();
        ui.connect_mixed_confirmation(submit, {remote, current}, [=](const auto&) {
            return MixedConfirmationResult{{Status::invalid_value, u"Invalid"}, {{current, u"Current error"}, {remote, u"Remote error"}}};
        });
        auto root = layout::pinned(layout::control(tabs), layout::scroll(layout::column({layout::control(current), layout::control(submit), layout::control(filler), layout::control(remote)})));
        submit.invoke();
        FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(root, ui, *look, measure, 400, 200, 1.f, scroll, detail::access(ui).focused());
        auto field = frame.find(remote);
        check(*tabs.active() == OptionValue{"two"} && field && field->bounds.y >= frame.viewport.y &&
              field->bounds.y + field->bounds.h <= frame.viewport.y + frame.viewport.h && scroll.offset > 0,
              "The first invalid field's page is selected and revealed before the first frame");
        check(frame.find(tabs)->lines[0].text.find(u"!") != std::u16string::npos, "Other page error is visible on its tab");
    }
}

void inactive_page_members_do_not_paint_or_hit() {
    Ui ui;
    auto first = ui.add_button({.id = "one", .label = u"One"}).value.value();
    auto second = ui.add_button({.id = "two", .label = u"Two"}).value.value();
    auto tabs = ui.add_tabs({.id = "tabs"}, {{{"one"}, u"One", true, {first}}, {{"two"}, u"Two", true, {second}}}).value.value();
    auto root = layout::column({layout::control(tabs), layout::control(first), layout::control(second)});
    for (const auto* look : {&web_appearance(), &material_appearance()}) {
        FakeMeasure measure; ScrollState scroll;
        second.enable(false);
        auto frame = arrange(root, ui, *look, measure, 400, 300, 1.f, scroll);
        check(frame.find(first) && !frame.find(second), "Disabled controls on hidden pages are not laid out");
        second.enable(true);
        tabs.activate({"two"});
        frame = arrange(root, ui, *look, measure, 400, 300, 1.f, scroll);
        check(!frame.find(first) && frame.find(second), "Switch replaces the visible page");
        tabs.activate({"one"});
    }
}

void list_reveals_the_keyboard_row_and_clips_hits() {
    for (const auto* look : appearances) {
        Ui ui; std::vector<RadioOption> items;
        for (int i = 1; i <= 100; ++i) items.push_back({{std::to_string(i)}, u"Row"});
        auto list = ui.add_list({.id = "list"}, std::move(items)).value.value();
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
void nested_scroll_reveals_the_list_target() {
    for (const auto* look : appearances) {
        Ui ui;
        auto filler = ui.add_label({.id = "filler", .value = u"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12"}).value.value();
        std::vector<RadioOption> options;
        for (int i = 1; i <= 100; ++i) options.push_back({{std::to_string(i)}, u"Row"});
        auto list = ui.add_list({.id = "list"}, std::move(options)).value.value();
        std::optional<OptionValue> selected;
        list.connect_source({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
        list.connect_immediate([&](std::optional<OptionValue> value) { selected = value; return Result{}; });
        list.focus(); detail::access(ui).key_down({.key = Key::end});
        ScrollState outer, inner; FakeMeasure measure;
        auto page = layout::scroll(layout::column({layout::control(filler), layout::scroll(layout::control(list), inner, 140)}));
        auto frame = arrange(page, ui, *look, measure, 360, 200, 1.f, outer, "list");
        auto item = frame.find(list);
        check(item && item->options.size() == 100 && inside(item->options.back(), item->clip) &&
              outer.offset > 0 && inner.offset > 0 && selected == OptionValue{"100"},
              "End reveals the whole last row through inner and outer viewports on the first frame");
        check(item && !frame.option_at(*item, item->options.front().x + 2, item->options.front().y + 2),
              "Rows outside the combined clip cannot be hit");
    }
}
void assembled_layout_handles_long_text_and_larger_type() {
    for (const auto* look : appearances) for (int font : {16, 24}) {
        Ui ui;
        auto left = ui.add_label({.id = "left", .value = u"很長的說明文字會在狹窄空間中重新換行並保留間距"}).value.value();
        auto right = ui.add_label({.id = "right", .value = u"右側標籤"}).value.value();
        auto below = ui.add_label({.id = "below", .value = u"下方結果"}).value.value();
        auto page = layout::column({layout::row({layout::control(left, font), layout::control(right, font)}, 12),
                                    layout::grid({layout::control(below, font)}, 160)}, 14, 8);
        FakeMeasure measure; ScrollState scroll;
        auto frame = arrange(page, ui, *look, measure, 240, 600, 1.f, scroll);
        auto a = frame.find(left)->bounds, b = frame.find(right)->bounds, c = frame.find(below)->bounds;
        check(a.x + a.w + 12 <= b.x && c.y >= std::max(a.y + a.h, b.y + b.h) + 14 && c.x >= 8 && c.x + c.w <= 232,
              "Row, column and grid retain their gaps and bounds at a narrow width and larger type");
    }
}

void multiple_dialog_layout() {
    Ui ui;
    auto background = ui.add_button({.id="background"}).value.value();
    auto one = ui.add_button({.id="one"}).value.value();
    auto two = ui.add_button({.id="two"}).value.value();
    auto first = ui.add_dialog("first", {one}, one, background, [](bool) {}).value.value();
    auto second = ui.add_dialog("second", {two}, two, background, [](bool) {}).value.value();
    std::vector<DialogLayout> dialogs{{first, layout::control(one)}, {second, layout::control(two)}};
    auto root = layout::modal(layout::control(background), std::move(dialogs));
    FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(root, ui, *appearances[0], measure, 640, 480, 1, scroll);
    check(!frame.modal && frame.overlays.empty(), "closed dialogs leave background alone");
    check(bool(second.open(background)), "second opens");
    frame = arrange(root, ui, *appearances[0], measure, 640, 480, 1, scroll);
    check(frame.modal && frame.find(two) && !frame.find(one), "only active dialog is arranged");
}

void modal_long_dropdown() {
    Ui ui; auto background=ui.add_button({.id="background"}).value.value();
    std::vector<RadioOption> choices;
    for (int i=0; i<150; ++i) choices.push_back({{std::to_string(i)}, u"item"});
    auto dropdown=ui.add_dropdown({.id="modal.dropdown"},choices).value.value();
    dropdown.connect_source({[] { return std::optional<OptionValue>{{"0"}}; }, [](auto) { return [] {}; }});
    dropdown.connect_immediate([](const Change<DropdownHandle>&) { return Result{}; });
    auto dialog=ui.add_dialog("dialog",{dropdown},dropdown,background,[](bool) {}).value.value();
    auto root=layout::modal(layout::control(background),layout::control(dropdown),dialog);
    dialog.open(); dropdown.open(); test::FakeMeasure measure; ScrollState scroll;
    auto frame=arrange(root,ui,web_appearance(),measure,640,480,1,scroll);
    auto popup=std::find_if(frame.overlays.begin(),frame.overlays.end(),[](const auto& p) { return p.dropdown_panel; });
    test::check(popup!=frame.overlays.end(), "dialog dropdown has panel");
    test::check(frame.wheel(ui,popup->bounds.x+2,popup->bounds.y+2,-80), "dialog panel scrolls");
    detail::access(ui).key_down({.key=Key::escape});
    test::check(*dialog.is_open() && !dropdown.describe()->expanded, "Escape closes panel before dialog");
}
void long_dropdown_viewport() {
    for (const auto* look : {&web_appearance(), &material_appearance()}) for (int size : {16, 28}) {
        Ui ui;
        if (size == 28) { PresentationSettings settings; settings.text_size=28; settings.density=Density::compact; ui.set_presentation(settings); }
        std::vector<RadioOption> choices;
        for (int i=0; i<150; ++i) { auto value=std::to_string(i); choices.push_back({{value}, std::u16string(value.begin(),value.end())}); }
        auto dropdown=ui.add_dropdown({.id="long"}, choices).value.value();
        dropdown.connect_source({[] { return std::optional<OptionValue>{{"149"}}; }, [](auto) { return [] {}; }});
        dropdown.connect_immediate([](const Change<DropdownHandle>&) { return Result{}; });
        dropdown.open(); test::FakeMeasure measure; ScrollState scroll;
        auto frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        test::check(frame.overlays.size()==1 && frame.overlays[0].options.size()<=12, "long popup lays out only visible rows");
        auto& state=detail::access(ui);
        auto nodes=ui.semantics().children({"long"});
        test::check(nodes && nodes->size()==150, "all options remain in semantic tree");
        for (const auto& ref : *nodes) test::check(bool(ui.semantics().get(ref)), "each logical option remains queryable");
        auto last=ui.semantics().get({"long", OptionValue{"149"}});
        test::check(last && last->visible, "initial selection is visible");
        test::check(bool(state.key_down({.key=Key::home})), "Home moves candidate");
        frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        auto first=ui.semantics().get({"long", OptionValue{"0"}});
        last=ui.semantics().get({"long", OptionValue{"149"}});
        test::check(first && first->visible && last && !last->visible, "Home reveals first, last is offscreen");
        test::check(bool(state.key_down({.key=Key::end})), "End moves candidate");
        frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        last=ui.semantics().get({"long", OptionValue{"149"}});
        test::check(last && last->visible && dropdown.describe()->candidate->id=="149", "End reveals last candidate");
        state.key_down({.key=Key::home});
        frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        for (int i=1; i<150; ++i) {
            state.key_down({.key=Key::down, .repeat=true});
            frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
            auto candidate=ui.semantics().get({"long",OptionValue{std::to_string(i)}});
            test::check(candidate && candidate->visible && candidate->bounds && candidate->bounds->h==frame.overlays[0].options.back().h, "repeated Down keeps candidate fully visible");
        }
        state.key_down({.key=Key::home});
        frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        auto popup=frame.overlays[0];
        for (std::size_t i=0; i<popup.options.size(); ++i) {
            const auto& row=popup.options[i];
            if (i) test::check(popup.options[i-1].y+popup.options[i-1].h<=row.y, "dropdown rows do not overlap");
            int x=row.x+2, y=row.y+row.h/2;
            if (!popup.clip.contains(x,y)) continue;
            auto logical=frame.option_at(popup,x,y);
            auto semantic=logical ? ui.semantics().get({"long",OptionValue{std::to_string(*logical)}}) : Query<SemanticNode>{};
            test::check(semantic && semantic->name==popup.lines[i].text, "each visible row hit matches its painted label");
        }
        test::check(frame.wheel(ui,popup.bounds.x+2,popup.bounds.y+2,-100), "wheel scrolls popup");
        frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        test::check(dropdown.describe()->expanded && dropdown.draft()->value().id=="149", "wheel preserves popup and value");
        auto region=frame.regions.back();
        test::check(frame.scroll_to(ui,*region.state,region.offset_for_thumb(region.track.y+region.track.h-region.thumb.h)), "drag scrollbar to end");
        frame=arrange(layout::control(dropdown),ui,*look,measure,640,480,1,scroll);
        test::check(ui.semantics().get({"long",OptionValue{"149"}})->visible && dropdown.describe()->expanded, "drag reveals last without closing");
        auto row=frame.overlays[0].options[2];
        auto painted_label=frame.overlays[0].lines[2].text;
        int x=row.x+2,y=row.y+row.h/2;
        auto index=frame.option_at(frame.overlays[0],x,y);
        test::check(index && *index>0, "scrolled hit uses logical option index");
        test::check(bool(state.pointer_down("long",index)) && bool(state.pointer_up("long",index)), "select scrolled option");
        auto selected=dropdown.draft()->value().id;
        test::check(std::u16string(selected.begin(),selected.end())==painted_label, "selected option matches the painted row label");
        auto closed_option = ui.semantics().get({"long", OptionValue{selected}});
        test::check(closed_option && !closed_option->visible && !closed_option->bounds, "collapsed option is offscreen before the next layout");
    }
}
int main() {
    const test::Case cases[] = {{"modal long dropdown", modal_long_dropdown}, {"long dropdown viewport", long_dropdown_viewport},
        {"multiple dialog layout", multiple_dialog_layout},
        {"Appearance controls component words and geometry", appearance_controls_component_words_and_geometry},
        {"G11 reveal cross-page error", cross_page_error_is_revealed_in_first_frame},
        {"C09 only active page is drawn", inactive_page_members_do_not_paint_or_hit},
        {"B03 nested reveal", nested_scroll_reveals_the_list_target},
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
        {"Disabled button cue fits narrow bounds", disabled_button_cue_fits_narrow_bounds},
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
