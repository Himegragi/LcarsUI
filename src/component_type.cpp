#include "component_type.hpp"
#include "core_internal.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lcars::detail {
Palette ComponentPaint::palette() const { return ui.effective_palette(appearance); }
int text_row(int line_height, double line_spacing) {
    return std::max(line_height, static_cast<int>(std::lround(line_height * line_spacing)));
}
int paragraph_gap(const Control& control, float scale) {
    return control.paragraph_spacing > 0 ? static_cast<int>(std::lround(control.paragraph_spacing * scale)) : 0;
}
ComponentPaint::ComponentPaint(UiState& state, const Placement& item, const Semantics& semantic,
                               const Appearance& look, float render_scale, bool active_window)
    : ui(state), placement(item), semantics(semantic), appearance(look), scale(render_scale), window_focused(active_window),
      focus_visible(state.draws_focus_indicator(semantic)) {}
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
// A label's text laid out with its alignment, line spacing and paragraph spacing. Lines carry the box
// the glyphs occupy: x is where the (aligned) text starts, y its top inside the row, h the font's line
// height. A left-aligned line keeps the full width as before; the others are as wide as their text.
struct Flowed { std::vector<Line> lines; int height = 0; };
Flowed flowed_block(TextMeasure& measure, std::u16string_view text, int size, int width, const Control& control, float scale) {
    Flowed out;
    if (text.empty()) return out;
    const int line = measure.line_height(size), row = text_row(line, control.line_spacing), gap = paragraph_gap(control, scale);
    int y = 0;
    const auto pieces = wrap_marked(measure, text, size, width);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        Line placed{{0, y + (row - line) / 2, width, line}, pieces[i].text};
        if (control.text_align != TextAlign::left) {
            const int w = static_cast<int>(std::ceil(measure.measure(pieces[i].text, size).width));
            const int room = std::max(0, width - w);
            placed.bounds.x = control.text_align == TextAlign::center ? room / 2 : room;
            placed.bounds.w = w;
        }
        out.lines.push_back(std::move(placed));
        y += row;
        if (i + 1 < pieces.size() && !pieces[i].soft) y += gap;
    }
    out.height = y;
    return out;
}
std::u16string command_title(const ComponentLayout& c) {
    auto title = c.control.label;
    auto semantic = c.ui.describe(c.control.id);
    if (semantic && !semantic->enabled) title += c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix;
    if (semantic && semantic->executing) title += c.appearance.component_presentation(c.ui.presentation().density).executing_suffix;
    return title;
}
int command_width(const ComponentLayout& c) {
    return static_cast<int>(std::ceil(c.measure.measure(command_title(c), c.placement.text_size).width)) +
           2 * c.appearance.button_padding(c.scale, c.ui.presentation().density);
}
int scaled(float scale, int value) { return std::max(1, static_cast<int>(std::lround(value * scale))); }
Rect offset(Rect rect, Rect origin) { return {rect.x + origin.x, rect.y + origin.y, rect.w, rect.h}; }
int conflict_lines(ComponentLayout& c, int y, bool conflict) {
    if (!conflict) return y;
    auto& p = c.placement;
    p.description_lines = block(c.measure, c.appearance.component_presentation(c.ui.presentation().density).conflict_suffix, p.text_size, c.width);
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
// The slider's focus dot sits left of the title. Its room is always reserved, so the title never moves
// when the indicator appears. Diameter is ~40% of the text size.
struct SliderDot { int diameter, gap; int room() const { return diameter + gap; } };
SliderDot slider_dot(const Appearance& appearance, float scale, int text_size, Density density) {
    return {std::max(2, static_cast<int>(std::lround(text_size * 0.4))), scaled(scale, appearance.component_presentation(density).gap_small)};
}
class Label final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.label_layout_handle(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto flowed = flowed_block(c.measure, c.ui.text(c.control.id), p.text_size, c.width, c.control, c.scale);
        p.lines = std::move(flowed.lines);
        p.bounds = {0, 0, c.width, flowed.height};
    }
    void paint(ComponentPaint& c) const override {
        if (c.focus_visible) {
            auto r = c.placement.clip;
            auto color = c.palette().accent;
            int edge = std::max(1, scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).focus));
            marked(c, PaintRole::focus_indicator, [&] {
                c.fill({r.x, r.y, r.w, edge}, color);
                c.fill({r.x, r.y + r.h - edge, r.w, edge}, color);
                c.fill({r.x, r.y, edge, r.h}, color);
                c.fill({r.x + r.w - edge, r.y, edge, r.h}, color);
            });
        }
        if (c.semantics.role == TextRole::error) paint_errors(c, c.placement.lines, c.placement.text_size, c.palette().text);
        else paint_lines(c, c.placement.lines, c.placement.text_size, c.palette().text);
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
        if (control.read_only) name += c.appearance.component_presentation(c.ui.presentation().density).read_only_suffix;
        if (!control.enabled) name += c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix;
        int caption = p.caption_size = std::max(1, p.text_size * 7 / 8);
        auto probe = c.appearance.arrange_field(c.width, {line, line, line, line}, c.scale, c.ui.presentation().density);
        auto label = block(c.measure, name, caption, probe.label.w);
        auto description_text = semantics.description;
        if (semantics.source_conflict) {
            if (!description_text.empty()) description_text += u"\n";
            description_text += c.appearance.component_presentation(c.ui.presentation().density).conflict_suffix;
        }
        auto description = block(c.measure, description_text, caption, probe.description.w);
        auto error = block(c.measure, semantics.error, caption, probe.error.w);
        const int row = text_row(line, control.line_spacing);
        // A fixed number of lines: every row, plus room for a paragraph gap between each pair of them.
        const int editor_height = control.multiline ? row * static_cast<int>(control.visible_lines) + paragraph_gap(control, c.scale) * static_cast<int>(control.visible_lines - 1) : row;
        p.field = c.appearance.arrange_field(c.width, {height_of(label), height_of(description), height_of(error), editor_height}, c.scale, c.ui.presentation().density);
        auto anchor = [](std::vector<Line>& lines, Rect at) { for (auto& l : lines) { l.bounds.x += at.x; l.bounds.y += at.y; } };
        anchor(label, p.field.label); anchor(description, p.field.description); anchor(error, p.field.error);
        p.label_lines = std::move(label); p.description_lines = std::move(description); p.error_lines = std::move(error);
        p.bounds = {0, 0, c.width, p.field.height};
        const auto* session = c.ui.edit(control.id);
        if (control.multiline) { arrange_lines(c, line, row); return; }
        p.run = c.measure.measure(session->visible(), p.text_size);
        p.text_top = (row - line) / 2;
        float offset = c.ui.text_offset(control.id) * c.scale;
        if (c.ui.focused() != control.id) {
            auto anchor = c.ui.text_anchor(control.id, p.text_size);
            if (anchor) offset = p.run.x(anchor);
        }
        float room = static_cast<float>(p.field.editor.w - static_cast<int>(std::lround(c.appearance.component_presentation(c.ui.presentation().density).inset * c.scale)));
        if (c.ui.focused() == control.id) {
            float caret = p.run.x(session->visible_caret());
            if (caret - offset > room) offset = caret - room;
            if (caret < offset) offset = caret;
        }
        offset = std::clamp(offset, 0.f, std::max(0.f, p.run.width - room));
        if (c.settle) {
            c.ui.set_text_offset(control.id, offset / c.scale);
            auto stop = std::find_if(p.run.stops.begin(), p.run.stops.end(), [offset](const auto& item) { return item.second >= offset - 0.01f; });
            c.ui.set_text_anchor(control.id, p.text_size, stop == p.run.stops.end() ? 0 : stop->first);
        }
        p.offset = offset;
        // Text that fits is placed by its alignment; text that does not fit scrolls from its start as before.
        if (p.run.width <= room && control.text_align != TextAlign::left)
            p.shift = control.text_align == TextAlign::center ? std::max(0.f, (static_cast<float>(p.field.editor.w) - p.run.width) / 2) : room - p.run.width;
    }
    // Multi-line: wrap at the edge, reserve the scrollbar's width whether or not it shows, and let
    // core decide the first visible line (scroll position, caret reveal, font-size anchor).
    static void arrange_lines(ComponentLayout& c, int line, int row) {
        auto& p = c.placement;
        auto& view = p.view;
        p.multiline = true;
        view.visible = c.control.visible_lines; view.line_height = line; view.row_height = row;
        const int bar = scaled(c.scale, 10), gap = scaled(c.scale, 4);
        const Rect editor = p.field.editor;
        view.text = {editor.x, editor.y, std::max(0, editor.w - bar - gap), editor.h};
        view.track = {editor.x + editor.w - bar, editor.y, bar, editor.h};
        if (!c.settle) return;  // a measurement probe only needs the fixed height
        const int slack = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset);  // room for a caret at a line end
        const int wrap_width = std::max(1, view.text.w - slack);
        view.lines = wrap_lines(c.measure, c.ui.edit(c.control.id)->visible(), p.text_size, wrap_width);
        // Rows: one per line, a paragraph gap after each hard line. Alignment: centered in the text area,
        // right-aligned to the wrap edge (which keeps the caret's slack free at the right).
        const int paragraph = paragraph_gap(c.control, c.scale);
        int top = 0;
        for (std::size_t i = 0; i < view.lines.size(); ++i) {
            auto& item = view.lines[i];
            item.top = top;
            top += row + (item.soft ? 0 : paragraph);
            if (c.control.text_align == TextAlign::center) item.shift = std::max(0.f, (static_cast<float>(view.text.w) - item.run.width) / 2);
            else if (c.control.text_align == TextAlign::right) item.shift = std::max(0.f, static_cast<float>(wrap_width) - item.run.width);
        }
        c.ui.settle_lines(c.control.id, view, p.text_size, wrap_width);
        const int count = static_cast<int>(view.lines.size()), visible = static_cast<int>(view.visible);
        if (count > visible) {
            const int extent = count * row, maximum = (count - visible) * row;
            const int thumb = std::min(view.track.h, std::max(scaled(c.scale, 24), view.track.h * view.track.h / std::max(1, extent)));
            view.thumb = {view.track.x, view.track.y + (view.track.h - thumb) * static_cast<int>(view.first) * row / maximum, view.track.w, thumb};
        }
    }
    static void paint_lines_of(ComponentPaint& c) {
        const auto& item = c.placement;
        const auto& view = item.view;
        const auto p = c.palette();
        const auto* session = c.ui.edit(item.id);
        const bool focused = c.semantics.focused, enabled = c.semantics.enabled;
        auto d = [&](int value) { return scaled(c.scale, value); };
        const int inset = d(c.appearance.component_presentation(c.ui.presentation().density).inset);
        if (view.thumb.h) {
            c.rounded(view.track, view.track.w / 2, p.panel);
            c.rounded(view.thumb, view.thumb.w / 2, enabled ? p.outline : p.border);  // a control edge: outline when usable
        }
        c.clip(view.text);
        const auto text = session->visible();
        const unsigned first = session->begin();
        const unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
        const unsigned end_line = static_cast<unsigned>(std::min<std::size_t>(view.first + view.visible, view.lines.size()));
        for (unsigned i = view.first; i < end_line; ++i) {
            const auto& line = view.lines[i];
            const int y = view.row_y(i), row = view.row_height, dy = (row - view.line_height) / 2;  // the row; the glyphs sit in its middle
            auto x_at = [&](unsigned offset) { return view.text.x + static_cast<int>(std::lround(view.x_in_line(i, offset))); };
            if (focused && last > first) {
                const unsigned a = std::max(first, line.begin), b = std::min(last, line.end);
                const bool covers_break = !session->composing && !line.soft && line.end < text.size() && first <= line.end && last > line.end;
                const int extra = covers_break ? d(6) : 0;  // a selected line break shows as a short block
                if (a < b || covers_break) {
                    const int from = x_at(std::min(a, line.end)), to = x_at(std::max(a, b)) + extra;
                    if (session->composing) marked(c, PaintRole::composition, [&] { c.fill({from, y + row - dy - inset, to - from, inset}, p.accent); });
                    else marked(c, PaintRole::selection, [&] { c.fill({from, y, to - from, row}, p.selection); });
                }
            }
            if (line.end > line.begin) c.text(std::u16string_view(text).substr(line.begin, line.end - line.begin), item.text_size, x_at(line.begin), y + dy, enabled ? p.text : p.muted);
        }
        if (focused && c.window_focused && enabled) {
            const unsigned caret_line = view.line_of(session->visible_caret());
            if (caret_line >= view.first && caret_line < end_line)
                marked(c, PaintRole::caret, [&] { c.fill({item.caret_x(session->visible_caret()), item.caret_y(session->visible_caret()), d(c.appearance.component_presentation(c.ui.presentation().density).thin) + 1, view.row_height}, p.accent); });
        }
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement;
        const auto& semantics = c.semantics;
        const auto& f = item.field;
        auto p = c.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        auto surface = offset(f.surface, item.bounds), editor = offset(f.editor, item.bounds);
        bool focused = semantics.focused, enabled = semantics.enabled, cue = c.focus_visible;
        int ring = cue ? d(c.appearance.component_presentation(c.ui.presentation().density).inset) : d(c.appearance.component_presentation(c.ui.presentation().density).thin);
        // The edge identifies the field: outline when usable, the light divider color when disabled.
        Color edge = semantics.invalid ? p.error : cue ? p.accent : enabled ? p.outline : p.border;
        marked(c, (cue ? PaintRole::focus_indicator : PaintRole::content) | (semantics.invalid ? PaintRole::error_indicator : PaintRole::content), [&] { c.rounded(surface, d(c.appearance.radius()), f.underline ? (enabled ? p.surface : p.background) : edge); });
        if (!f.underline) {
            Rect inner{surface.x + ring, surface.y + ring, surface.w - 2 * ring, surface.h - 2 * ring};
            c.rounded(inner, std::max(0, d(c.appearance.radius()) - ring), enabled ? p.surface : p.background);
        } else marked(c, (cue ? PaintRole::focus_indicator : PaintRole::content) | (semantics.invalid ? PaintRole::error_indicator : PaintRole::content), [&] { c.fill({surface.x, surface.y + surface.h - 2 * ring, surface.w, 2 * ring}, edge); });
        paint_lines(c, item.label_lines, item.caption_size, semantics.invalid ? p.error : p.muted);
        paint_lines(c, item.description_lines, item.caption_size, p.muted);
        paint_errors(c, item.error_lines, item.caption_size, p.error);
        if (item.multiline) { paint_lines_of(c); return; }
        const auto* session = c.ui.edit(item.id);
        c.clip(editor);
        auto at = [&](unsigned position) { return item.caret_x(position); };
        if (focused) {
            unsigned first = session->begin();
            unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
            if (last > first) {
                if (session->composing) marked(c, PaintRole::composition, [&] { c.fill({at(first), editor.y + editor.h - item.text_top - d(c.appearance.component_presentation(c.ui.presentation().density).inset), at(last) - at(first), d(c.appearance.component_presentation(c.ui.presentation().density).inset)}, p.accent); });
                else marked(c, PaintRole::selection, [&] { c.fill({at(first), editor.y, at(last) - at(first), editor.h}, p.selection); });
            }
        }
        c.text(session->visible(), item.text_size, item.caret_x(0), editor.y + item.text_top, enabled ? p.text : p.muted);
        if (focused && c.window_focused && enabled) marked(c, PaintRole::caret, [&] { c.fill({at(session->visible_caret()), editor.y, d(c.appearance.component_presentation(c.ui.presentation().density).thin) + 1, editor.h}, p.accent); });
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
        int room = std::max(1, w - 2 * c.appearance.button_padding(c.scale, c.ui.presentation().density));
        auto lines = wrap(c.measure, command_title(c), p.text_size, room);
        int text_height = static_cast<int>(lines.size()) * line;
        int h = c.appearance.button_height(text_height, c.scale, c.ui.presentation().density);
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
        auto p = c.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        bool focused = c.focus_visible, enabled = c.semantics.enabled;
        int ring = focused ? d(c.appearance.component_presentation(c.ui.presentation().density).focus) : d(c.appearance.component_presentation(c.ui.presentation().density).thin);
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
        int extra = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).checkbox_extra);
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
        paint_lines(c, c.placement.description_lines, c.placement.text_size, c.palette().muted);
        paint_errors(c, c.placement.error_lines, c.placement.text_size, c.palette().error);
        auto p = c.palette();
        int size = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).indicator), x = c.placement.bounds.x + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap);
        int y = c.placement.bounds.y + (c.appearance.button_height(c.placement.lines.front().bounds.h, c.scale, c.ui.presentation().density) - size) / 2;

        c.fill({x, y, size, size}, c.semantics.enabled ? p.outline : p.border);
        int inset = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset);
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
        int line = c.measure.line_height(p.text_size), row = std::max(scaled(c.scale, 24), line + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap));
        p.lines.push_back({{0, 0, c.width, line}, semantics.name + (semantics.enabled ? u"" : c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix)});
        int y = line + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small);
        for (const auto& option : semantics.options) {
            p.options.push_back({0, y, c.width, row});
            p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).radio_label_offset), y + (row - line) / 2, std::max(0, c.width - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).radio_label_offset)), line}, option.label + (option.enabled ? u"" : c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix)});
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
        auto p = c.palette();
        const auto& item = c.placement;
        if (!item.lines.empty()) c.text(item.lines[0].text, item.text_size, item.lines[0].bounds.x, item.lines[0].bounds.y, c.semantics.enabled ? p.text : p.muted);
        if (c.focus_visible) marked(c, PaintRole::focus_indicator, [&] { c.fill({item.bounds.x, item.bounds.y, scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset), item.bounds.h}, p.accent); });
        for (std::size_t i = 0; i < item.options.size(); ++i) {
            const auto& r = item.options[i]; const auto& option = c.semantics.options[i];
            int size = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).indicator), inset = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).focus);
            int x = r.x + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small), y = r.y + (r.h - size) / 2;
            auto color = option.enabled && c.semantics.enabled ? p.outline : p.border;
            c.rounded({x, y, size, size}, size / 2, color);
            c.rounded({x + inset, y + inset, size - 2 * inset, size - 2 * inset}, size / 2, option.selected ? p.accent : p.surface);
            const auto& label = item.lines[i + 1];
            c.text(label.text, item.text_size, label.bounds.x, label.bounds.y, option.enabled && c.semantics.enabled ? p.text : p.muted);
            if (c.focus_visible && option.selected) marked(c, PaintRole::focus_indicator, [&] { c.fill({r.x, r.y + r.h - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset), r.w, scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset)}, p.accent); });
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
        int line = c.measure.line_height(p.text_size), row = line + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).row_padding);
        p.label_lines = block(c.measure, s.name + (s.enabled ? u"" : c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix), p.text_size, c.width);
        int y = height_of(p.label_lines);
        if (y) y += scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small);
        if (s.options.empty()) {
            p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap), y, c.width, line}, c.appearance.component_presentation(c.ui.presentation().density).no_items});
            y += row;
        }
        for (const auto& item : s.options) {
            p.options.push_back({0, y, c.width, row});
            auto text = (item.selected ? c.appearance.component_presentation(c.ui.presentation().density).selected_marker : c.appearance.component_presentation(c.ui.presentation().density).unselected_marker) + item.label;
            if (!item.enabled) text += c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix;
            p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap), y + (row - line) / 2, std::max(0, c.width - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).body_spacing)), line}, std::move(text)});
            y += row;
        }
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette(); const auto& p = c.placement;
        paint_lines(c, p.label_lines, p.text_size, c.semantics.enabled ? palette.text : palette.muted);
        if (c.semantics.options.empty()) { paint_lines(c, p.lines, p.text_size, palette.muted); return; }
        for (std::size_t i = 0; i < p.options.size(); ++i) {
            const auto& item = c.semantics.options[i]; const auto& row = p.options[i];
            // Avoid shaping and submitting text for rows entirely outside the viewport.
            const auto visible = intersect(row, p.clip);
            if (visible.w <= 0 || visible.h <= 0) continue;
            if (item.selected) marked(c, PaintRole::selection, [&] { c.fill(row, palette.selection); });
            if (c.focus_visible && c.semantics.candidate == item.value)
                marked(c, PaintRole::focus_indicator, [&] { c.fill({row.x, row.y, scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).focus), row.h}, palette.accent); });
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
        int line = c.measure.line_height(p.text_size), row = c.appearance.button_height(line, c.scale, c.ui.presentation().density);
        p.lines.push_back({{0, 0, c.width, line}, s.name + (s.enabled ? u"" : c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix)});
        p.lines.push_back({{scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap), line + (row - line) / 2, std::max(0, c.width - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).body_spacing)), line}, s.value.empty() ? c.appearance.component_presentation(c.ui.presentation().density).no_selection : s.value});
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
        auto palette = c.palette();
        auto bounds = c.placement.bounds;
        if (c.placement.dropdown_panel) {
            c.clip(bounds);
            c.fill(bounds, palette.surface);
            if (c.semantics.options.empty()) {
                paint_lines(c, c.placement.lines, c.placement.text_size, palette.muted);
                return;
            }
            for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
                const auto& o = c.semantics.options[c.placement.option_start + i];
                const auto& r = c.placement.options[i];
                if (c.semantics.candidate && *c.semantics.candidate == o.value) marked(c, PaintRole::selection, [&] { c.fill(r, palette.selection); });
                const auto& line = c.placement.lines[i];
                c.text(o.enabled ? line.text : line.text + c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix, c.placement.text_size, line.bounds.x, line.bounds.y, o.enabled ? palette.text : palette.muted);
            }
            return;
        }
        marked(c, (c.focus_visible ? PaintRole::focus_indicator : PaintRole::content) | (c.semantics.invalid ? PaintRole::error_indicator : PaintRole::content), [&] { c.rounded(bounds, scaled(c.scale, c.appearance.radius()), c.semantics.invalid ? palette.error : c.focus_visible ? palette.accent : c.semantics.enabled ? palette.outline : palette.border); });
        int inset = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset);
        c.rounded({bounds.x + inset, bounds.y + inset, std::max(0, bounds.w - 2 * inset), std::max(0, bounds.h - 2 * inset)}, scaled(c.scale, c.appearance.radius()), palette.surface);
        paint_lines(c, c.placement.lines, c.placement.text_size, c.semantics.enabled ? palette.text : palette.muted);
        if (c.placement.lines.size() > 1) {
            const auto& line = c.placement.lines[1].bounds;
            c.text(c.appearance.component_presentation(c.ui.presentation().density).dropdown_marker, c.placement.text_size, bounds.x + std::max(0, bounds.w - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).dropdown_marker_space)), line.y,
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
        int line = c.measure.line_height(p.text_size), pad = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).slider_pad), track_height = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).slider_height);
        auto title = semantics.name + u": " + semantics.value + (semantics.enabled ? u"" : c.appearance.component_presentation(c.ui.presentation().density).disabled_suffix);
        if (semantics.source_conflict) title += c.appearance.component_presentation(c.ui.presentation().density).conflict_suffix;
        auto dot = slider_dot(c.appearance, c.scale, p.text_size, c.ui.presentation().density);
        p.lines = block(c.measure, title, p.text_size, std::max(1, c.width - dot.room()));
        for (auto& l : p.lines) l.bounds.x += dot.room();
        int y = height_of(p.lines) + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small);
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
        const auto& item = c.placement; auto palette = c.palette();
        paint_lines(c, item.lines, item.text_size, c.semantics.enabled ? palette.text : palette.muted);
        paint_errors(c, item.error_lines, item.text_size, palette.error);
        auto track = item.slider_track; int thickness = scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).focus);
        c.fill({track.x, track.y + track.h / 2 - thickness / 2, track.w, thickness}, c.semantics.enabled ? palette.outline : palette.border);
        if (c.semantics.slider_range && c.semantics.numeric_value) {
            const auto range = *c.semantics.slider_range;
            double ratio = (static_cast<double>(*c.semantics.numeric_value) - range.minimum) /
                           (static_cast<double>(range.maximum) - range.minimum);
            int x = track.x + static_cast<int>(std::lround(track.w * ratio));
            // While the pointer holds the thumb it sits under the pointer (kept on the current track);
            // the value is still the snapped one. Releasing or interrupting the drag slides it back to the
            // tick, and a value change from the user's own input slides it to the new tick (#75).
            if (const auto animated = c.ui.slider_animated_ratio(item.id)) x = track.x + static_cast<int>(std::lround(track.w * *animated));
            else if (const auto held = c.ui.slider_held_x(item.id)) x = std::clamp(*held, track.x, track.x + track.w);
            int size = scaled(c.scale, c.focus_visible ? c.appearance.component_presentation().body_spacing : c.appearance.component_presentation().row_padding);
            c.rounded({x - size / 2, track.y + (track.h - size) / 2, size, size}, size / 2,
                      c.semantics.enabled ? palette.accent : palette.muted);
        }
        if (c.focus_visible && !item.lines.empty()) {
            auto dot = slider_dot(c.appearance, c.scale, item.text_size, c.ui.presentation().density);
            const auto& first = item.lines.front().bounds;
            marked(c, PaintRole::focus_indicator, [&] { c.rounded({first.x - dot.room(), first.y + (first.h - dot.diameter) / 2, dot.diameter, dot.diameter}, dot.diameter / 2, palette.accent); });
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
        int y = height_of(p.lines) + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small);
        p.progress_track = {0, y, std::max(0, c.width), scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).row_padding)};
        p.progress_fill = p.progress_track;
        p.progress_fill.w = static_cast<int>(static_cast<long long>(p.progress_track.w) * semantic.numeric_value.value_or(0) / 100);
        p.bounds = {0, 0, c.width, y + p.progress_track.h};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        paint_lines(c, c.placement.lines, c.placement.text_size, palette.text);
        // The track is the only cue of the bar's extent when no value text is shown; with text it stays decorative.
        c.fill(c.placement.progress_track, c.semantics.value.empty() ? palette.outline : palette.border);
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
        int height = c.appearance.button_height(c.measure.line_height(c.placement.text_size), c.scale, c.ui.presentation().density);
        c.placement.label_lines = block(c.measure, semantics.name, c.placement.text_size, c.width);
        int top = height_of(c.placement.label_lines);
        if (top) top += scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small);
        int count = static_cast<int>(semantics.options.size());
        for (int i = 0; i < count; ++i) {
            int left = c.width * i / count, right = c.width * (i + 1) / count;
            c.placement.options.push_back({left, top, right - left, height});
            const auto& option = semantics.options[static_cast<std::size_t>(i)];
            std::u16string prefix = option.invalid ? c.appearance.component_presentation(c.ui.presentation().density).invalid_tab_marker : u"";
            if (!option.enabled) prefix += c.appearance.component_presentation(c.ui.presentation().density).disabled_tab_marker;
            auto label = option.label;
            int room = std::max(0, right - left - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap));
            while (!label.empty() && c.measure.measure(prefix + label, c.placement.text_size).width > room) {
                if (label.back() >= 0xdc00 && label.back() <= 0xdfff && label.size() > 1) label.pop_back();
                label.pop_back();
            }
            label = prefix + label;
            c.placement.lines.push_back({{left + scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap_small), top + (height - c.measure.line_height(c.placement.text_size)) / 2,
                                         std::max(0, right - left - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).gap)), height}, std::move(label)});
        }
        c.placement.bounds = {0, 0, c.width, top + height};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        paint_lines(c, c.placement.label_lines, c.placement.text_size, palette.text);
        for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
            const auto& option = c.semantics.options[i]; auto rect = c.placement.options[i];
            marked(c, option.selected ? PaintRole::selection : PaintRole::content, [&] { c.fill(rect, option.selected ? palette.selection : palette.surface); });
            if (option.selected) marked(c, PaintRole::selection, [&] { c.fill({rect.x, rect.y + rect.h - scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).focus), rect.w, scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).focus)}, palette.accent); });
            if (c.focus_visible && c.semantics.candidate && *c.semantics.candidate == option.value)
                marked(c, PaintRole::focus_indicator, [&] { c.fill({rect.x, rect.y, rect.w, scaled(c.scale, c.appearance.component_presentation(c.ui.presentation().density).inset)}, palette.accent); });
            const auto& line = c.placement.lines[i];
            marked(c, option.invalid ? PaintRole::error_text : PaintRole::content, [&] { c.text(line.text, c.placement.text_size, line.bounds.x, line.bounds.y, option.enabled ? palette.text : palette.muted); });
        }
    }
    bool has_activate_operation() const override { return true; }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.tabs_key_down(key); }
};
class NavigationList final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected}); }
    ComponentHandle handle(UiState& ui, const Control& c) const override { return ui.navigation_layout_handle(c); }
    int natural_width(const ComponentLayout& c) const override {
        auto semantic = c.ui.describe(c.control.id);
        int width = 0;
        for (const auto& option : semantic->options) width = std::max(width, static_cast<int>(c.measure.measure(option.label, c.placement.text_size).width));
        return std::max(scaled(c.scale, 160), width + scaled(c.scale, 48));
    }
    void arrange(ComponentLayout& c) const override {
        auto semantic = *c.ui.describe(c.control.id);
        int line = c.measure.line_height(c.placement.text_size);
        bool material = c.appearance.name() == "Material-inspired";
        int padding = material ? 32 : 16;
        if (c.ui.presentation().density == Density::compact) padding /= 2;
        int height = std::max(scaled(c.scale, 24), line + scaled(c.scale, padding));
        for (std::size_t i = 0; i < semantic.options.size(); ++i) {
            int y = static_cast<int>(i) * height;
            c.placement.options.push_back({0, y, c.width, height});
            auto label = semantic.options[i].label;
            if (!semantic.options[i].enabled) label += u"\uff08\u505c\u7528\uff09";
            c.placement.lines.push_back({{scaled(c.scale, 16), y + (height-line)/2, c.width-scaled(c.scale, 32), line}, std::move(label)});
        }
        c.placement.bounds = {0, 0, c.width, height * static_cast<int>(semantic.options.size())};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        bool material = c.appearance.name() == "Material-inspired";
        c.fill(c.placement.bounds, material ? palette.surface : palette.panel);
        if (!material) c.fill({c.placement.bounds.x+c.placement.bounds.w-scaled(c.scale,1), c.placement.bounds.y, scaled(c.scale,1), c.placement.bounds.h}, palette.border);
        for (std::size_t i=0; i<c.placement.options.size(); ++i) {
            const auto& option = c.semantics.options[i]; auto rect = c.placement.options[i];
            if (option.selected) {
                int radius = material ? rect.h / 2 : scaled(c.scale, 6);
                if (c.focus_visible) marked(c, PaintRole::focus_indicator, [&] { c.rounded(rect, radius, palette.accent); });
                auto inset = c.focus_visible ? scaled(c.scale, 2) : 0;
                marked(c, PaintRole::selection, [&] {
                    c.rounded({rect.x+inset, rect.y+inset, rect.w-2*inset, rect.h-2*inset}, std::max(0, radius-inset), palette.selection);
                    if (!material) c.fill({rect.x, rect.y, scaled(c.scale, 3), rect.h}, palette.accent);
                });
            }
            const auto& line = c.placement.lines[i];
            c.text(line.text, c.placement.text_size, line.bounds.x, line.bounds.y, option.enabled && c.semantics.enabled ? palette.text : palette.muted);
        }
    }
    Result key_down(UiState& ui, KeyPress key) const override { return ui.navigation_key_down(key); }
};
NavigationList navigation_list;
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
        {Kind::checkbox, &checkbox}, {Kind::radio_group, &radio_group}, {Kind::slider, &slider}, {Kind::dropdown, &dropdown}, {Kind::list, &list}, {Kind::tabs, &tabs}, {Kind::progress, &progress}, {Kind::navigation_list, &navigation_list}};
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
