#include "component_type.hpp"
#include "core_internal.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lcars::detail {
namespace {
std::vector<Line> block(TextMeasure& measure, std::u16string_view text, int size, int width) {
    std::vector<Line> lines;
    if (text.empty()) return lines;
    int y = 0, height = measure.line_height(size);
    for (auto& value : wrap(measure, text, size, width)) {
        lines.push_back({{0, y, width, height}, std::move(value)});
        y += height;
    }
    return lines;
}
int height_of(const std::vector<Line>& lines) {
    return lines.empty() ? 0 : lines.back().bounds.y + lines.back().bounds.h - lines.front().bounds.y;
}
int command_width(const ComponentLayout& c) {
    return static_cast<int>(std::ceil(c.measure.measure(c.control.label, c.placement.text_size).width)) +
           2 * c.appearance.button_padding(c.scale);
}
int scaled(float scale, int value) { return std::max(1, static_cast<int>(std::lround(value * scale))); }
Rect offset(Rect rect, Rect origin) { return {rect.x + origin.x, rect.y + origin.y, rect.w, rect.h}; }
void paint_lines(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    for (const auto& line : lines) c.text(line.text, pixels, line.bounds.x, line.bounds.y, color);
}
class Label final : public ComponentType {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.label_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        p.lines = block(c.measure, c.ui.text(c.control.id), p.text_size, c.width);
        p.bounds = {0, 0, c.width, height_of(p.lines)};
    }
    void paint(ComponentPaint& c) const override {
        paint_lines(c, c.placement.lines, c.placement.text_size, c.appearance.palette().text);
    }
    bool can_focus() const override { return false; }
    bool can_target() const override { return true; }
    bool accepts_pointer() const override { return false; }
};
class Text final : public ComponentType {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.text_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        const auto& control = c.control;
        int line = c.measure.line_height(p.text_size);
        auto semantics = *c.ui.describe(control.id);
        auto name = semantics.name;
        if (control.read_only) name += u"\uff08\u552f\u8b80\uff09";
        if (!control.enabled) name += u"\uff08\u505c\u7528\uff09";
        int caption = p.caption_size = std::max(1, p.text_size * 7 / 8);
        auto probe = c.appearance.arrange_field(c.width, {line, line, line, line}, c.scale);
        auto label = block(c.measure, name, caption, probe.label.w);
        auto description = block(c.measure, semantics.description, caption, probe.description.w);
        auto error = block(c.measure, semantics.error, caption, probe.error.w);
        p.field = c.appearance.arrange_field(c.width, {height_of(label), height_of(description), height_of(error), line}, c.scale);
        auto anchor = [](std::vector<Line>& lines, Rect at) { for (auto& l : lines) { l.bounds.x += at.x; l.bounds.y += at.y; } };
        anchor(label, p.field.label); anchor(description, p.field.description); anchor(error, p.field.error);
        p.label_lines = std::move(label); p.description_lines = std::move(description); p.error_lines = std::move(error);
        p.bounds = {0, 0, c.width, p.field.height};
        const auto* session = c.ui.edit(control.id);
        p.run = c.measure.measure(session->visible(), p.text_size);
        float offset = c.ui.text_offset(control.id) * c.scale;
        float room = static_cast<float>(p.field.editor.w - static_cast<int>(std::lround(2 * c.scale)));
        if (c.ui.focused() == control.id) {
            float caret = p.run.x(session->visible_caret());
            if (caret - offset > room) offset = caret - room;
            if (caret < offset) offset = caret;
        }
        offset = std::clamp(offset, 0.f, std::max(0.f, p.run.width - room));
        c.ui.set_text_offset(control.id, offset / c.scale);
        p.offset = offset;
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement;
        const auto& semantics = c.semantics;
        const auto& f = item.field;
        auto p = c.appearance.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        auto surface = offset(f.surface, item.bounds), editor = offset(f.editor, item.bounds);
        bool focused = semantics.focused, enabled = semantics.enabled;
        int ring = focused ? d(2) : d(1);
        Color edge = semantics.invalid ? p.error : focused ? p.accent : p.border;
        c.rounded(surface, d(c.appearance.radius()), f.underline ? (enabled ? p.surface : p.background) : edge);
        if (!f.underline) {
            Rect inner{surface.x + ring, surface.y + ring, surface.w - 2 * ring, surface.h - 2 * ring};
            c.rounded(inner, std::max(0, d(c.appearance.radius()) - ring), enabled ? p.surface : p.background);
        } else c.fill({surface.x, surface.y + surface.h - 2 * ring, surface.w, 2 * ring}, edge);
        paint_lines(c, item.label_lines, item.caption_size, semantics.invalid ? p.error : p.muted);
        paint_lines(c, item.description_lines, item.caption_size, p.muted);
        paint_lines(c, item.error_lines, item.caption_size, p.error);
        const auto* session = c.ui.edit(item.id);
        c.clip(editor);
        auto at = [&](unsigned position) { return item.caret_x(position); };
        if (focused) {
            unsigned first = session->begin();
            unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
            if (last > first) {
                if (session->composing) c.fill({at(first), editor.y + editor.h - d(2), at(last) - at(first), d(2)}, p.accent);
                else c.fill({at(first), editor.y, at(last) - at(first), editor.h}, p.selection);
            }
        }
        c.text(session->visible(), item.text_size, item.caret_x(0), editor.y, enabled ? p.text : p.muted);
        if (focused && c.window_focused && enabled) c.fill({at(session->visible_caret()), editor.y, d(1) + 1, editor.h}, p.accent);
    }
    bool selects_text() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.text_key_down(key); }
};
// Legacy toggle/navigation use the button presentation, but have no button activation behavior.
class Command : public ComponentType {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.button_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        int line = c.measure.line_height(p.text_size);
        int w = std::min(c.width, command_width(c)), h = c.appearance.button_height(line, c.scale);
        p.bounds = {0, 0, w, h};
        auto title = c.measure.measure(c.control.label, p.text_size);
        p.lines.push_back({{(w - static_cast<int>(title.width)) / 2, (h - line) / 2, static_cast<int>(std::ceil(title.width)), line}, c.control.label});
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement;
        auto p = c.appearance.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        bool focused = c.semantics.focused, enabled = c.semantics.enabled;
        int ring = focused ? d(3) : d(1);
        c.rounded(item.bounds, d(c.appearance.radius()), focused ? p.accent : enabled ? p.border : p.background);
        Rect inner{item.bounds.x + ring, item.bounds.y + ring, item.bounds.w - 2 * ring, item.bounds.h - 2 * ring};
        c.rounded(inner, std::max(0, d(c.appearance.radius()) - ring), enabled ? p.surface : p.background);
        paint_lines(c, item.lines, item.text_size, enabled ? p.text : p.muted);
    }
    int natural_width(const ComponentLayout& c) const override { return command_width(c); }
};
class Button final : public Command {
public:
    bool has_activate_operation() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.command_key_down(key); }
    void pointer_down(UiState& ui) const override { ui.command_pointer_down(); }
};
class Checkbox final : public Command {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.checkbox_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        Command::arrange(c);
        int extra = scaled(c.scale, 24);
        c.placement.bounds.w = std::min(c.width, c.placement.bounds.w + extra);
        for (auto& line : c.placement.lines) line.bounds.x += extra;
        auto semantics = *c.ui.describe(c.control.id);
        if (semantics.invalid) {
            auto& p = c.placement;
            p.error_lines = block(c.measure, semantics.error, p.text_size, c.width);
            for (auto& line : p.error_lines) line.bounds.y += p.bounds.h;
            p.bounds.h += height_of(p.error_lines);
        }
    }
    void paint(ComponentPaint& c) const override {
        Command::paint(c);
        paint_lines(c, c.placement.error_lines, c.placement.text_size, c.appearance.palette().error);
        auto p = c.appearance.palette();
        int size = scaled(c.scale, 14), x = c.placement.bounds.x + scaled(c.scale, 8);
        int y = c.placement.bounds.y + (c.placement.bounds.h - size) / 2;
        c.fill({x, y, size, size}, c.semantics.enabled ? p.border : p.muted);
        int inset = scaled(c.scale, 2);
        c.fill({x + inset, y + inset, size - inset * 2, size - inset * 2}, p.surface);
        if (c.semantics.checked.value_or(false))
            c.fill({x + 2 * inset, y + 2 * inset, size - 4 * inset, size - 4 * inset}, p.accent);
    }
    bool has_activate_operation() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override {
        return key.key == Key::space ? ui.command_key_down(key) : Result{};
    }
    void pointer_down(UiState& ui) const override { ui.command_pointer_down(); }
};
class RadioGroup final : public Command {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.radio_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), row = line + scaled(c.scale, 8);
        p.lines.push_back({{0, 0, c.width, line}, semantics.name});
        int y = line + scaled(c.scale, 4);
        for (const auto& option : semantics.options) {
            p.options.push_back({0, y, c.width, row});
            p.lines.push_back({{scaled(c.scale, 26), y + (row - line) / 2, std::max(0, c.width - scaled(c.scale, 26)), line}, option.label + (option.enabled ? u"" : u"\uff08\u505c\u7528\uff09")});
            y += row;
        }
        if (semantics.invalid) {
            p.error_lines = block(c.measure, semantics.error, p.text_size, c.width);
            for (auto& l : p.error_lines) l.bounds.y += y;
            y += height_of(p.error_lines);
        }
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto p = c.appearance.palette();
        const auto& item = c.placement;
        if (!item.lines.empty()) c.text(item.lines[0].text, item.text_size, item.lines[0].bounds.x, item.lines[0].bounds.y, p.text);
        if (c.semantics.focused) c.fill({item.bounds.x, item.bounds.y, scaled(c.scale, 2), item.bounds.h}, p.accent);
        for (std::size_t i = 0; i < item.options.size(); ++i) {
            const auto& r = item.options[i]; const auto& option = c.semantics.options[i];
            int size = scaled(c.scale, 14), inset = scaled(c.scale, 3);
            int x = r.x + scaled(c.scale, 4), y = r.y + (r.h - size) / 2;
            auto color = option.enabled && c.semantics.enabled ? p.border : p.muted;
            c.rounded({x, y, size, size}, size / 2, color);
            c.rounded({x + inset, y + inset, size - 2 * inset, size - 2 * inset}, size / 2, option.selected ? p.accent : p.surface);
            const auto& label = item.lines[i + 1];
            c.text(label.text, item.text_size, label.bounds.x, label.bounds.y, option.enabled && c.semantics.enabled ? p.text : p.muted);
            if (c.semantics.focused && option.selected) c.fill({r.x, r.y + r.h - scaled(c.scale, 2), r.w, scaled(c.scale, 2)}, p.accent);
        }
        paint_lines(c, item.error_lines, item.text_size, p.error);
    }
    int natural_width(const ComponentLayout&) const override { return -1; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.radio_key_down(key); }
};
class Dropdown final : public Command {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.dropdown_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), row = c.appearance.button_height(line, c.scale);
        p.lines.push_back({{0, 0, c.width, line}, s.name});
        p.lines.push_back({{scaled(c.scale, 8), line + (row - line) / 2, std::max(0, c.width - scaled(c.scale, 16)), line}, s.value.empty() ? u"\u672a\u9078\u53d6" : s.value});
        int y = line + row;
        if (s.invalid) {
            p.error_lines = block(c.measure, s.error, p.text_size, c.width);
            for (auto& l : p.error_lines) l.bounds.y += y;
            y += height_of(p.error_lines);
        }
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.appearance.palette();
        auto bounds = c.placement.bounds;
        if (c.placement.popup) {
            c.fill(bounds, palette.surface);
            for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
                const auto& o = c.semantics.options[i];
                const auto& r = c.placement.options[i];
                if (c.semantics.candidate && *c.semantics.candidate == o.value) c.fill(r, palette.selection);
                const auto& line = c.placement.lines[i];
                c.text(line.text, c.placement.text_size, line.bounds.x, line.bounds.y, o.enabled ? palette.text : palette.muted);
            }
            return;
        }
        c.rounded(bounds, scaled(c.scale, c.appearance.radius()), c.semantics.invalid ? palette.error : c.semantics.focused ? palette.accent : palette.border);
        int inset = scaled(c.scale, 2);
        c.rounded({bounds.x + inset, bounds.y + inset, std::max(0, bounds.w - 2 * inset), std::max(0, bounds.h - 2 * inset)}, scaled(c.scale, c.appearance.radius()), palette.surface);
        paint_lines(c, c.placement.lines, c.placement.text_size, c.semantics.enabled ? palette.text : palette.muted);
        paint_lines(c, c.placement.error_lines, c.placement.text_size, palette.error);
    }
    int natural_width(const ComponentLayout&) const override { return -1; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.dropdown_key_down(key); }
};
class Slider final : public ComponentType {
public:
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.slider_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), pad = scaled(c.scale, 10), track_height = scaled(c.scale, 18);
        auto title = semantics.name + u": " + semantics.value + (semantics.enabled ? u"" : u"\uff08\u505c\u7528\uff09");
        if (semantics.source_conflict) title += u"\uff08\u4f86\u6e90\u5df2\u66f4\u65b0\uff09";
        p.lines = block(c.measure, title, p.text_size, c.width);
        int y = height_of(p.lines) + scaled(c.scale, 4);
        p.slider_track = {pad, y, std::max(1, c.width - 2 * pad), track_height};
        y += track_height;
        if (semantics.invalid) {
            p.error_lines = block(c.measure, semantics.error, p.text_size, c.width);
            for (auto& l : p.error_lines) l.bounds.y += y;
            y += height_of(p.error_lines);
        }
        p.bounds = {0, 0, c.width, std::max(y, line)};
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement; auto palette = c.appearance.palette();
        paint_lines(c, item.lines, item.text_size, c.semantics.enabled ? palette.text : palette.muted);
        paint_lines(c, item.error_lines, item.text_size, palette.error);
        auto track = item.slider_track; int thickness = scaled(c.scale, 3);
        c.fill({track.x, track.y + track.h / 2 - thickness / 2, track.w, thickness}, c.semantics.enabled ? palette.border : palette.muted);
        if (c.semantics.slider_range && c.semantics.numeric_value) {
            const auto range = *c.semantics.slider_range;
            double ratio = (static_cast<double>(*c.semantics.numeric_value) - range.minimum) /
                           (static_cast<double>(range.maximum) - range.minimum);
            int x = track.x + static_cast<int>(std::lround(track.w * ratio));
            int size = scaled(c.scale, c.semantics.focused ? 16 : 12);
            c.rounded({x - size / 2, track.y + (track.h - size) / 2, size, size}, size / 2,
                      c.semantics.enabled ? palette.accent : palette.muted);
            if (c.semantics.focused) c.fill({track.x, track.y + track.h - scaled(c.scale, 2), track.w, scaled(c.scale, 2)}, palette.accent);
        }
    }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.slider_key_down(key); }
};
Slider slider;
Dropdown dropdown;
RadioGroup radio_group;
Checkbox checkbox;
Label label;
Text text;
Button button;
Command command;
const std::map<Kind, const ComponentType*>& types() {
    static const std::map<Kind, const ComponentType*> registry{
        {Kind::label, &label}, {Kind::text, &text}, {Kind::button, &button},
        {Kind::toggle, &command}, {Kind::navigation, &command}, {Kind::checkbox, &checkbox}, {Kind::radio_group, &radio_group}, {Kind::slider, &slider}, {Kind::dropdown, &dropdown}};
    return registry;
}
// Test registrations belong to the thread owning their UI. Built-in lookups remain immutable
// and can be used concurrently by independent UI environments.
std::map<Kind, const ComponentType*>& test_types() {
    thread_local std::map<Kind, const ComponentType*> registry;
    return registry;
}
}
const ComponentType& component_type(Kind kind) {
    auto test = test_types().find(kind);
    if (test != test_types().end()) return *test->second;
    auto it = types().find(kind);
    if (it == types().end()) throw std::invalid_argument("Unknown component type");
    return *it->second;
}
ScopedComponentType::ScopedComponentType(Kind kind, const ComponentType& type) : kind_(kind) {
    if (types().contains(kind) || !test_types().emplace(kind, &type).second)
        throw std::invalid_argument("Component type already registered");
}
ScopedComponentType::~ScopedComponentType() { test_types().erase(kind_); }
}
