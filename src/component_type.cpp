#include "component_type.hpp"
#include "core_internal.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lcars::detail {
namespace {
bool supports(ComponentState state, std::initializer_list<ComponentState> states) {
    return std::find(states.begin(), states.end(), state) != states.end();
}
using S = ComponentState;
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
std::u16string command_title(const ComponentLayout& c) {
    auto title = c.control.label;
    auto semantic = c.ui.describe(c.control.id);
    if (semantic && !semantic->enabled) title += c.appearance.component_presentation().disabled_suffix;
    if (semantic && semantic->executing) title += c.appearance.component_presentation().executing_suffix;
    return title;
}
int command_width(const ComponentLayout& c) {
    return static_cast<int>(std::ceil(c.measure.measure(command_title(c), c.placement.text_size).width)) +
           2 * c.appearance.button_padding(c.scale);
}
int scaled(float scale, int value) { return std::max(1, static_cast<int>(std::lround(value * scale))); }
Rect offset(Rect rect, Rect origin) { return {rect.x + origin.x, rect.y + origin.y, rect.w, rect.h}; }
int conflict_lines(ComponentLayout& c, int y, bool conflict) {
    if (!conflict) return y;
    auto& p = c.placement;
    p.description_lines = block(c.measure, c.appearance.component_presentation().conflict_suffix, p.text_size, c.width);
    for (auto& line : p.description_lines) line.bounds.y += y;
    return y + height_of(p.description_lines);
}
template<class F> void marked(ComponentPaint& c, PaintRole role, F&& paint) { c.role(role); paint(); c.role(PaintRole::content); }
void paint_errors(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    marked(c, PaintRole::error_text, [&] { for (const auto& line : lines) c.text(line.text, pixels, line.bounds.x, line.bounds.y, color); });
}
void paint_lines(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    for (const auto& line : lines) c.text(line.text, pixels, line.bounds.x, line.bounds.y, color);
}
class Label final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.label_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        p.lines = block(c.measure, c.ui.text(c.control.id), p.text_size, c.width);
        p.bounds = {0, 0, c.width, height_of(p.lines)};
    }
    void paint(ComponentPaint& c) const override {
        if (c.semantics.role == TextRole::error) paint_errors(c, c.placement.lines, c.placement.text_size, c.appearance.palette().text);
        else paint_lines(c, c.placement.lines, c.placement.text_size, c.appearance.palette().text);
    }
    bool can_focus() const override { return false; }
    bool can_target() const override { return true; }
    bool accepts_pointer() const override { return false; }
};
class Text final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::read_only, S::error, S::conflict}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.text_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        const auto& control = c.control;
        int line = c.measure.line_height(p.text_size);
        auto semantics = *c.ui.describe(control.id);
        auto name = semantics.name;
        if (control.read_only) name += c.appearance.component_presentation().read_only_suffix;
        if (!control.enabled) name += c.appearance.component_presentation().disabled_suffix;
        int caption = p.caption_size = std::max(1, p.text_size * 7 / 8);
        auto probe = c.appearance.arrange_field(c.width, {line, line, line, line}, c.scale);
        auto label = block(c.measure, name, caption, probe.label.w);
        auto description_text = semantics.description;
        if (semantics.source_conflict) {
            if (!description_text.empty()) description_text += u"\n";
            description_text += c.appearance.component_presentation().conflict_suffix;
        }
        auto description = block(c.measure, description_text, caption, probe.description.w);
        auto error = block(c.measure, semantics.error, caption, probe.error.w);
        p.field = c.appearance.arrange_field(c.width, {height_of(label), height_of(description), height_of(error), line}, c.scale);
        auto anchor = [](std::vector<Line>& lines, Rect at) { for (auto& l : lines) { l.bounds.x += at.x; l.bounds.y += at.y; } };
        anchor(label, p.field.label); anchor(description, p.field.description); anchor(error, p.field.error);
        p.label_lines = std::move(label); p.description_lines = std::move(description); p.error_lines = std::move(error);
        p.bounds = {0, 0, c.width, p.field.height};
        const auto* session = c.ui.edit(control.id);
        p.run = c.measure.measure(session->visible(), p.text_size);
        float offset = c.ui.text_offset(control.id) * c.scale;
        float room = static_cast<float>(p.field.editor.w - static_cast<int>(std::lround(c.appearance.component_presentation().inset * c.scale)));
        if (c.ui.focused() == control.id) {
            float caret = p.run.x(session->visible_caret());
            if (caret - offset > room) offset = caret - room;
            if (caret < offset) offset = caret;
        }
        offset = std::clamp(offset, 0.f, std::max(0.f, p.run.width - room));
        if (c.settle) c.ui.set_text_offset(control.id, offset / c.scale);
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
        int ring = focused ? d(c.appearance.component_presentation().inset) : d(c.appearance.component_presentation().thin);
        Color edge = semantics.invalid ? p.error : focused ? p.accent : p.border;
        marked(c, (focused ? PaintRole::focus_indicator : PaintRole::content) | (semantics.invalid ? PaintRole::error_indicator : PaintRole::content), [&] { c.rounded(surface, d(c.appearance.radius()), f.underline ? (enabled ? p.surface : p.background) : edge); });
        if (!f.underline) {
            Rect inner{surface.x + ring, surface.y + ring, surface.w - 2 * ring, surface.h - 2 * ring};
            c.rounded(inner, std::max(0, d(c.appearance.radius()) - ring), enabled ? p.surface : p.background);
        } else marked(c, (focused ? PaintRole::focus_indicator : PaintRole::content) | (semantics.invalid ? PaintRole::error_indicator : PaintRole::content), [&] { c.fill({surface.x, surface.y + surface.h - 2 * ring, surface.w, 2 * ring}, edge); });
        paint_lines(c, item.label_lines, item.caption_size, semantics.invalid ? p.error : p.muted);
        paint_lines(c, item.description_lines, item.caption_size, p.muted);
        paint_errors(c, item.error_lines, item.caption_size, p.error);
        const auto* session = c.ui.edit(item.id);
        c.clip(editor);
        auto at = [&](unsigned position) { return item.caret_x(position); };
        if (focused) {
            unsigned first = session->begin();
            unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
            if (last > first) {
                if (session->composing) marked(c, PaintRole::composition, [&] { c.fill({at(first), editor.y + editor.h - d(c.appearance.component_presentation().inset), at(last) - at(first), d(c.appearance.component_presentation().inset)}, p.accent); });
                else marked(c, PaintRole::selection, [&] { c.fill({at(first), editor.y, at(last) - at(first), editor.h}, p.selection); });
            }
        }
        c.text(session->visible(), item.text_size, item.caret_x(0), editor.y, enabled ? p.text : p.muted);
        if (focused && c.window_focused && enabled) marked(c, PaintRole::caret, [&] { c.fill({at(session->visible_caret()), editor.y, d(c.appearance.component_presentation().thin) + 1, editor.h}, p.accent); });
    }
    bool selects_text() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.text_key_down(key); }
};
// Legacy toggle/navigation use the button presentation, but have no button activation behavior.
class Command : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.button_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        int line = c.measure.line_height(p.text_size);
        int w = std::min(c.width, command_width(c));
        int room = std::max(1, w - 2 * c.appearance.button_padding(c.scale));
        auto lines = wrap(c.measure, command_title(c), p.text_size, room);
        int text_height = static_cast<int>(lines.size()) * line;
        int h = c.appearance.button_height(text_height, c.scale);
        p.bounds = {0, 0, w, h};
        int y = (h - text_height) / 2;
        for (auto& text : lines) {
            int width = static_cast<int>(std::ceil(c.measure.measure(text, p.text_size).width));
            p.lines.push_back({{(w - width) / 2, y, width, line}, std::move(text)});
            y += line;
        }
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement;
        auto p = c.appearance.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        bool focused = c.semantics.focused, enabled = c.semantics.enabled;
        int ring = focused ? d(c.appearance.component_presentation().focus) : d(c.appearance.component_presentation().thin);
        marked(c, focused ? PaintRole::focus_indicator : PaintRole::content, [&] { c.rounded(item.bounds, d(c.appearance.radius()), focused ? p.accent : enabled ? p.border : p.background); });
        Rect inner{item.bounds.x + ring, item.bounds.y + ring, item.bounds.w - 2 * ring, item.bounds.h - 2 * ring};
        c.rounded(inner, std::max(0, d(c.appearance.radius()) - ring), enabled ? p.surface : p.background);
        paint_lines(c, item.lines, item.text_size, enabled ? p.text : p.muted);
    }
    int natural_width(const ComponentLayout& c) const override { return command_width(c); }
};
class Button final : public Command {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::executing}); }
    bool has_activate_operation() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.command_key_down(key); }
    void pointer_down(UiState& ui) const override { ui.command_pointer_down(); }
};
class Checkbox final : public Command {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::error, S::conflict}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.checkbox_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        Command::arrange(c);
        int extra = scaled(c.scale, c.appearance.component_presentation().checkbox_extra);
        c.placement.bounds.w = std::min(c.width, c.placement.bounds.w + extra);
        for (auto& line : c.placement.lines) line.bounds.x += extra;
        auto semantics = *c.ui.describe(c.control.id);
        c.placement.bounds.h = conflict_lines(c, c.placement.bounds.h, semantics.source_conflict);
        if (semantics.invalid) {
            auto& p = c.placement;
            p.error_lines = block(c.measure, semantics.error, p.text_size, c.width);
            for (auto& line : p.error_lines) line.bounds.y += p.bounds.h;
            p.bounds.h += height_of(p.error_lines);
        }
    }
    void paint(ComponentPaint& c) const override {
        Command::paint(c);
        paint_lines(c, c.placement.description_lines, c.placement.text_size, c.appearance.palette().muted);
        paint_errors(c, c.placement.error_lines, c.placement.text_size, c.appearance.palette().error);
        auto p = c.appearance.palette();
        int size = scaled(c.scale, c.appearance.component_presentation().indicator), x = c.placement.bounds.x + scaled(c.scale, c.appearance.component_presentation().gap);
        int y = c.placement.bounds.y + (c.appearance.button_height(c.placement.lines.front().bounds.h, c.scale) - size) / 2;
        c.fill({x, y, size, size}, c.semantics.enabled ? p.border : p.muted);
        int inset = scaled(c.scale, c.appearance.component_presentation().inset);
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
class RadioGroup final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::error, S::conflict}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.radio_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), row = line + scaled(c.scale, c.appearance.component_presentation().gap);
        p.lines.push_back({{0, 0, c.width, line}, semantics.name + (semantics.enabled ? u"" : c.appearance.component_presentation().disabled_suffix)});
        int y = line + scaled(c.scale, c.appearance.component_presentation().gap_small);
        for (const auto& option : semantics.options) {
            p.options.push_back({0, y, c.width, row});
            p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation().radio_label_offset), y + (row - line) / 2, std::max(0, c.width - scaled(c.scale, c.appearance.component_presentation().radio_label_offset)), line}, option.label + (option.enabled ? u"" : c.appearance.component_presentation().disabled_suffix)});
            y += row;
        }
        y = conflict_lines(c, y, semantics.source_conflict);
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
        if (!item.lines.empty()) c.text(item.lines[0].text, item.text_size, item.lines[0].bounds.x, item.lines[0].bounds.y, c.semantics.enabled ? p.text : p.muted);
        if (c.semantics.focused) marked(c, PaintRole::focus_indicator, [&] { c.fill({item.bounds.x, item.bounds.y, scaled(c.scale, c.appearance.component_presentation().inset), item.bounds.h}, p.accent); });
        for (std::size_t i = 0; i < item.options.size(); ++i) {
            const auto& r = item.options[i]; const auto& option = c.semantics.options[i];
            int size = scaled(c.scale, c.appearance.component_presentation().indicator), inset = scaled(c.scale, c.appearance.component_presentation().focus);
            int x = r.x + scaled(c.scale, c.appearance.component_presentation().gap_small), y = r.y + (r.h - size) / 2;
            auto color = option.enabled && c.semantics.enabled ? p.border : p.muted;
            c.rounded({x, y, size, size}, size / 2, color);
            c.rounded({x + inset, y + inset, size - 2 * inset, size - 2 * inset}, size / 2, option.selected ? p.accent : p.surface);
            const auto& label = item.lines[i + 1];
            c.text(label.text, item.text_size, label.bounds.x, label.bounds.y, option.enabled && c.semantics.enabled ? p.text : p.muted);
            if (c.semantics.focused && option.selected) marked(c, PaintRole::focus_indicator, [&] { c.fill({r.x, r.y + r.h - scaled(c.scale, c.appearance.component_presentation().inset), r.w, scaled(c.scale, c.appearance.component_presentation().inset)}, p.accent); });
        }
        paint_lines(c, item.description_lines, item.text_size, p.muted);
        paint_errors(c, item.error_lines, item.text_size, p.error);
    }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.radio_key_down(key); }
};
class List final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::candidate}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.list_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), row = line + scaled(c.scale, c.appearance.component_presentation().row_padding);
        p.label_lines = block(c.measure, s.name + (s.enabled ? u"" : c.appearance.component_presentation().disabled_suffix), p.text_size, c.width);
        int y = height_of(p.label_lines);
        if (y) y += scaled(c.scale, c.appearance.component_presentation().gap_small);
        if (s.options.empty()) {
            p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation().gap), y, c.width, line}, c.appearance.component_presentation().no_items});
            y += row;
        }
        for (const auto& item : s.options) {
            p.options.push_back({0, y, c.width, row});
            auto text = (item.selected ? c.appearance.component_presentation().selected_marker : c.appearance.component_presentation().unselected_marker) + item.label;
            if (!item.enabled) text += c.appearance.component_presentation().disabled_suffix;
            p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation().gap), y + (row - line) / 2, std::max(0, c.width - scaled(c.scale, c.appearance.component_presentation().body_spacing)), line}, std::move(text)});
            y += row;
        }
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.appearance.palette(); const auto& p = c.placement;
        paint_lines(c, p.label_lines, p.text_size, c.semantics.enabled ? palette.text : palette.muted);
        if (c.semantics.options.empty()) { paint_lines(c, p.lines, p.text_size, palette.muted); return; }
        for (std::size_t i = 0; i < p.options.size(); ++i) {
            const auto& item = c.semantics.options[i]; const auto& row = p.options[i];
            if (item.selected) marked(c, PaintRole::selection, [&] { c.fill(row, palette.selection); });
            if (c.semantics.focused && c.semantics.candidate == item.value)
                marked(c, PaintRole::focus_indicator, [&] { c.fill({row.x, row.y, scaled(c.scale, c.appearance.component_presentation().focus), row.h}, palette.accent); });
            const auto& line = p.lines[i];
            c.text(line.text, p.text_size, line.bounds.x, line.bounds.y, item.enabled && c.semantics.enabled ? palette.text : palette.muted);
        }
    }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.list_key_down(key); }
};
class Dropdown final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::candidate, S::error, S::conflict}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.dropdown_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), row = c.appearance.button_height(line, c.scale);
        p.lines.push_back({{0, 0, c.width, line}, s.name + (s.enabled ? u"" : c.appearance.component_presentation().disabled_suffix)});
        p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation().gap), line + (row - line) / 2, std::max(0, c.width - scaled(c.scale, c.appearance.component_presentation().body_spacing)), line}, s.value.empty() ? c.appearance.component_presentation().no_selection : s.value});
        int y = line + row;
        y = conflict_lines(c, y, s.source_conflict);
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
            c.clip(bounds);
            c.fill(bounds, palette.surface);
            if (c.semantics.options.empty()) {
                paint_lines(c, c.placement.lines, c.placement.text_size, palette.muted);
                return;
            }
            for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
                const auto& o = c.semantics.options[i];
                const auto& r = c.placement.options[i];
                if (c.semantics.candidate && *c.semantics.candidate == o.value) marked(c, PaintRole::selection, [&] { c.fill(r, palette.selection); });
                const auto& line = c.placement.lines[i];
                c.text(o.enabled ? line.text : line.text + c.appearance.component_presentation().disabled_suffix, c.placement.text_size, line.bounds.x, line.bounds.y, o.enabled ? palette.text : palette.muted);
            }
            return;
        }
        marked(c, (c.semantics.focused ? PaintRole::focus_indicator : PaintRole::content) | (c.semantics.invalid ? PaintRole::error_indicator : PaintRole::content), [&] { c.rounded(bounds, scaled(c.scale, c.appearance.radius()), c.semantics.invalid ? palette.error : c.semantics.focused ? palette.accent : palette.border); });
        int inset = scaled(c.scale, c.appearance.component_presentation().inset);
        c.rounded({bounds.x + inset, bounds.y + inset, std::max(0, bounds.w - 2 * inset), std::max(0, bounds.h - 2 * inset)}, scaled(c.scale, c.appearance.radius()), palette.surface);
        paint_lines(c, c.placement.lines, c.placement.text_size, c.semantics.enabled ? palette.text : palette.muted);
        if (c.placement.lines.size() > 1) {
            const auto& line = c.placement.lines[1].bounds;
            c.text(c.appearance.component_presentation().dropdown_marker, c.placement.text_size, bounds.x + std::max(0, bounds.w - scaled(c.scale, c.appearance.component_presentation().dropdown_marker_space)), line.y,
                   c.semantics.enabled ? palette.text : palette.muted);
        }
        paint_lines(c, c.placement.description_lines, c.placement.text_size, palette.muted);
        paint_errors(c, c.placement.error_lines, c.placement.text_size, palette.error);
    }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.dropdown_key_down(key); }
};
class Slider final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::error, S::conflict}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.slider_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(p.text_size), pad = scaled(c.scale, c.appearance.component_presentation().slider_pad), track_height = scaled(c.scale, c.appearance.component_presentation().slider_height);
        auto title = semantics.name + u": " + semantics.value + (semantics.enabled ? u"" : c.appearance.component_presentation().disabled_suffix);
        if (semantics.source_conflict) title += c.appearance.component_presentation().conflict_suffix;
        p.lines = block(c.measure, title, p.text_size, c.width);
        int y = height_of(p.lines) + scaled(c.scale, c.appearance.component_presentation().gap_small);
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
        paint_errors(c, item.error_lines, item.text_size, palette.error);
        auto track = item.slider_track; int thickness = scaled(c.scale, c.appearance.component_presentation().focus);
        c.fill({track.x, track.y + track.h / 2 - thickness / 2, track.w, thickness}, c.semantics.enabled ? palette.border : palette.muted);
        if (c.semantics.slider_range && c.semantics.numeric_value) {
            const auto range = *c.semantics.slider_range;
            double ratio = (static_cast<double>(*c.semantics.numeric_value) - range.minimum) /
                           (static_cast<double>(range.maximum) - range.minimum);
            int x = track.x + static_cast<int>(std::lround(track.w * ratio));
            int size = scaled(c.scale, c.semantics.focused ? c.appearance.component_presentation().body_spacing : c.appearance.component_presentation().row_padding);
            c.rounded({x - size / 2, track.y + (track.h - size) / 2, size, size}, size / 2,
                      c.semantics.enabled ? palette.accent : palette.muted);
            if (c.semantics.focused) marked(c, PaintRole::focus_indicator, [&] { c.fill({track.x, track.y + track.h - scaled(c.scale, c.appearance.component_presentation().inset), track.w, scaled(c.scale, c.appearance.component_presentation().inset)}, palette.accent); });
        }
    }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.slider_key_down(key); }
};
class Progress final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.progress_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantic = *c.ui.describe(c.control.id);
        auto title = semantic.name + (semantic.value.empty() ? u"" : u": " + semantic.value);
        p.lines = block(c.measure, title, p.text_size, c.width);
        int y = height_of(p.lines) + scaled(c.scale, c.appearance.component_presentation().gap_small);
        p.progress_track = {0, y, std::max(0, c.width), scaled(c.scale, c.appearance.component_presentation().row_padding)};
        p.progress_fill = p.progress_track;
        p.progress_fill.w = static_cast<int>(static_cast<long long>(p.progress_track.w) * semantic.numeric_value.value_or(0) / 100);
        p.bounds = {0, 0, c.width, y + p.progress_track.h};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.appearance.palette();
        paint_lines(c, c.placement.lines, c.placement.text_size, palette.text);
        c.fill(c.placement.progress_track, palette.border);
        if (c.placement.progress_fill.w > 0) c.fill(c.placement.progress_fill, palette.accent);
    }
    bool can_focus() const override { return false; }
    bool accepts_pointer() const override { return false; }
};
class Tabs final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::candidate, S::error}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.tabs_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto semantics = *c.ui.describe(c.control.id);
        int height = c.appearance.button_height(c.measure.line_height(c.placement.text_size), c.scale);
        c.placement.label_lines = block(c.measure, semantics.name, c.placement.text_size, c.width);
        int top = height_of(c.placement.label_lines);
        if (top) top += scaled(c.scale, c.appearance.component_presentation().gap_small);
        int count = static_cast<int>(semantics.options.size());
        for (int i = 0; i < count; ++i) {
            int left = c.width * i / count, right = c.width * (i + 1) / count;
            c.placement.options.push_back({left, top, right - left, height});
            const auto& option = semantics.options[static_cast<std::size_t>(i)];
            std::u16string prefix = option.invalid ? c.appearance.component_presentation().invalid_tab_marker : u"";
            if (!option.enabled) prefix += c.appearance.component_presentation().disabled_tab_marker;
            auto label = option.label;
            int room = std::max(0, right - left - scaled(c.scale, c.appearance.component_presentation().gap));
            while (!label.empty() && c.measure.measure(prefix + label, c.placement.text_size).width > room) {
                if (label.back() >= 0xdc00 && label.back() <= 0xdfff && label.size() > 1) label.pop_back();
                label.pop_back();
            }
            label = prefix + label;
            c.placement.lines.push_back({{left + scaled(c.scale, c.appearance.component_presentation().gap_small), top + (height - c.measure.line_height(c.placement.text_size)) / 2,
                                         std::max(0, right - left - scaled(c.scale, c.appearance.component_presentation().gap)), height}, std::move(label)});
        }
        c.placement.bounds = {0, 0, c.width, top + height};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.appearance.palette();
        paint_lines(c, c.placement.label_lines, c.placement.text_size, palette.text);
        for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
            const auto& option = c.semantics.options[i]; auto rect = c.placement.options[i];
            marked(c, option.selected ? PaintRole::selection : PaintRole::content, [&] { c.fill(rect, option.selected ? palette.selection : palette.surface); });
            if (option.selected) marked(c, PaintRole::selection, [&] { c.fill({rect.x, rect.y + rect.h - scaled(c.scale, c.appearance.component_presentation().focus), rect.w, scaled(c.scale, c.appearance.component_presentation().focus)}, palette.accent); });
            if (c.semantics.focused && c.semantics.candidate && *c.semantics.candidate == option.value)
                marked(c, PaintRole::focus_indicator, [&] { c.fill({rect.x, rect.y, rect.w, scaled(c.scale, c.appearance.component_presentation().inset)}, palette.accent); });
            const auto& line = c.placement.lines[i];
            marked(c, option.invalid ? PaintRole::error_text : PaintRole::content, [&] { c.text(line.text, c.placement.text_size, line.bounds.x, line.bounds.y, option.enabled ? palette.text : palette.muted); });
        }
    }
    bool has_activate_operation() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.tabs_key_down(key); }
};
Tabs tabs;
Progress progress;
Slider slider;
List list;
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
        {Kind::toggle, &command}, {Kind::navigation, &command}, {Kind::checkbox, &checkbox}, {Kind::radio_group, &radio_group}, {Kind::slider, &slider}, {Kind::dropdown, &dropdown}, {Kind::list, &list}, {Kind::tabs, &tabs}, {Kind::progress, &progress}};
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
