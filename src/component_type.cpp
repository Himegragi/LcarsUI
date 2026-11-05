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
Label label;
Text text;
Button button;
Command command;
const std::map<Kind, const ComponentType*>& types() {
    static const std::map<Kind, const ComponentType*> registry{
        {Kind::label, &label}, {Kind::text, &text}, {Kind::button, &button},
        {Kind::toggle, &command}, {Kind::navigation, &command}};
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
