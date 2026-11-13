#include "component_type.hpp"
#include "core_internal.hpp"
#include "vertical.hpp"
#include "justify.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lcars::detail {
// The per-type state and the engines a component type reaches into. UiState only declares this class, so nothing outside
// this file can use it: the maps, and the helpers of the engines the types share with the rest of the state.
struct UiState::Internals {
    template <class U> static auto& edits(U& ui) { return ui.edits_; }
    template <class U> static auto& bools(U& ui) { return ui.bools_; }
    template <class U> static auto& radios(U& ui) { return ui.radios_; }
    template <class U> static auto& dropdowns(U& ui) { return ui.dropdowns_; }
    template <class U> static auto& tabs(U& ui) { return ui.tabs_; }
    template <class U> static auto& lists(U& ui) { return ui.lists_; }
    template <class U> static auto& numerics(U& ui) { return ui.numerics_; }
    template <class U> static auto& progresses(U& ui) { return ui.progresses_; }
    template <class U> static auto& errors(U& ui) { return ui.errors_; }
    template <class U> static auto& providers(U& ui) { return ui.providers_; }
    template <class U> static auto& shortcuts(U& ui) { return ui.shortcuts_; }
    template <class U, class F> static bool source_matches(const U& ui, const F& field) { return ui.source_matches(field); }
    template <class U, class F> static auto insertion_style(const U& ui, const F& field, unsigned first, unsigned last) { return ui.insertion_style(field, first, last); }
    template <class U> static const WordBreaker& dictionary(const U& ui) { return ui.dictionary(); }
    static Result select_option(UiState& ui, const Id& id, OptionValue value) { return ui.select_option(id, std::move(value)); }
    static Result select_dropdown(UiState& ui, const Id& id, OptionValue value) { return ui.select_dropdown(id, std::move(value)); }
    static double slider_display_ratio(const UiState& ui, std::string_view id) { return ui.slider_display_ratio(id); }
    static void slider_started_moving(UiState& ui, const Id& id, double from, int value_before) { ui.slider_started_moving(id, from, value_before); }
    static Result edit_key(UiState& ui, KeyPress key) { return ui.edit_key(key); }
    static Result delete_selection(UiState& ui, std::string_view id) { return ui.delete_selection(id); }
};
Palette ComponentPaint::palette() const { return ui.effective_palette(appearance); }
ComponentPresentation ComponentPaint::presentation() const { return appearance.component_presentation(ui.presentation().density); }
ComponentPresentation ComponentLayout::presentation() const { return appearance.component_presentation(ui.presentation().density); }
// The width a line of text keeps when it starts at `text_x` instead of at its natural `gap` (a ring pushes text inward) and `trailing`
// pixels stay free on the right.
static int room_after(int width, int trailing, int text_x, int gap) { return std::max(0, width - trailing - (text_x - gap)); }
int text_row(int line_height, double line_spacing) {
    return std::max(line_height, static_cast<int>(std::lround(line_height * line_spacing)));
}
int paragraph_gap(const ComponentConfig& control, float scale) {
    return control.paragraph_spacing > 0 ? static_cast<int>(std::lround(control.paragraph_spacing * scale)) : 0;
}
bool ComponentPaint::hovered() const { return ui.hover_drawn(placement.id, nullptr); }
bool ComponentPaint::hovered(const OptionValue& item) const { return ui.hover_drawn(placement.id, &item); }
void ComponentPaint::hover(Rect rect, int radius) {
    auto p = palette();
    if (!p.hover_alpha) return;
    role(PaintRole::hover);
    overlay(rect, radius, p.hover, p.hover_alpha);
    role(PaintRole::content);
}
void ComponentPaint::overlay(Rect rect, int radius, Color color, unsigned char alpha) {
    overlay_alpha_ = alpha;
    if (radius > 0) rounded(rect, radius, color); else fill(rect, color);
    overlay_alpha_ = 255;
}

TextAlign drawn_text_align(TextAlign align, const PresentationSettings& settings) {
    const bool stretched = align == TextAlign::justify || align == TextAlign::distribute;
    return stretched && settings.justify_as_left ? TextAlign::left : align;
}
TextAlign drawn_text_align(const ComponentConfig& control, const PresentationSettings& settings) { return drawn_text_align(control.text_align, settings); }
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
Flowed flowed_block(TextMeasure& measure, std::u16string_view text, int size, int width, const ComponentConfig& control, float scale, TextAlign align) {
    Flowed out;
    if (text.empty()) return out;
    const int line = measure.line_height(size), row = text_row(line, control.line_spacing), gap = paragraph_gap(control, scale);
    int y = 0;
    const auto pieces = wrap_marked(measure, text, size, width);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        Line placed{{0, y + (row - line) / 2, width, line}, pieces[i].text};
        if (align == TextAlign::center || align == TextAlign::right) {
            const int w = static_cast<int>(std::ceil(measure.measure(pieces[i].text, size).width));
            const int room = std::max(0, width - w);
            placed.bounds.x = align == TextAlign::center ? room / 2 : room;
            placed.bounds.w = w;
        } else if (align == TextAlign::distribute || (align == TextAlign::justify && pieces[i].soft)) {
            // Stretched to the full width, in pieces; a line with nowhere to stretch stays left-aligned.
            if (auto stretched = justify_line(measure, pieces[i].text, size, measure.measure(pieces[i].text, size), static_cast<float>(width), control.justify_method))
                placed.segments = std::move(stretched->segments);
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
    if (semantic && !semantic->enabled) title += c.presentation().disabled_suffix;
    if (semantic && semantic->executing) title += c.presentation().executing_suffix;
    return title;
}
int scaled(float scale, int value) { return std::max(1, static_cast<int>(std::lround(value * scale))); }
// A command's side padding never falls below the focus ring's thickness, so the ring cannot reach the title.
int command_padding(const ComponentLayout& c) {
    return std::max(c.appearance.button_padding(c.scale, c.ui.presentation().density), focus_ring_width(c.ui, c.appearance, c.scale));
}
int command_width(const ComponentLayout& c) {
    return static_cast<int>(std::ceil(c.measure.measure(command_title(c), c.placement.text_size).width)) + 2 * command_padding(c);
}
// The height of a button-like box around `content` pixels of title and indicator: the appearance's height, never
// less than the content plus a ring's thickness above and below it.
int command_height(const UiState& ui, const Appearance& appearance, float scale, int text_height, int content) {
    return std::max(appearance.button_height(text_height, scale, ui.presentation().density), content + 2 * focus_ring_width(ui, appearance, scale));
}
// The box of a button-like component: the whole placement unless the component stacks error or conflict text below it.
Rect command_box(const ComponentPaint& c, int indicator) {
    const auto& item = c.placement;
    const int lines = static_cast<int>(item.lines.size()), line_height = lines ? item.lines.front().bounds.h : 0;
    const int text_height = lines * line_height;
    Rect box = item.bounds;
    box.h = std::min(box.h, command_height(c.ui, c.appearance, c.scale, text_height, std::max(text_height, indicator)));
    return box;
}
Rect offset(Rect rect, Rect origin) { return {rect.x + origin.x, rect.y + origin.y, rect.w, rect.h}; }
// The one place the lines of a message under a component are laid out: `text` wrapped to `width` pixels into `out`, the
// lines stacked from row `y` down and `inset` pixels in from the left. Returns the row below the last line.
int stack_lines(ComponentLayout& c, std::vector<Line>& out, std::u16string_view text, int y, int inset, int width) {
    out = block(c.measure, text, c.placement.text_size, width);
    for (auto& line : out) { line.bounds.x += inset; line.bounds.y += y; }
    return y + height_of(out);
}
// The source-conflict note under a component (a second line of text in the description lines), when there is a conflict.
int conflict_lines(ComponentLayout& c, int y, bool conflict, int inset = 0) {
    if (!conflict) return y;
    return stack_lines(c, c.placement.description_lines, c.presentation().conflict_suffix, y, inset, std::max(1, c.width - 2 * inset));
}
// The error message under a component, when it is invalid. Text set in from the edges keeps at least one pixel of width;
// text across the whole width takes the width as given, even none. (A text field does not use this: its appearance decides the
// rectangle of its error text, so arrange_field wraps the message to that rectangle and anchors it there.)
int error_lines(ComponentLayout& c, int y, const Semantics& semantics, int inset = 0) {
    if (!semantics.invalid) return y;
    return stack_lines(c, c.placement.error_lines, semantics.error, y, inset, inset > 0 ? std::max(1, c.width - 2 * inset) : c.width);
}
template<class F> void marked(ComponentPaint& c, PaintRole role, F&& paint) { c.role(role); paint(); c.role(PaintRole::content); }
void paint_errors(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    marked(c, PaintRole::error_text, [&] { for (const auto& line : lines) c.text(line.text, pixels, line.bounds.x, line.bounds.y, color); });
}
// A justified line is drawn piece by piece, every piece at its own x, so nothing (a ligature, kerning) spans the added width.
// Calls draw(text of the piece, x of the piece from the line's start) for each segment lying inside [from, to) of the line;
// the caller turns x into a pixel the way its component rounds it. Offsets are relative to the line text's start.
template<class Draw> void draw_segments(const std::vector<TextSegment>& segments, std::u16string_view line_text, unsigned from, unsigned to, Draw&& draw) {
    for (const auto& piece : segments)
        if (piece.begin >= from && piece.end <= to) draw(line_text.substr(piece.begin, piece.end - piece.begin), piece.x);
}
void paint_lines(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    for (const auto& line : lines) {
        if (line.segments.empty()) { c.text(line.text, pixels, line.bounds.x, line.bounds.y, color); continue; }
        draw_segments(line.segments, line.text, 0, static_cast<unsigned>(line.text.size()), [&](std::u16string_view piece, float x) {
            c.text(piece, pixels, line.bounds.x + static_cast<int>(std::floor(x)), line.bounds.y, color);  // a label floors, so a piece never starts past its place
        });
    }
}
// The focus ring of a text field is drawn at the edge of its surface, so whatever the surface holds (the editor and, in an
// appearance that puts it inside, the label) keeps a ring's thickness of room from every edge. Missing room is added: the
// content moves in from the sides, and the surface grows at the top and bottom, pushing what follows it down. It does this
// whether or not the field is focused, so focus never moves anything.
void keep_ring_room(FieldLayout& f, int ring) {
    const Rect surface = f.surface;
    if (surface.w <= 0 || surface.h <= 0) return;
    auto held = [&](const Rect& r) {
        return r.w > 0 && r.h > 0 && r.x >= surface.x && r.y >= surface.y && r.x + r.w <= surface.x + surface.w && r.y + r.h <= surface.y + surface.h;
    };
    Rect* contents[] = {&f.label, &f.editor};
    int left = ring, right = ring, top = ring, bottom = ring;  // the room each side already has, capped at what is needed
    for (const Rect* r : contents) {
        if (!held(*r)) continue;
        left = std::min(left, r->x - surface.x);
        right = std::min(right, surface.x + surface.w - (r->x + r->w));
        top = std::min(top, r->y - surface.y);
        bottom = std::min(bottom, surface.y + surface.h - (r->y + r->h));
    }
    const int add_left = ring - left, add_right = ring - right, add_top = ring - top, add_bottom = ring - bottom;
    if (!add_left && !add_right && !add_top && !add_bottom) return;
    const int grow = add_top + add_bottom, below = surface.y + surface.h;
    for (Rect* r : contents)
        if (held(*r)) { r->x += add_left; r->w = std::max(0, r->w - add_left - add_right); r->y += add_top; }
    for (Rect* r : {&f.description, &f.error}) if (r->h > 0 && r->y >= below) r->y += grow;
    f.surface.h += grow;
    f.height += grow;
}
bool plain_key(KeyPress key) { return !key.ctrl || !key.letter; }
bool arrow_key(Key key) {
    return key == Key::left || key == Key::right || key == Key::up || key == Key::down || key == Key::home || key == Key::end;
}
template <class T> bool diverged(bool connected, const T& origin, const T& source, const T& value) { return connected && origin != source && value != source; }
// A text field the user can change now: not read only, not composing, with a source and a write timing. The edit operations
// and the editing items of the context menu both follow this one rule.
template <class Field> bool text_editable(const ComponentConfig& control, const Field& field) {
    return !control.read_only && !field.session.composing && field.writable();
}
std::u16string number_text(int value) { auto digits = std::to_string(value); return {digits.begin(), digits.end()}; }
template <class Fields> bool has_confirmed_write(const Fields& fields, const ComponentConfig& c) {
    auto it = fields.find(c.id);
    return it != fields.end() && bool(it->second.confirmed);
}
bool is_flow_verb(Verb verb) { return verb == Verb::set_text_align || verb == Verb::set_justify_method || verb == Verb::set_line_spacing || verb == Verb::set_paragraph_spacing; }
// Labels and text fields share the text flow verbs; the same UiState entry points as the typed handles, so the same Status.
void flow_verbs(const VerbContext& c, std::vector<Verb>& out) {
    if (c.ui.renderer_failed()) return;
    for (auto flow : {Verb::set_text_align, Verb::set_justify_method, Verb::set_line_spacing, Verb::set_paragraph_spacing}) out.push_back(flow);
}
std::optional<Result> flow_perform(const PerformContext& c) {
    if (c.ref.item || !is_flow_verb(c.verb)) return std::nullopt;
    if (c.verb == Verb::set_text_align) {
        if (auto align = std::get_if<TextAlign>(&c.argument)) return c.ui.set_text_align(c.ref.id, *align);
    } else if (c.verb == Verb::set_justify_method) {
        if (auto method = std::get_if<JustifyMethod>(&c.argument)) return c.ui.set_justify_method(c.ref.id, *method);
    } else if (c.verb == Verb::set_line_spacing) {
        if (auto multiple = std::get_if<double>(&c.argument)) return c.ui.set_line_spacing(c.ref.id, *multiple);
    } else if (auto dip = std::get_if<int>(&c.argument)) return c.ui.set_paragraph_spacing(c.ref.id, *dip);
    return c.invalid_argument();
}
// Tabs and navigation lists share their pages: the candidate, the options with their error marks, the selected page's label.
void describe_pages(const UiState& ui, const ComponentConfig& c, Semantics& s) {
    auto& all = UiState::Internals::tabs(ui);
    auto it = all.find(c.id);
    if (it == all.end()) return;
    const auto& t = it->second;
    s.candidate = t.pages[t.candidate].value;
    for (std::size_t i = 0; i < t.pages.size(); ++i) {
        const auto& page = t.pages[i];
        bool invalid = false;
        for (const auto& member : page.members) {
            auto member_id = ui.layout_id(member);
            if (member_id) {
                auto& errors = UiState::Internals::errors(ui);
                auto error = errors.find(*member_id);
                if (error != errors.end() && !error->second.empty()) { invalid = true; break; }
            }
        }
        s.options.push_back({page.value, page.label, page.enabled, i == t.active, invalid});
        if (i == t.active) s.value = page.label;
    }
}
// A release over the page the press armed selects it: tabs and navigation lists share their pages.
Result page_release(UiState& ui, const PointerRelease& release) {
    auto& pages = UiState::Internals::tabs(ui).at(release.id).pages;
    if (!release.pressed_option || release.pressed_option != release.option || *release.option >= pages.size()) return {};
    return ui.activate_tab(release.id, pages[*release.option].value);
}
class Label final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal}); }
    std::optional<std::u16string> display_text(const UiState& ui, const ComponentConfig& c) const override {
        if (c.role == TextRole::error) {
            auto& errors = UiState::Internals::errors(ui);
            auto it = errors.find(c.target);
            return it == errors.end() ? std::u16string{} : it->second;
        }
        auto& providers = UiState::Internals::providers(ui);
        if (auto it = providers.find(c.id); it != providers.end()) return it->second();
        return std::nullopt;
    }
    bool accepts_text_provider() const override { return true; }
    void describe_node(const UiState&, const ComponentConfig&, const Semantics& s, const NodeRef&, SemanticNode& n) const override {
        n.writing_mode = s.writing_mode; n.vertical_digits = s.vertical_digits;
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { out.push_back(Verb::set_writing_mode); out.push_back(Verb::set_vertical_digits); }
        ComponentType::verbs(c, out);
        if (!c.item) flow_verbs(c, out);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (!c.ref.item && (c.verb == Verb::set_writing_mode || c.verb == Verb::set_vertical_digits)) {
            if (auto result = c.ui.available(c.ref.id); !result) return result;  // hidden, or behind a modal dialog
            auto handle = c.ui.layout_handle(c.ref.id);
            const auto& label = std::get<LabelHandle>(handle);
            if (c.verb == Verb::set_writing_mode) {
                if (const auto* mode = std::get_if<WritingMode>(&c.argument)) return label.set_writing_mode(*mode);
            } else if (const auto* digits = std::get_if<int>(&c.argument)) return label.set_vertical_digits(*digits);
            return Result{Status::invalid_value, {}};
        }
        return flow_perform(c);
    }
    std::string_view name() const override { return "label"; }
    Role role() const override { return Role::label; }
    bool supports_text_flow() const override { return true; }
    bool admits_distribute() const override { return true; }
    bool supports_vertical_writing() const override { return true; }
    bool accepts_error() const override { return false; }
    bool needs_name() const override { return false; }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress) const override { return false; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<LabelHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        if (c.control.writing_mode == WritingMode::vertical) { arrange_vertical(c); return; }  // justify/distribute read as top there (vertical_params)
        if (c.settle) c.ui.clear_vertical_scroll(c.control.id);
        auto flowed = flowed_block(c.measure, c.ui.text(c.control.id), p.text_size, c.width, c.control, c.scale, drawn_text_align(c.control, c.ui.presentation()));
        p.lines = std::move(flowed.lines);
        p.bounds = {0, 0, c.width, flowed.height};
    }
    // Columns of the given height from right to left; the box is as wide as its columns or the space offered,
    // and scrolls sideways (offset counted from the right edge) when the columns are wider.
    static void arrange_vertical(ComponentLayout& c) {
        auto& p = c.placement;
        p.vertical = true;
        const int em = std::max(1, p.text_size);
        const auto params = vertical_params(c.control, c.scale);
        const int pitch = std::max(1, static_cast<int>(std::lround(c.measure.line_height(em) * params.line_spacing)));
        const int bar = scaled(c.scale, 10), gap = scaled(c.scale, 4);
        const auto text = c.ui.text(c.control.id);
        auto content_width = [&](const std::vector<VerticalColumn>& columns) {
            int width = 0;
            for (std::size_t i = 0; i < columns.size(); ++i) width += pitch + (columns[i].last_in_paragraph && i + 1 < columns.size() ? params.paragraph_spacing : 0);
            return width;
        };
        auto columns = flow_vertical(c.measure, text, em, c.control.vertical_digits, c.vertical_height);
        int content = content_width(columns);
        const bool sideways = content > c.width;
        // A label that scrolls can take focus and is framed by the ring at the edge of its box: the columns, the bar and
        // the reading position all keep a ring's thickness from that edge (the same room is kept focused or not).
        const int ring = sideways ? focus_ring_width(c.ui, c.appearance, c.scale) : 0;
        const int bar_room = sideways ? bar + gap : 0;
        if (sideways && c.vertical_height > 0) {  // the bar and the ring take some of the height: wrap again for the shorter columns
            columns = flow_vertical(c.measure, text, em, c.control.vertical_digits, std::max(em, c.vertical_height - bar_room - 2 * ring));
            content = content_width(columns);
        }
        int tallest = 0;
        for (const auto& column : columns) tallest = std::max(tallest, column.bounds.h);
        const int area = c.vertical_height > 0 ? std::max(0, c.vertical_height - bar_room - 2 * ring) : tallest;
        const int width = std::min(content, c.width), view = std::max(0, width - 2 * ring), maximum = std::max(0, content - view);
        auto& state = c.ui.vertical_scroll(c.control.id);
        const int offset = std::clamp(state.offset, 0, maximum);
        if (c.settle) { state.offset = offset; c.ui.set_vertical_scroll(c.control.id, maximum, pitch); }
        int cursor = content;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            auto& column = columns[i];
            cursor -= pitch;
            int slack = std::max(0, area - column.bounds.h);
            int top = params.align == VerticalParams::Align::center ? slack / 2 : params.align == VerticalParams::Align::bottom ? slack : 0;
            column.bounds = {cursor - (content - view) + offset + ring, top + ring, pitch, column.bounds.h};
            p.lines.push_back({column.bounds, text.substr(column.begin, column.end - column.begin)});
            if (column.last_in_paragraph && i + 1 < columns.size()) cursor -= params.paragraph_spacing;
        }
        p.bounds = {0, 0, width, area + bar_room + 2 * ring};
        auto& v = p.column_layout;
        v.em = em; v.pitch = pitch; v.extent = content; v.maximum = maximum; v.offset = offset;
        if (maximum > 0) {
            v.track = {ring, p.bounds.h - ring - bar, view, bar};
            int thumb = std::min(view, std::max(scaled(c.scale, 24), view * view / std::max(1, content)));
            v.thumb = {ring + (view - thumb) * (maximum - offset) / maximum, v.track.y, thumb, bar};
        }
        v.columns = std::move(columns);
    }
    static void paint_vertical(ComponentPaint& c, Color color) {
        const auto& v = c.placement.column_layout;
        const auto text = c.ui.text(c.placement.id);
        auto paint = [&] {
            for (const auto& column : v.columns) {
                if (column.bounds.x + column.bounds.w <= c.placement.clip.x || column.bounds.x >= c.placement.clip.x + c.placement.clip.w) continue;
                for (const auto& piece : column.pieces) {
                    Rect cell = piece.form == VerticalForm::sideways ? Rect{column.bounds.x, column.bounds.y + piece.y, v.pitch, piece.advance}
                                                                     : Rect{column.bounds.x + (v.pitch - v.em) / 2, column.bounds.y + piece.y, v.em, v.em};
                    c.vertical_text(std::u16string_view(text).substr(piece.begin, piece.end - piece.begin), piece.form, v.em, cell, color);
                }
            }
        };
        if (c.semantics.role == TextRole::error) marked(c, PaintRole::error_text, paint); else paint();
    }
    void paint(ComponentPaint& c) const override {
        if (c.focus_visible) {  // a scrolling text: the ring frames its viewport, the text slides under the band, never over it
            const auto shape = c.focus_ring(c.placement.clip, scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
            c.clip(shape.inner);
        }
        if (c.placement.vertical) paint_vertical(c, c.palette().text);
        else if (c.semantics.role == TextRole::error) paint_errors(c, c.placement.lines, c.placement.text_size, c.palette().text);
        else paint_lines(c, c.placement.lines, c.placement.text_size, c.palette().text);
    }
    bool can_focus() const override { return false; }
    bool can_target() const override { return true; }
    bool accepts_pointer() const override { return false; }
};
class Text final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::read_only, S::error, S::conflict}); }
    void adopt(UiState& ui, const ComponentConfig& c) const override {
        if (!valid_text_control(c)) throw std::invalid_argument("Invalid initial text");
        auto& field = UiState::Internals::edits(ui)[c.id];
        field.session.text = c.value; field.rich = c.rich;
    }
    bool focus_always_visible(const Semantics& s) const override { return !s.read_only && s.enabled; }
    bool labels_in_placement() const override { return true; }
    // The caret's own line, also in a multi-line field.
    std::optional<Rect> keyboard_anchor(UiState& state, const Placement& target, const std::optional<OptionValue>&, int) const override {
        if (const auto* session = state.edit(target.id)) {
            const auto caret = target.caret_rect(session->visible_caret());
            return Rect{caret.x, caret.y, 0, caret.h};
        }
        return Rect{target.bounds.x, target.bounds.y + target.field.editor.y, 0, target.field.editor.h};
    }
    bool has_builtin_menu() const override { return true; }
    void builtin_menu_entries(const UiState& ui, const ComponentConfig& c, bool clipboard_text, std::vector<UiState::MenuEntry>& raw) const override {
        const auto& field = UiState::Internals::edits(ui).at(c.id);
        const bool editable = text_editable(c, field);
        const bool selected = field.session.begin() != field.session.end();
        auto separator = [&] { UiState::MenuEntry entry; entry.separator = true; raw.push_back(std::move(entry)); };
        auto add = [&](const char* id, std::u16string label, char16_t key, std::u16string hint, bool enabled) {
            raw.push_back({id, std::move(label), std::move(hint), enabled, false, key});
        };
        add(menu_undo, u"復原(U)", u'U', u"Ctrl+Z", editable && !field.past.empty());
        add(menu_redo, u"重做(R)", u'R', u"Ctrl+Y", editable && !field.future.empty());
        separator();
        add(menu_cut, u"剪下(T)", u'T', u"Ctrl+X", editable && selected);
        add(menu_copy, u"複製(C)", u'C', u"Ctrl+C", selected);
        add(menu_paste, u"貼上(P)", u'P', u"Ctrl+V", editable && clipboard_text);
        add(menu_delete, u"刪除(D)", u'D', u"Del", editable && selected);
        separator();
        add(menu_select_all, u"全選(A)", u'A', u"Ctrl+A", true);
        if (field.rich) {  // Windows rich-edit style: the character styles follow the editing items. U is shared with 復原(U).
            separator();
            add(menu_bold, u"粗體(B)", u'B', u"Ctrl+B", editable);
            add(menu_italic, u"斜體(I)", u'I', u"Ctrl+I", editable);
            add(menu_underline, u"底線(U)", u'U', u"Ctrl+U", editable);
        }
    }
    Result run_builtin_menu(UiState& ui, const std::string& target, std::string_view command) const override {
        if (command == menu_undo) return ui.undo(target);
        if (command == menu_redo) return ui.redo(target);
        if (command == menu_cut) return ui.cut(target);
        if (command == menu_copy) return ui.copy(target);
        if (command == menu_paste) return ui.paste_clipboard(target);
        if (command == menu_delete) return UiState::Internals::delete_selection(ui, target);
        if (command == menu_bold) return ui.toggle_style(target, StyleAttribute::bold);
        if (command == menu_italic) return ui.toggle_style(target, StyleAttribute::italic);
        if (command == menu_underline) return ui.toggle_style(target, StyleAttribute::underline);
        if (command == menu_select_all) {
            auto& edits = UiState::Internals::edits(ui);
            auto it = edits.find(target);
            if (it == edits.end()) return {Status::invalid_value, u"元件不是文字欄位"};
            return ui.select(target, 0, static_cast<unsigned>(it->second.session.text.size()));
        }
        return {Status::not_found, u"沒有這個選單項目"};
    }
    std::optional<std::u16string> display_text(const UiState& ui, const ComponentConfig& c) const override {
        auto& edits = UiState::Internals::edits(ui);
        if (auto it = edits.find(c.id); it != edits.end()) return it->second.session.text;
        return std::nullopt;
    }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        s.multiline = c.multiline; s.visible_lines = c.multiline ? c.visible_lines : 0;
        auto& edits = UiState::Internals::edits(ui);
        auto it = edits.find(c.id);
        if (it == edits.end()) return;
        const auto& field = it->second;
        if (c.multiline) {
            const auto& m = field.multi;
            s.scroll_line = m.pending_first.value_or(m.first);
            s.line_count = m.placed ? static_cast<unsigned>(m.view.lines.size()) : 0;
        }
        s.source_conflict = field.connected && (field.origin != field.source_value || field.origin_spans != field.source_spans) && !UiState::Internals::source_matches(ui, field);
        s.rich = field.rich;
    }
    void describe_operations(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& edits = UiState::Internals::edits(ui);
        auto it = edits.find(c.id);
        if (it == edits.end()) return;
        if (c.multiline) s.operations.push_back(Operation::scroll);
        s.operations.insert(s.operations.end(), {Operation::select, Operation::copy});
        const auto& field = it->second;
        if (text_editable(c, field)) {
            s.operations.insert(s.operations.end(), {Operation::edit, Operation::cut, Operation::paste});
            if (field.rich) s.operations.push_back(Operation::style);
            if (!field.past.empty()) s.operations.push_back(Operation::undo);
            if (!field.future.empty()) s.operations.push_back(Operation::redo);
        }
    }
    void describe_node(const UiState& ui, const ComponentConfig& c, const Semantics& s, const NodeRef& ref, SemanticNode& n) const override {
        auto& edits = UiState::Internals::edits(ui);
        auto it = edits.find(c.id);
        if (it == edits.end()) return;
        const auto& field = it->second;
        n.text_selection = field.session.selection; n.text_revision = field.session.revision;
        if (field.rich) {
            n.rich = true; n.styles = field.spans;
            n.typing_style = UiState::Internals::insertion_style(ui, field, field.session.begin(), field.session.end());
        }
        if (s.multiline && !ref.item) n.scroll = TextScroll{s.scroll_line, s.visible_lines, s.line_count};
        if (!ref.item) {
            n.word_segmentation = field.segmentation;
            n.word_segmentation_in_use = segmentation_in_use(field.segmentation, UiState::Internals::dictionary(ui));
        }
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (c.item) return;
        auto add = [&](Verb verb, bool condition) { if (condition) out.push_back(verb); };
        add(Verb::set_value, c.admits(Operation::edit));
        add(Verb::select, c.admits(Operation::select));
        add(Verb::undo, c.admits(Operation::undo));
        add(Verb::redo, c.admits(Operation::redo));
        add(Verb::copy, c.admits(Operation::copy));
        add(Verb::cut, c.admits(Operation::cut));
        add(Verb::paste, c.admits(Operation::paste));
        add(Verb::scroll, c.semantics.multiline && c.admits(Operation::scroll));
        flow_verbs(c, out);
        add(Verb::set_style, c.admits(Operation::style));
        add(Verb::set_typing_style, c.admits(Operation::style));
        for (auto word_verb : {Verb::set_word_segmentation, Verb::select_word, Verb::select_paragraph, Verb::double_click, Verb::triple_click})
            add(word_verb, c.admits(Operation::select));
    }
    bool confirm_pending(const UiState& ui, const ComponentConfig& c) const override { return has_confirmed_write(UiState::Internals::edits(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        auto& ui = c.ui;
        if (auto result = flow_perform(c)) return result;
        if (c.verb == Verb::scroll) {
            if (!ui.find(c.ref.id)->multiline) return Result{Status::not_supported, {}};
            if (auto line = std::get_if<int>(&c.argument); line && *line >= 0) return ui.set_scroll_line(c.ref.id, static_cast<unsigned>(*line));
            return c.invalid_argument();
        }
        if (c.verb == Verb::select_word || c.verb == Verb::select_paragraph || c.verb == Verb::double_click || c.verb == Verb::triple_click) {
            auto offset = std::get_if<int>(&c.argument);
            if (!offset || *offset < 0) return c.invalid_argument();
            const auto at = static_cast<unsigned>(*offset);
            return c.verb == Verb::select_word ? ui.select_word(c.ref.id, at) : c.verb == Verb::select_paragraph ? ui.select_paragraph(c.ref.id, at)
                 : c.verb == Verb::double_click ? ui.double_click(c.ref.id, at) : ui.triple_click(c.ref.id, at);
        }
        if (c.verb == Verb::set_word_segmentation) {
            if (auto mode = std::get_if<WordSegmentation>(&c.argument)) return ui.set_word_segmentation(c.ref.id, *mode);
            return c.invalid_argument();
        }
        if (c.verb == Verb::set_value) {
            if (auto value = std::get_if<std::u16string>(&c.argument)) return ui.set_value(c.ref.id, *value);
            if (auto value = std::get_if<RichText>(&c.argument)) return ui.set_rich_value(c.ref.id, *value);
            return c.invalid_argument();
        }
        if (c.verb == Verb::set_style) {
            if (auto edit = std::get_if<StyleEdit>(&c.argument)) return ui.set_style(c.ref.id, edit->begin, edit->end, edit->change);
            return c.invalid_argument();
        }
        if (c.verb == Verb::set_typing_style) {
            if (auto style = std::get_if<TextStyle>(&c.argument)) return ui.set_typing_style(c.ref.id, *style);
            return c.invalid_argument();
        }
        if (c.verb == Verb::select) {
            if (auto selection = std::get_if<Selection>(&c.argument)) return ui.select(c.ref.id, selection->anchor, selection->caret);
            return c.invalid_argument();
        }
        if (c.verb == Verb::undo) return ui.undo(c.ref.id);
        if (c.verb == Verb::redo) return ui.redo(c.ref.id);
        if (c.verb == Verb::copy) return ui.copy(c.ref.id);
        if (c.verb == Verb::cut) return ui.cut(c.ref.id);
        if (c.verb == Verb::paste) {
            if (auto value = std::get_if<std::u16string>(&c.argument)) return ui.paste(c.ref.id, *value);
            return c.invalid_argument();
        }
        return std::nullopt;
    }
    std::string_view name() const override { return "text"; }
    Role role() const override { return Role::text; }
    bool supports_text_flow() const override { return true; }
    bool supports_rich_text() const override { return true; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<TextHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        const auto& control = c.control;
        int line = c.measure.line_height(p.text_size);
        auto semantics = *c.ui.describe(control.id);
        auto name = semantics.name;
        if (control.read_only) name += c.presentation().read_only_suffix;
        if (!control.enabled) name += c.presentation().disabled_suffix;
        int caption = p.caption_size = std::max(1, p.text_size * 7 / 8);
        const int ring = focus_ring_width(c.ui, c.appearance, c.scale);
        auto probe = c.appearance.arrange_field(c.width, {line, line, line, line}, c.scale, c.ui.presentation().density);
        keep_ring_room(probe, ring);
        auto label = block(c.measure, name, caption, probe.label.w);
        auto description_text = semantics.description;
        if (semantics.source_conflict) {
            if (!description_text.empty()) description_text += u"\n";
            description_text += c.presentation().conflict_suffix;
        }
        auto description = block(c.measure, description_text, caption, probe.description.w);
        auto error = block(c.measure, semantics.error, caption, probe.error.w);
        const int row = text_row(line, control.line_spacing);
        // A fixed number of lines: every row, plus room for a paragraph gap between each pair of them.
        const int editor_height = control.multiline ? row * static_cast<int>(control.visible_lines) + paragraph_gap(control, c.scale) * static_cast<int>(control.visible_lines - 1) : row;
        p.field = c.appearance.arrange_field(c.width, {height_of(label), height_of(description), height_of(error), editor_height}, c.scale, c.ui.presentation().density);
        keep_ring_room(p.field, ring);
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
        float room = static_cast<float>(p.field.editor.w - static_cast<int>(std::lround(c.presentation().inset * c.scale)));
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
        // A single line is the last line of its paragraph, so justify leaves it left-aligned (#84).
        const auto align = drawn_text_align(control, c.ui.presentation());
        if (p.run.width <= room && (align == TextAlign::center || align == TextAlign::right))
            p.shift = align == TextAlign::center ? std::max(0.f, (static_cast<float>(p.field.editor.w) - p.run.width) / 2) : room - p.run.width;
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
        const int slack = scaled(c.scale, c.presentation().inset);  // room for a caret at a line end
        const int wrap_width = std::max(1, view.text.w - slack);
        const auto visible_text = c.ui.edit(c.control.id)->visible();
        if (c.control.rich) {  // wrap and measure every run in its own face
            const auto styles = c.ui.visible_style_spans(c.control.id);
            view.lines = wrap_lines(c.measure, visible_text, p.text_size, wrap_width, &styles);
        } else view.lines = wrap_lines(c.measure, visible_text, p.text_size, wrap_width);
        const auto align = drawn_text_align(c.control, c.ui.presentation());
        // Rows: one per line, a paragraph gap after each hard line. Alignment: centered in the text area,
        // right-aligned to the wrap edge (which keeps the caret's slack free at the right).
        const int paragraph = paragraph_gap(c.control, c.scale);
        int top = 0;
        for (std::size_t i = 0; i < view.lines.size(); ++i) {
            auto& item = view.lines[i];
            item.top = top;
            top += row + (item.soft ? 0 : paragraph);
            if (align == TextAlign::center) item.shift = std::max(0.f, (static_cast<float>(view.text.w) - item.run.width) / 2);
            else if (align == TextAlign::right) item.shift = std::max(0.f, static_cast<float>(wrap_width) - item.run.width);
            else if (align == TextAlign::justify && item.soft) {
                // Every wrapped piece but the paragraph's last is stretched to the wrap edge; its stops carry the
                // added width, so the caret, hits, selection and the input method follow the drawn pieces. A rich
                // line is also cut at every style boundary (no extra there), so each piece lies inside one style run.
                if (auto stretched = justify_line(c.measure, std::u16string_view(visible_text).substr(item.begin, item.end - item.begin), p.text_size, item.run,
                                                  static_cast<float>(wrap_width), c.control.justify_method, item.pieces.empty() ? nullptr : &item.pieces)) {
                    item.run = std::move(stretched->run);
                    item.segments = std::move(stretched->segments);
                    for (auto& piece : item.pieces) {  // a style run is drawn where its first piece starts and reaches the end of its last one
                        const TextSegment* first = nullptr; const TextSegment* last = nullptr;
                        for (const auto& segment : item.segments)
                            if (segment.begin >= piece.begin && segment.end <= piece.end) { if (!first) first = &segment; last = &segment; }
                        if (first) { piece.x = first->x; piece.width = last->x + last->width - first->x; }
                    }
                }
            }
            for (auto& piece : item.pieces) piece.x += item.shift;  // rich runs are drawn where the aligned line is (a justified line has no shift)
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
        const int inset = d(c.presentation().inset);
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
            const Color base = enabled ? p.text : p.muted;
            if (line.end > line.begin && line.pieces.empty()) {
                if (line.segments.empty()) c.text(std::u16string_view(text).substr(line.begin, line.end - line.begin), item.text_size, x_at(line.begin), y + dy, base);
                else draw_segments(line.segments, std::u16string_view(text).substr(line.begin, line.end - line.begin), 0, line.end - line.begin,  // a justified line, piece by piece (#84)
                                   [&](std::u16string_view piece, float x) { c.text(piece, item.text_size, view.text.x + static_cast<int>(std::lround(line.shift + x)), y + dy, base); });
            }
            // A rich line is drawn run by run: its face, its color (by name, for the theme of the moment) and its underline.
            // piece.x already includes the line's alignment shift; a justified line is drawn segment by segment (#84),
            // every segment inside one style run, and the run's underline spans the added gaps.
            for (const auto& piece : line.pieces) {
                const int left = view.text.x + static_cast<int>(std::lround(piece.x));
                Color color = base;  // a disabled field is muted whatever the colors say
                if (enabled)
                    if (auto named = c.ui.named_color(item.id, piece.style.color)) color = *named;
                const FontStyle face{piece.style.bold, piece.style.italic};
                if (line.segments.empty())
                    c.styled_text(std::u16string_view(text).substr(line.begin + piece.begin, piece.end - piece.begin), item.text_size, face, left, y + dy, color);
                else draw_segments(line.segments, std::u16string_view(text).substr(line.begin, line.end - line.begin), piece.begin, piece.end,
                                   [&](std::u16string_view part, float x) { c.styled_text(part, item.text_size, face, view.text.x + static_cast<int>(std::lround(line.shift + x)), y + dy, color); });
                if (piece.style.underline) {
                    const int width = static_cast<int>(std::lround(piece.x + piece.width)) - static_cast<int>(std::lround(piece.x));
                    c.fill({left, y + dy + view.line_height * 5 / 6, width, std::max(1, item.text_size / 14)}, color);
                }
            }
        }
        if (focused && c.window_focused && enabled) {
            const unsigned caret_line = view.line_of(session->visible_caret());
            if (caret_line >= view.first && caret_line < end_line)
                marked(c, PaintRole::caret, [&] { c.fill({item.caret_x(session->visible_caret()), item.caret_y(session->visible_caret()), d(c.presentation().thin) + 1, view.row_height}, p.accent); });
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
        const int radius = d(c.appearance.radius()), thin = d(c.presentation().thin);
        const Color fill = enabled ? p.surface : p.background;
        // The edge identifies the field: outline when usable, the light divider color when disabled, error when invalid.
        const Color edge = semantics.invalid ? p.error : enabled ? p.outline : p.border;
        const PaintRole error = semantics.invalid ? PaintRole::error_indicator : PaintRole::content;
        if (cue) {  // the focus ring replaces the edge, or the underline of the underline style
            const auto shape = c.focus_ring(surface, radius, fill, semantics.invalid);
            if (c.hovered()) c.hover(shape.inner, shape.inner_radius);
        } else if (!f.underline) {
            const auto shape = c.frame(surface, radius, thin, edge, fill, error);
            if (c.hovered()) c.hover(shape.inner, shape.inner_radius);
        } else {
            c.rounded(surface, radius, fill);
            if (c.hovered()) c.hover(surface, radius);
            marked(c, error, [&] { c.fill({surface.x, surface.y + surface.h - 2 * thin, surface.w, 2 * thin}, edge); });
        }
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
                if (session->composing) marked(c, PaintRole::composition, [&] { c.fill({at(first), editor.y + editor.h - item.text_top - d(c.presentation().inset), at(last) - at(first), d(c.presentation().inset)}, p.accent); });
                else marked(c, PaintRole::selection, [&] { c.fill({at(first), editor.y, at(last) - at(first), editor.h}, p.selection); });
            }
        }
        c.text(session->visible(), item.text_size, item.caret_x(0), editor.y + item.text_top, enabled ? p.text : p.muted);
        if (focused && c.window_focused && enabled) marked(c, PaintRole::caret, [&] { c.fill({at(session->visible_caret()), editor.y, d(c.presentation().thin) + 1, editor.h}, p.accent); });
    }
    bool selects_text() const override { return true; }
    bool handles_key(const UiState&, const ComponentConfig& c, KeyPress key) const override {
        // Multi-line fields also take Enter and the vertical keys; Ctrl+Enter is left to shortcuts.
        const bool multi = c.multiline;
        // A rich field also takes Ctrl+B / I / U before any application shortcut on them.
        const bool style_key = c.rich && (key.letter == u'B' || key.letter == u'I' || key.letter == u'U');
        return key.ctrl ? (key.letter == u'A' || key.letter == u'C' || key.letter == u'X' || key.letter == u'V' || key.letter == u'Z' || key.letter == u'Y' || style_key ||
                           (!key.letter && (key.key == Key::left || key.key == Key::right || key.key == Key::backspace || key.key == Key::del)) ||
                           (multi && !key.letter && (key.key == Key::home || key.key == Key::end))) :
            (key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end || key.key == Key::backspace || key.key == Key::del ||
             (multi && (key.key == Key::enter || key.key == Key::up || key.key == Key::down || key.key == Key::page_up || key.key == Key::page_down)));
    }
    Result key_down(UiState& ui, KeyPress key) const override { return UiState::Internals::edit_key(ui, key); }
};
// Legacy toggle/navigation use the button presentation, but have no button activation behavior.
class Command : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled}); }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<ButtonHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        int line = c.measure.line_height(p.text_size);
        int w = std::min(c.width, command_width(c));
        int room = std::max(1, w - 2 * command_padding(c));
        auto lines = wrap(c.measure, command_title(c), p.text_size, room);
        int text_height = static_cast<int>(lines.size()) * line;
        int h = command_height(c.ui, c.appearance, c.scale, text_height, std::max(text_height, indicator_px(c.ui, c.appearance, c.scale)));
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
        const int radius = d(c.appearance.radius());
        const Color fill = enabled ? p.surface : p.background;
        const Rect box = command_box(c, indicator_px(c.ui, c.appearance, c.scale));
        const auto shape = focused ? c.focus_ring(box, radius, fill, c.semantics.invalid)
                                   : c.frame(box, radius, d(c.presentation().thin), enabled ? p.border : p.background, fill);
        if (c.hovered()) c.hover(shape.inner, shape.inner_radius);
        paint_lines(c, item.lines, item.text_size, enabled ? p.text : p.muted);
    }
    int natural_width(const ComponentLayout& c) const override { return command_width(c); }
protected:
    // What the box must hold besides its title: the checkbox's square.
    virtual int indicator_px(const UiState&, const Appearance&, float) const { return 0; }
};
class Button final : public Command {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::executing}); }
    void describe_operations(const UiState&, const ComponentConfig&, Semantics& s) const override { if (!s.executing) s.operations.push_back(Operation::activate); }
    void describe_node(const UiState& ui, const ComponentConfig& c, const Semantics&, const NodeRef& ref, SemanticNode& n) const override {
        if (ref.item) return;
        for (const auto& binding : UiState::Internals::shortcuts(ui)) {
            if (binding.target != c.id) continue;
            const auto& chord = binding.chord;
            if (chord.ctrl) n.accelerator_key += u"Ctrl+";
            if (chord.shift) n.accelerator_key += u"Shift+";
            if (chord.letter) n.accelerator_key += chord.letter;
            else {
                switch (chord.key) {
                case Key::enter: n.accelerator_key += u"Enter"; break;
                case Key::space: n.accelerator_key += u"Space"; break;
                case Key::left: n.accelerator_key += u"Left"; break;
                case Key::right: n.accelerator_key += u"Right"; break;
                case Key::up: n.accelerator_key += u"Up"; break;
                case Key::down: n.accelerator_key += u"Down"; break;
                case Key::home: n.accelerator_key += u"Home"; break;
                case Key::end: n.accelerator_key += u"End"; break;
                case Key::page_up: n.accelerator_key += u"PageUp"; break;
                case Key::page_down: n.accelerator_key += u"PageDown"; break;
                case Key::backspace: n.accelerator_key += u"Backspace"; break;
                case Key::del: n.accelerator_key += u"Delete"; break;
                default: break;
                }
            }
            break;
        }
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (!c.item && c.ui.has_action(c.control.id) && c.admits(Operation::activate)) out.push_back(Verb::invoke);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.verb == Verb::invoke) return c.ui.invoke(c.ref.id);
        return std::nullopt;
    }
    std::string_view name() const override { return "button"; }
    Role role() const override { return Role::button; }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override { return plain_key(key) && (key.key == Key::enter || key.key == Key::space); }
    Result key_down(UiState& ui, KeyPress key) const override { return command_key_down(ui, key); }
    void pointer_down(UiState& ui, const ComponentConfig&, std::optional<std::size_t>) const override { command_pointer_down(ui); }
};
class Checkbox final : public Command {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::error, S::conflict}); }
    std::optional<std::u16string> display_text(const UiState& ui, const ComponentConfig& c) const override {
        auto& bools = UiState::Internals::bools(ui);
        if (auto it = bools.find(c.id); it != bools.end()) return std::u16string(it->second.value ? u"true" : u"false");
        return std::nullopt;
    }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& bools = UiState::Internals::bools(ui);
        if (auto it = bools.find(c.id); it != bools.end()) {
            s.checked = it->second.value;
            s.source_conflict = diverged(it->second.connected, it->second.origin, it->second.source_value, it->second.value);
        }
    }
    void describe_operations(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& bools = UiState::Internals::bools(ui);
        auto it = bools.find(c.id);
        if (!s.executing && (it == bools.end() || it->second.writable())) s.operations.push_back(Operation::activate);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (!c.item && c.admits(Operation::activate)) out.push_back(Verb::toggle);
    }
    bool confirm_pending(const UiState& ui, const ComponentConfig& c) const override { return has_confirmed_write(UiState::Internals::bools(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.verb == Verb::toggle) return std::get<CheckboxHandle>(c.ui.layout_handle(c.ref.id)).toggle();
        return std::nullopt;
    }
    std::string_view name() const override { return "checkbox"; }
    Role role() const override { return Role::checkbox; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<CheckboxHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        Command::arrange(c);
        int extra = scaled(c.scale, c.presentation().checkbox_extra);
        c.placement.bounds.w = std::min(c.width, c.placement.bounds.w + extra);
        for (auto& line : c.placement.lines) line.bounds.x += extra;
        auto semantics = *c.ui.describe(c.control.id);
        c.placement.bounds.h = conflict_lines(c, c.placement.bounds.h, semantics.source_conflict);
        c.placement.bounds.h = error_lines(c, c.placement.bounds.h, semantics);
    }
    void paint(ComponentPaint& c) const override {
        Command::paint(c);
        paint_lines(c, c.placement.description_lines, c.placement.text_size, c.palette().muted);
        paint_errors(c, c.placement.error_lines, c.placement.text_size, c.palette().error);
        auto p = c.palette();
        int size = scaled(c.scale, c.presentation().indicator);
        int x = c.placement.bounds.x + std::max(scaled(c.scale, c.presentation().gap), focus_ring_width(c.ui, c.appearance, c.scale));  // never under the ring
        int y = c.placement.bounds.y + (command_box(c, size).h - size) / 2;

        c.fill({x, y, size, size}, c.semantics.enabled ? p.outline : p.border);
        int inset = scaled(c.scale, c.presentation().inset);
        c.fill({x + inset, y + inset, size - inset * 2, size - inset * 2}, p.surface);
        if (c.semantics.checked.value_or(false))
            c.fill({x + 2 * inset, y + 2 * inset, size - 4 * inset, size - 4 * inset}, p.accent);
    }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override { return plain_key(key) && (key.key == Key::enter || key.key == Key::space); }
    Result key_down(UiState& ui, KeyPress key) const override { return key.key == Key::space ? command_key_down(ui, key) : Result{}; }
    void pointer_down(UiState& ui, const ComponentConfig&, std::optional<std::size_t>) const override { command_pointer_down(ui); }
protected:
    int indicator_px(const UiState& ui, const Appearance& appearance, float scale) const override {
        return scaled(scale, appearance.component_presentation(ui.presentation().density).indicator);
    }
};
class RadioGroup final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::error, S::conflict}); }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& radios = UiState::Internals::radios(ui);
        auto it = radios.find(c.id);
        if (it == radios.end()) return;
        s.source_conflict = diverged(it->second.connected, it->second.origin, it->second.source_value, it->second.value);
        for (const auto& option : it->second.options) {
            bool selected = it->second.value && *it->second.value == option.value;
            s.options.push_back({option.value, option.label, option.enabled, selected});
            if (selected) s.value = option.label;
        }
    }
    void describe_operations(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& radios = UiState::Internals::radios(ui);
        auto it = radios.find(c.id);
        if (it == radios.end()) return;
        const auto& f = it->second;
        if (f.writable() && std::any_of(f.options.begin(), f.options.end(), [](const auto& option) { return option.enabled; }))
            s.operations.push_back(Operation::select);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (c.item) { if (c.admits(Operation::select)) out.push_back(Verb::select); }
        else ComponentType::verbs(c, out);
    }
    bool confirm_pending(const UiState& ui, const ComponentConfig& c) const override { return has_confirmed_write(UiState::Internals::radios(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.ref.item && c.verb == Verb::select) return std::get<RadioGroupHandle>(c.ui.layout_handle(c.ref.id)).select(*c.ref.item);
        return std::nullopt;
    }
    std::string_view name() const override { return "radio_group"; }
    Role role() const override { return Role::radio_group; }
    std::optional<Role> item_role() const override { return Role::radio_option; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<RadioGroupHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        // The ring frames one option row: the row is tall enough for the label and the circle plus a ring above and below,
        // and both keep a ring's thickness from the row's edges.
        const int ring = focus_ring_width(c.ui, c.appearance, c.scale), size = scaled(c.scale, c.presentation().indicator);
        int line = c.measure.line_height(p.text_size), row = std::max({scaled(c.scale, 24), line + scaled(c.scale, c.presentation().gap), line + 2 * ring, size + 2 * ring});
        const int circle_x = std::max(scaled(c.scale, c.presentation().gap_small), ring);
        const int label_x = std::max(scaled(c.scale, c.presentation().radio_label_offset), circle_x + size + scaled(c.scale, c.presentation().gap));
        p.lines.push_back({{0, 0, c.width, line}, semantics.name + (semantics.enabled ? u"" : c.presentation().disabled_suffix)});
        int y = line + scaled(c.scale, c.presentation().gap_small);
        for (const auto& option : semantics.options) {
            p.options.push_back({0, y, c.width, row});
            p.lines.push_back({{label_x, y + (row - line) / 2, std::max(0, c.width - label_x - ring), line}, option.label + (option.enabled ? u"" : c.presentation().disabled_suffix)});
            y += row;
        }
        y = conflict_lines(c, y, semantics.source_conflict);
        y = error_lines(c, y, semantics);
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto p = c.palette();
        const auto& item = c.placement;
        if (!item.lines.empty()) c.text(item.lines[0].text, item.text_size, item.lines[0].bounds.x, item.lines[0].bounds.y, c.semantics.enabled ? p.text : p.muted);
        // The ring frames the selected option's row, or the first usable one while nothing is selected.
        std::optional<std::size_t> framed;
        if (c.focus_visible) {
            for (std::size_t i = 0; i < c.semantics.options.size() && i < item.options.size(); ++i)
                if (c.semantics.options[i].selected) { framed = i; break; }
            for (std::size_t i = 0; !framed && i < c.semantics.options.size() && i < item.options.size(); ++i)
                if (c.semantics.options[i].enabled) framed = i;
        }
        for (std::size_t i = 0; i < item.options.size(); ++i) {
            const auto& r = item.options[i]; const auto& option = c.semantics.options[i];
            int size = scaled(c.scale, c.presentation().indicator), inset = scaled(c.scale, c.presentation().focus);
            int x = r.x + std::max(scaled(c.scale, c.presentation().gap_small), focus_ring_width(c.ui, c.appearance, c.scale)), y = r.y + (r.h - size) / 2;
            Rect shown = r; int shown_radius = 0;  // what hover covers: the row, or what the ring leaves of it
            if (framed == i) {
                const auto shape = c.focus_ring(r, scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
                shown = shape.inner; shown_radius = shape.inner_radius;
            }
            if (c.hovered(option.value)) c.hover(shown, shown_radius);
            auto color = option.enabled && c.semantics.enabled ? p.outline : p.border;
            c.rounded({x, y, size, size}, size / 2, color);
            c.rounded({x + inset, y + inset, size - 2 * inset, size - 2 * inset}, size / 2, option.selected ? p.accent : p.surface);
            const auto& label = item.lines[i + 1];
            c.text(label.text, item.text_size, label.bounds.x, label.bounds.y, option.enabled && c.semantics.enabled ? p.text : p.muted);
        }
        paint_lines(c, item.description_lines, item.text_size, p.muted);
        paint_errors(c, item.error_lines, item.text_size, p.error);
    }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override { return plain_key(key) && arrow_key(key.key); }
    Result key_down(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        if (key.repeat) return {};
        int direction = key.key == Key::right || key.key == Key::down ? 1 : key.key == Key::left || key.key == Key::up ? -1 : 0;
        if (!direction) return {};
        auto& field = UiState::Internals::radios(ui).at(ui.focused());
        int n = static_cast<int>(field.options.size()), index = direction > 0 ? -1 : n;
        for (int i = 0; i < n; ++i) if (field.value && field.options[i].value == *field.value) { index = i; break; }
        for (int i = index + direction; i >= 0 && i < n; i += direction)
            if (field.options[i].enabled) return UiState::Internals::select_option(ui, ui.focused(), field.options[i].value);
        return {};
    }
    void pointer_down(UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override { ui.arm_pointer(c.id, option); }
    Result pointer_up(UiState& ui, const PointerRelease& release) const override {
        auto& field = UiState::Internals::radios(ui).at(release.id);
        if (!release.pressed_option || release.pressed_option != release.option || *release.option >= field.options.size()) return {};
        return UiState::Internals::select_option(ui, release.id, field.options[*release.option].value);
    }
};
class List final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::candidate}); }
    bool has_row_cursor() const override { return true; }
    // A list row's own anchor for a keyboard-opened menu.
    std::optional<Rect> keyboard_anchor(UiState& state, const Placement& target, const std::optional<OptionValue>& item, int row_inset) const override {
        if (!item) return std::nullopt;
        auto semantic = state.describe(target.id);
        if (semantic) for (std::size_t i = 0; i < semantic->options.size() && i < target.options.size(); ++i)
            if (semantic->options[i].value == *item) {
                const auto& row = target.options[i];
                return Rect{row.x + row_inset, row.y, 0, row.h};
            }
        return std::nullopt;
    }
    // A menu is opened for a row; from the keyboard that is the current one.
    std::optional<OptionValue> menu_cursor(const UiState& ui, const ComponentConfig& c) const override { return UiState::Internals::lists(ui).at(c.id).cursor; }
    Result check_menu_item(const UiState& ui, const ComponentConfig& c, const OptionValue& item) const override {
        const auto& items = UiState::Internals::lists(ui).at(c.id).items;
        auto row = std::find_if(items.begin(), items.end(), [&](const auto& r) { return r.value == item; });
        if (row == items.end()) return {Status::not_found, u"沒有這個項目"};
        if (!row->enabled) return {Status::disabled, u"項目已停用"};
        return {};
    }
    void menu_row_chosen(UiState& ui, const std::string& id, const OptionValue& item) const override {
        (void)std::get<ListHandle>(ui.layout_handle(id)).select(item);
        auto& lists = UiState::Internals::lists(ui);
        if (auto list = lists.find(id); list != lists.end()) list->second.cursor = item;
    }
    std::optional<Query<OptionValue>> menu_row(const UiState& ui, const ComponentConfig& c, std::size_t option) const override {
        const auto& items = UiState::Internals::lists(ui).at(c.id).items;
        if (option >= items.size()) return std::nullopt;
        if (!items[option].enabled) return Query<OptionValue>{Status::disabled, u"項目已停用", std::nullopt};
        return Query<OptionValue>{Status::ok, {}, items[option].value};
    }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& lists = UiState::Internals::lists(ui);
        auto it = lists.find(c.id);
        if (it == lists.end()) return;
        const auto& f = it->second; auto value = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        s.candidate = f.cursor;
        for (const auto& item : f.items) {
            bool selected = value && *value == item.value;
            s.options.push_back({item.value, item.label, item.enabled, selected});
            if (selected) s.value = item.label;
        }
    }
    void describe_operations(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& lists = UiState::Internals::lists(ui);
        auto it = lists.find(c.id);
        if (it == lists.end()) return;
        const auto& f = it->second;
        if (!c.read_only && f.source.read && f.immediate &&
            std::any_of(f.items.begin(), f.items.end(), [](const auto& item) { return item.enabled; })) s.operations.push_back(Operation::select);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { ComponentType::verbs(c, out); return; }
        if (c.admits(Operation::select)) out.push_back(Verb::select);
        if (c.admits(Operation::focus)) out.push_back(Verb::focus);
        if (c.ui.has_menu(c.control) && !c.ui.renderer_failed()) out.push_back(Verb::context_menu);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (!c.ref.item) return std::nullopt;
        auto handle = c.ui.layout_handle(c.ref.id);
        auto& list = std::get<ListHandle>(handle);
        if (c.verb == Verb::focus) return list.focus(*c.ref.item);
        if (c.verb != Verb::select) return std::nullopt;
        // A semantic selection does not move the entry cursor.
        UiState::OperationScope operation(&c.ui);
        auto& lists = UiState::Internals::lists(c.ui);
        auto previous = lists.at(c.ref.id).cursor;
        auto result = list.select(*c.ref.item);
        if (auto it = lists.find(c.ref.id); it != lists.end()) it->second.cursor = previous;
        return result;
    }
    std::string_view name() const override { return "list"; }
    Role role() const override { return Role::list; }
    std::optional<Role> item_role() const override { return Role::list_item; }
    bool requires_typed_creation() const override { return true; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<ListHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        // A row is tall enough for its ring, and its text starts a ring's thickness from the edge, focused or not.
        const int ring = focus_ring_width(c.ui, c.appearance, c.scale), text_x = std::max(scaled(c.scale, c.presentation().gap), ring);
        int line = c.measure.line_height(p.text_size), row = std::max(line + scaled(c.scale, c.presentation().row_padding), line + 2 * ring);
        p.label_lines = block(c.measure, s.name + (s.enabled ? u"" : c.presentation().disabled_suffix), p.text_size, c.width);
        int y = height_of(p.label_lines);
        if (y) y += scaled(c.scale, c.presentation().gap_small);
        if (s.options.empty()) {
            p.lines.push_back({{text_x, y, c.width, line}, c.presentation().no_items});
            y += row;
        }
        for (const auto& item : s.options) {
            p.options.push_back({0, y, c.width, row});
            auto text = (item.selected ? c.presentation().selected_marker : c.presentation().unselected_marker) + item.label;
            if (!item.enabled) text += c.presentation().disabled_suffix;
            p.lines.push_back({{text_x, y + (row - line) / 2, room_after(c.width, scaled(c.scale, c.presentation().body_spacing), text_x, scaled(c.scale, c.presentation().gap)), line}, std::move(text)});
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
            // The ring frames the candidate row; the selected row keeps its selection color inside it.
            Rect shown = row; int shown_radius = 0;
            if (c.focus_visible && c.semantics.candidate == item.value) {
                const auto shape = c.focus_ring(row, scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
                shown = shape.inner; shown_radius = shape.inner_radius;
            }
            if (item.selected) marked(c, PaintRole::selection, [&] { if (shown_radius > 0) c.rounded(shown, shown_radius, palette.selection); else c.fill(shown, palette.selection); });
            if (c.hovered(item.value)) c.hover(shown, shown_radius);
            const auto& line = p.lines[i];
            c.text(line.text, p.text_size, line.bounds.x, line.bounds.y, item.enabled && c.semantics.enabled ? palette.text : palette.muted);
        }
    }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override { return plain_key(key) && (key.key == Key::space || arrow_key(key.key)); }
    Result key_down(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        auto& f = UiState::Internals::lists(ui).at(ui.focused());
        auto handle = ui.layout_handle(ui.focused());
        auto& list = std::get<ListHandle>(handle);
        if (key.key == Key::space) return f.cursor ? list.select(*f.cursor) : Result{};
        if (!arrow_key(key.key)) return {};
        int n = static_cast<int>(f.items.size());
        int current = -1;
        for (int i = 0; i < n; ++i) if (f.cursor && f.items[i].value == *f.cursor) { current = i; break; }
        int step = key.key == Key::up || key.key == Key::left || key.key == Key::end ? -1 : 1;
        int start = key.key == Key::home ? 0 : key.key == Key::end ? n - 1 : current < 0 ? (step > 0 ? 0 : n - 1) : current + step;
        for (int i = start; i >= 0 && i < n; i += step) if (f.items[i].enabled) return list.select(f.items[i].value);
        return {};
    }
    void pointer_down(UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override {
        const auto& items = UiState::Internals::lists(ui).at(c.id).items;
        ui.arm_pointer(c.id, option, option && *option < items.size() ? std::optional<OptionValue>(items[*option].value) : std::nullopt);
    }
    Result pointer_up(UiState& ui, const PointerRelease& release) const override {
        const auto& items = UiState::Internals::lists(ui).at(release.id).items;
        if (!release.pressed_value || !release.option || *release.option >= items.size() || items[*release.option].value != *release.pressed_value) return {};
        return std::get<ListHandle>(ui.layout_handle(release.id)).select(*release.pressed_value);
    }
};
class Dropdown final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::candidate, S::error, S::conflict}); }
    bool opens_popup() const override { return true; }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& dropdowns = UiState::Internals::dropdowns(ui);
        auto it = dropdowns.find(c.id);
        if (it == dropdowns.end()) return;
        const auto& f = it->second;
        s.source_conflict = diverged(f.connected, f.origin, f.source_value, f.value);
        s.expanded = ui.popup_open(c.id); s.candidate = f.candidate;
        for (const auto& option : f.options) {
            bool selected = f.value && *f.value == option.value;
            s.options.push_back({option.value, option.label, option.enabled, selected});
            if (selected) s.value = option.label;
        }
    }
    void describe_operations(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& dropdowns = UiState::Internals::dropdowns(ui);
        auto it = dropdowns.find(c.id);
        if (it != dropdowns.end() && it->second.writable()) s.operations.push_back(Operation::select);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (c.item) { if (c.admits(Operation::select)) out.push_back(Verb::select); return; }
        ComponentType::verbs(c, out);
        if (c.admits(Operation::select)) { out.push_back(Verb::open); out.push_back(Verb::cancel); }
    }
    bool confirm_pending(const UiState& ui, const ComponentConfig& c) const override { return has_confirmed_write(UiState::Internals::dropdowns(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        auto handle = c.ui.layout_handle(c.ref.id);
        auto& dropdown = std::get<DropdownHandle>(handle);
        if (c.ref.item && c.verb == Verb::select) return dropdown.select(*c.ref.item);
        if (!c.ref.item && c.verb == Verb::open) return dropdown.open();
        if (!c.ref.item && c.verb == Verb::cancel) return dropdown.cancel();
        return std::nullopt;
    }
    std::string_view name() const override { return "dropdown"; }
    Role role() const override { return Role::dropdown; }
    std::optional<Role> item_role() const override { return Role::dropdown_option; }
    bool requires_typed_creation() const override { return true; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<DropdownHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        // The box (and its ring) frames the name, the value row and any error text: all of it keeps a ring's thickness
        // from the edge, focused or not.
        const int ring = focus_ring_width(c.ui, c.appearance, c.scale);
        int line = c.measure.line_height(p.text_size), row = std::max(c.appearance.button_height(line, c.scale, c.ui.presentation().density), line + 2 * ring);
        p.lines.push_back({{ring, ring, std::max(0, c.width - 2 * ring), line}, s.name + (s.enabled ? u"" : c.presentation().disabled_suffix)});
        const int value_x = std::max(scaled(c.scale, c.presentation().gap), ring);
        p.lines.push_back({{value_x, ring + line + (row - line) / 2, room_after(c.width, scaled(c.scale, c.presentation().body_spacing), value_x, scaled(c.scale, c.presentation().gap)), line}, s.value.empty() ? c.presentation().no_selection : s.value});
        int y = ring + line + row;
        y = conflict_lines(c, y, s.source_conflict, ring);
        y = error_lines(c, y, s, ring);
        if (y > ring + line + row) y += ring;  // text under the value row also keeps clear of the bottom edge
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
                if (c.hovered(o.value)) c.hover(r);
                const auto& line = c.placement.lines[i];
                c.text(o.enabled ? line.text : line.text + c.presentation().disabled_suffix, c.placement.text_size, line.bounds.x, line.bounds.y, o.enabled ? palette.text : palette.muted);
            }
            return;
        }
        const int radius = scaled(c.scale, c.appearance.radius());
        const auto shape = c.focus_visible ? c.focus_ring(bounds, radius, palette.surface, c.semantics.invalid)
            : c.frame(bounds, radius, scaled(c.scale, c.presentation().inset), c.semantics.invalid ? palette.error : c.semantics.enabled ? palette.outline : palette.border, palette.surface,
                      c.semantics.invalid ? PaintRole::error_indicator : PaintRole::content);
        if (c.hovered()) c.hover(shape.inner, shape.inner_radius);
        paint_lines(c, c.placement.lines, c.placement.text_size, c.semantics.enabled ? palette.text : palette.muted);
        if (c.placement.lines.size() > 1) {
            const auto& line = c.placement.lines[1].bounds;
            // The marker's room is never less than a ring plus a glyph (at most one em wide), so it stays inside the ring.
            const int marker_room = std::max(scaled(c.scale, c.presentation().dropdown_marker_space), focus_ring_width(c.ui, c.appearance, c.scale) + c.placement.text_size);
            c.text(c.presentation().dropdown_marker, c.placement.text_size, bounds.x + std::max(0, bounds.w - marker_room), line.y,
                   c.semantics.enabled ? palette.text : palette.muted);
        }
        paint_lines(c, c.placement.description_lines, c.placement.text_size, palette.muted);
        paint_errors(c, c.placement.error_lines, c.placement.text_size, palette.error);
    }
    bool handles_key(const UiState& ui, const ComponentConfig& c, KeyPress key) const override {
        return plain_key(key) && (key.key == Key::enter || key.key == Key::space || (ui.popup_open(c.id) && (arrow_key(key.key) || key.key == Key::escape)));
    }
    Result key_down(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        const Id& focus = ui.focused();
        auto& f = UiState::Internals::dropdowns(ui).at(focus);
        if (key.key == Key::escape) { ui.close_popup(); return {}; }
        if (key.key == Key::enter || key.key == Key::space) {
            if (key.repeat) return {};
            if (!ui.popup_open(focus)) return std::get<DropdownHandle>(ui.layout_handle(focus)).open();
            auto candidate = f.candidate; ui.close_popup(); return candidate ? UiState::Internals::select_dropdown(ui, focus, *candidate) : Result{};
        }
        if (!ui.popup_open(focus)) return {};
        int direction = key.key == Key::down || key.key == Key::right ? 1 : key.key == Key::up || key.key == Key::left ? -1 : 0;
        if (key.key == Key::home) direction = 1;
        if (key.key == Key::end) direction = -1;
        if (!direction) return {};
        int n = static_cast<int>(f.options.size()), index = direction > 0 ? -1 : n;
        if (key.key != Key::home && key.key != Key::end) for (int i = 0; i < n; ++i) if (f.candidate && f.options[i].value == *f.candidate) { index = i; break; }
        f.reveal = true;
        for (int i = index + direction; i >= 0 && i < n; i += direction) if (f.options[i].enabled) { f.candidate = f.options[i].value; break; }
        ui.invalidate(); return {};
    }
    void pointer_down(UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override {
        const auto& f = UiState::Internals::dropdowns(ui).at(c.id);
        ui.arm_pointer(c.id, option, ui.popup_open(c.id) && option && *option < f.options.size() ? std::optional<OptionValue>(f.options[*option].value) : std::nullopt);
    }
    Result pointer_up(UiState& ui, const PointerRelease& release) const override {
        const auto& f = UiState::Internals::dropdowns(ui).at(release.id);
        if (release.pressed_value) {
            if (!ui.popup_open(release.id) || !release.option || *release.option >= f.options.size() || f.options[*release.option].value != *release.pressed_value) return {};
            ui.close_popup(); return UiState::Internals::select_dropdown(ui, release.id, *release.pressed_value);
        }
        if (release.pressed_option || release.option) return {};
        return std::get<DropdownHandle>(ui.layout_handle(release.id)).open();
    }
};
class Slider final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::error, S::conflict}); }
    std::optional<std::u16string> display_text(const UiState& ui, const ComponentConfig& c) const override {
        auto& numerics = UiState::Internals::numerics(ui);
        if (auto it = numerics.find(c.id); it != numerics.end()) return number_text(it->second.value);
        return std::nullopt;
    }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& numerics = UiState::Internals::numerics(ui);
        auto it = numerics.find(c.id);
        if (it == numerics.end()) return;
        s.source_conflict = diverged(it->second.connected, it->second.origin, it->second.source_value, it->second.value);
        s.slider_range = it->second.range; s.numeric_value = it->second.value;
    }
    void describe_operations(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& numerics = UiState::Internals::numerics(ui);
        auto it = numerics.find(c.id);
        if (it != numerics.end() && !c.read_only && it->second.writable()) s.operations.push_back(Operation::edit);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (!c.item && c.admits(Operation::edit)) out.push_back(Verb::set_value);
    }
    bool confirm_pending(const UiState& ui, const ComponentConfig& c) const override { return has_confirmed_write(UiState::Internals::numerics(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.verb == Verb::set_value) {
            if (auto value = std::get_if<int>(&c.argument)) return std::get<SliderHandle>(c.ui.layout_handle(c.ref.id)).set_value(*value);
            return c.invalid_argument();
        }
        return std::nullopt;
    }
    std::string_view name() const override { return "slider"; }
    Role role() const override { return Role::slider; }
    bool requires_typed_creation() const override { return true; }
    bool begin_value_drag(UiState& ui, const Placement& p, int x, float scale) const override { ui.slider_pointer_down(p.id, x, p.slider_track, scale); return true; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<SliderHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        // The ring frames the title and the track, so both keep a ring's thickness from its edge; the track also leaves room
        // for the enlarged thumb at its ends. All of it is reserved whether or not the slider is focused.
        const int ring = focus_ring_width(c.ui, c.appearance, c.scale);
        const int thumb = scaled(c.scale, c.appearance.component_presentation().body_spacing);
        int line = c.measure.line_height(p.text_size), track_height = scaled(c.scale, c.presentation().slider_height);
        const int pad = std::max(scaled(c.scale, c.presentation().slider_pad), ring + (thumb + 1) / 2);
        auto title = semantics.name + u": " + semantics.value + (semantics.enabled ? u"" : c.presentation().disabled_suffix);
        if (semantics.source_conflict) title += c.presentation().conflict_suffix;
        p.lines = block(c.measure, title, p.text_size, std::max(1, c.width - 2 * ring));
        for (auto& l : p.lines) { l.bounds.x += ring; l.bounds.y += ring; }
        int y = ring + height_of(p.lines) + scaled(c.scale, c.presentation().gap_small);
        p.slider_track = {pad, y, std::max(1, c.width - 2 * pad), track_height};
        y += track_height + ring;
        y = error_lines(c, y, semantics);
        p.bounds = {0, 0, c.width, std::max(y, line)};
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement; auto palette = c.palette();
        if (c.focus_visible) {  // the ring frames the title and the track; error text stays outside it
            const int ring = focus_ring_width(c.ui, c.appearance, c.scale);
            c.focus_ring({item.bounds.x, item.bounds.y, item.bounds.w, item.slider_track.y + item.slider_track.h + ring - item.bounds.y},
                         scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
        }
        paint_lines(c, item.lines, item.text_size, c.semantics.enabled ? palette.text : palette.muted);
        paint_errors(c, item.error_lines, item.text_size, palette.error);
        auto track = item.slider_track; int thickness = scaled(c.scale, c.presentation().focus);
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
            Rect thumb{x - size / 2, track.y + (track.h - size) / 2, size, size};
            c.rounded(thumb, size / 2, c.semantics.enabled ? palette.accent : palette.muted);
            if (c.hovered()) c.hover(thumb, size / 2);  // anywhere on the slider marks the thumb
        }
    }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override { return plain_key(key) && arrow_key(key.key); }
    Result key_down(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        const Id id = ui.focused();
        auto& f = UiState::Internals::numerics(ui).at(id);
        const long long lo = f.range.minimum, hi = f.range.maximum, step = f.range.step, value = f.value;
        long long target = value;
        switch (key.key) {
        case Key::home: target = lo; break;
        case Key::end: target = hi; break;
        case Key::right: case Key::up:
            if (value < hi) target = std::min(hi, lo + ((value - lo) / step + 1) * step);
            break;
        case Key::left: case Key::down:
            if (value > lo) target = lo + ((value - lo - 1) / step) * step;
            break;
        default: return {};
        }
        const double from = UiState::Internals::slider_display_ratio(ui, id);
        auto result = ui.slider_set_value(id, static_cast<int>(target));
        UiState::Internals::slider_started_moving(ui, id, from, static_cast<int>(value));
        return result;
    }
    void pointer_down(UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override { ui.arm_pointer(c.id, option); }
    Result pointer_up(UiState&, const PointerRelease&) const override { return {}; }
};
class Progress final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal}); }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override {
        auto& progresses = UiState::Internals::progresses(ui);
        auto it = progresses.find(c.id);
        if (it == progresses.end()) return;
        s.read_only = true;
        if (it->second.source.read) {
            int displayed = std::clamp(it->second.source.read(), 0, 100);
            s.numeric_value = displayed;
            s.value = number_text(displayed) + u"%";
        }
    }
    std::string_view name() const override { return "progress"; }
    Role role() const override { return Role::progress; }
    bool requires_typed_creation() const override { return true; }
    bool needs_name() const override { return false; }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress) const override { return false; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<ProgressHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantic = *c.ui.describe(c.control.id);
        auto title = semantic.name + (semantic.value.empty() ? u"" : u": " + semantic.value);
        p.lines = block(c.measure, title, p.text_size, c.width);
        int y = height_of(p.lines) + scaled(c.scale, c.presentation().gap_small);
        p.progress_track = {0, y, std::max(0, c.width), scaled(c.scale, c.presentation().row_padding)};
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
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override { describe_pages(ui, c, s); }
    void describe_operations(const UiState&, const ComponentConfig&, Semantics& s) const override { if (!s.executing) s.operations.push_back(Operation::activate); }
    void describe_node(const UiState&, const ComponentConfig&, const Semantics&, const NodeRef&, SemanticNode& n) const override { n.selection_required = true; }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { ComponentType::verbs(c, out); return; }
        if (c.admits(Operation::focus) || c.admits(Operation::select)) out.push_back(Verb::select);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.ref.item && c.verb == Verb::select) return std::get<TabsHandle>(c.ui.layout_handle(c.ref.id)).activate(*c.ref.item);
        return std::nullopt;
    }
    std::string_view name() const override { return "tabs"; }
    Role role() const override { return Role::tabs; }
    std::optional<Role> item_role() const override { return Role::tab_item; }
    bool requires_typed_creation() const override { return true; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<TabsHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto semantics = *c.ui.describe(c.control.id);
        // A tab is tall enough for its ring, and its label starts a ring's thickness from the edge, focused or not.
        const int ring = focus_ring_width(c.ui, c.appearance, c.scale), text_x = std::max(scaled(c.scale, c.presentation().gap_small), ring);
        const int line_height = c.measure.line_height(c.placement.text_size);
        int height = std::max(c.appearance.button_height(line_height, c.scale, c.ui.presentation().density), line_height + 2 * ring);
        c.placement.label_lines = block(c.measure, semantics.name, c.placement.text_size, c.width);
        int top = height_of(c.placement.label_lines);
        if (top) top += scaled(c.scale, c.presentation().gap_small);
        int count = static_cast<int>(semantics.options.size());
        for (int i = 0; i < count; ++i) {
            int left = c.width * i / count, right = c.width * (i + 1) / count;
            c.placement.options.push_back({left, top, right - left, height});
            const auto& option = semantics.options[static_cast<std::size_t>(i)];
            std::u16string prefix = option.invalid ? c.presentation().invalid_tab_marker : u"";
            if (!option.enabled) prefix += c.presentation().disabled_tab_marker;
            auto label = option.label;
            const int room = room_after(right - left, scaled(c.scale, c.presentation().gap), text_x, scaled(c.scale, c.presentation().gap_small));
            while (!label.empty() && c.measure.measure(prefix + label, c.placement.text_size).width > room) {
                if (label.back() >= 0xdc00 && label.back() <= 0xdfff && label.size() > 1) label.pop_back();
                label.pop_back();
            }
            label = prefix + label;
            c.placement.lines.push_back({{left + text_x, top + (height - c.measure.line_height(c.placement.text_size)) / 2,
                                         room, height}, std::move(label)});
        }
        c.placement.bounds = {0, 0, c.width, top + height};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        paint_lines(c, c.placement.label_lines, c.placement.text_size, palette.text);
        for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
            const auto& option = c.semantics.options[i]; auto rect = c.placement.options[i];
            // The ring frames the candidate tab; the tab keeps its fill, and the selected tab its underline, inside the ring.
            const Color tab_fill = option.selected ? palette.selection : palette.surface;
            ComponentPaint::RingShape shape{rect, 0, 0};
            const bool framed = c.focus_visible && c.semantics.candidate && *c.semantics.candidate == option.value;
            if (framed) shape = c.focus_ring(rect, scaled(c.scale, c.appearance.radius()), tab_fill, c.semantics.invalid);
            marked(c, option.selected ? PaintRole::selection : PaintRole::content, [&] { if (framed) c.rounded(shape.inner, shape.inner_radius, tab_fill); else c.fill(rect, tab_fill); });
            if (c.hovered(option.value)) c.hover(shape.inner, shape.inner_radius);
            if (option.selected) {
                const int thickness = scaled(c.scale, c.presentation().focus);
                marked(c, PaintRole::selection, [&] { c.fill({shape.inner.x, shape.inner.y + shape.inner.h - thickness, shape.inner.w, thickness}, palette.accent); });
            }
            const auto& line = c.placement.lines[i];
            marked(c, option.invalid ? PaintRole::error_text : PaintRole::content, [&] { c.text(line.text, c.placement.text_size, line.bounds.x, line.bounds.y, option.enabled ? palette.text : palette.muted); });
        }
    }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override {
        return plain_key(key) && (key.key == Key::enter || key.key == Key::space || key.key == Key::left || key.key == Key::right);
    }
    Result key_down(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        auto& t = UiState::Internals::tabs(ui).at(ui.focused());
        if (key.key == Key::left || key.key == Key::right) {
            int dir = key.key == Key::left ? -1 : 1;
            int i = static_cast<int>(t.candidate);
            for (std::size_t n = 0; n < t.pages.size(); ++n) {
                i = (i + dir + static_cast<int>(t.pages.size())) % static_cast<int>(t.pages.size());
                if (t.pages[static_cast<std::size_t>(i)].enabled) { t.candidate = static_cast<std::size_t>(i); break; }
            }
            ui.invalidate();
            return {};
        }
        return command_key_down(ui, key);
    }
    void pointer_down(UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override { ui.arm_pointer(c.id, option); }
    Result pointer_up(UiState& ui, const PointerRelease& release) const override { return page_release(ui, release); }
};
class NavigationList final : public ComponentType {
public:
    bool supports_state(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected}); }
    void describe(const UiState& ui, const ComponentConfig& c, Semantics& s) const override { describe_pages(ui, c, s); }
    void describe_node(const UiState&, const ComponentConfig&, const Semantics&, const NodeRef&, SemanticNode& n) const override { n.selection_required = true; }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { ComponentType::verbs(c, out); return; }
        if (c.admits(Operation::focus) || c.admits(Operation::select)) out.push_back(Verb::select);
        if (c.ui.has_menu(c.control) && !c.ui.renderer_failed()) out.push_back(Verb::context_menu);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.ref.item && c.verb == Verb::select) return std::get<NavigationListHandle>(c.ui.layout_handle(c.ref.id)).select(*c.ref.item);
        return std::nullopt;
    }
    std::string_view name() const override { return "navigation_list"; }
    Role role() const override { return Role::list; }
    std::optional<Role> item_role() const override { return Role::list_item; }
    ComponentHandle handle(UiState& ui, const ComponentConfig& c) const override { return ui.typed_handle<NavigationListHandle>(c); }
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
        int height = std::max({scaled(c.scale, 24), line + scaled(c.scale, padding), line + 2 * focus_ring_width(c.ui, c.appearance, c.scale)});
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
            const int radius = material ? rect.h / 2 : scaled(c.scale, 6);
            ComponentPaint::RingShape shape{rect, radius, radius};
            if (option.selected) {
                // The ring frames the selected row (the row that has the keyboard focus); its selection color sits inside the ring.
                if (c.focus_visible) shape = c.focus_ring(rect, radius, material ? palette.surface : palette.panel, c.semantics.invalid);
                marked(c, PaintRole::selection, [&] {
                    c.rounded(shape.inner, shape.inner_radius, palette.selection);
                    if (!material) c.fill({shape.inner.x, shape.inner.y, scaled(c.scale, 3), shape.inner.h}, palette.accent);
                });
            }
            if (c.hovered(option.value)) c.hover(shape.inner, shape.inner_radius);
            const auto& line = c.placement.lines[i];
            c.text(line.text, c.placement.text_size, line.bounds.x, line.bounds.y, option.enabled && c.semantics.enabled ? palette.text : palette.muted);
        }
    }
    bool handles_key(const UiState&, const ComponentConfig&, KeyPress key) const override {
        return plain_key(key) && (key.key == Key::up || key.key == Key::down || key.key == Key::home || key.key == Key::end);
    }
    Result key_down(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        if (auto r = ui.available(ui.focused()); !r) return r;
        auto& t = UiState::Internals::tabs(ui).at(ui.focused());
        int index = static_cast<int>(t.active), last = static_cast<int>(t.pages.size()) - 1;
        if (key.key == Key::home) index = -1;
        else if (key.key == Key::end) index = last + 1;
        else if (key.key != Key::up && key.key != Key::down) return {};
        int direction = key.key == Key::up || key.key == Key::end ? -1 : 1;
        for (index += direction; index >= 0 && index <= last; index += direction)
            if (t.pages[static_cast<std::size_t>(index)].enabled) return ui.activate_tab(ui.focused(), t.pages[static_cast<std::size_t>(index)].value);
        return {};
    }
    bool ignores_press(const UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override {
        if (!option) return false;
        const auto& pages = UiState::Internals::tabs(ui).at(c.id).pages;
        return *option >= pages.size() || !pages[*option].enabled;
    }
    void pointer_down(UiState& ui, const ComponentConfig& c, std::optional<std::size_t> option) const override { ui.arm_pointer(c.id, option); }
    Result pointer_up(UiState& ui, const PointerRelease& release) const override { return page_release(ui, release); }
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
const std::map<ComponentKind, const ComponentType*>& types() {
    static const std::map<ComponentKind, const ComponentType*> registry{
        {ComponentKind::label, &label}, {ComponentKind::text, &text}, {ComponentKind::button, &button},
        {ComponentKind::checkbox, &checkbox}, {ComponentKind::radio_group, &radio_group}, {ComponentKind::slider, &slider}, {ComponentKind::dropdown, &dropdown}, {ComponentKind::list, &list}, {ComponentKind::tabs, &tabs}, {ComponentKind::progress, &progress}, {ComponentKind::navigation_list, &navigation_list}};
    return registry;
}
// Test registrations belong to the thread owning their UI. Built-in lookups remain immutable
// and can be used concurrently by independent UI environments.
std::map<ComponentKind, const ComponentType*>& test_types() {
    thread_local std::map<ComponentKind, const ComponentType*> registry;
    return registry;
}
}
int focus_ring_width(const UiState& ui, const Appearance& appearance, float scale) {
    const auto& settings = ui.presentation();
    return scaled(scale, settings.focus_ring_width.value_or(appearance.component_presentation(settings.density).focus));
}
int ComponentPaint::ring_radius(int own) const {
    const auto& radius = ui.presentation().focus_ring_radius;
    return radius ? static_cast<int>(std::lround(*radius * scale)) : own;
}
Color ComponentPaint::backdrop() const { return placement.dialog ? palette().surface : palette().background; }
ComponentPaint::RingShape ComponentPaint::ring_shape(Rect outer, int radius, int width) {
    RingShape shape;
    shape.outer_radius = std::clamp(radius, 0, std::max(0, std::min(outer.w, outer.h) / 2));  // a radius that is too large is a capsule
    shape.inner = {outer.x + width, outer.y + width, std::max(0, outer.w - 2 * width), std::max(0, outer.h - 2 * width)};
    shape.inner_radius = std::max(0, shape.outer_radius - width);
    return shape;
}
ComponentPaint::RingShape ComponentPaint::frame(Rect outer, int radius, int width, Color edge, Color fill, PaintRole frame_role) {
    const auto shape = ring_shape(outer, radius, width);
    marked(*this, frame_role, [&] { rounded(outer, shape.outer_radius, edge); });
    rounded(shape.inner, shape.inner_radius, fill);
    return shape;
}
ComponentPaint::RingShape ComponentPaint::focus_ring(Rect outer, int own_radius, Color fill, bool invalid) {
    const auto shape = ring_shape(outer, ring_radius(own_radius), focus_ring_width(ui, appearance, scale));
    const auto marks = invalid ? PaintRole::focus_indicator | PaintRole::error_indicator : PaintRole::focus_indicator;
    marked(*this, marks, [&] {
        rounded(outer, shape.outer_radius, invalid ? palette().error : palette().focus_color());
        rounded(shape.inner, shape.inner_radius, fill);
    });
    return shape;
}
Result command_key_down(UiState& ui, KeyPress key) {
    UiState::OperationScope operation(&ui);
    if (key.repeat) return {};
    if (key.key == Key::enter) return ui.invoke(ui.focused());
    if (key.key == Key::space) ui.arm_key(ui.focused());
    return {};
}
void command_pointer_down(UiState& ui) {
    UiState::OperationScope operation(&ui);
    ui.arm_pointer(ui.focused());
}
bool VerbContext::admits(Operation operation) const {
    return std::find(semantics.operations.begin(), semantics.operations.end(), operation) != semantics.operations.end();
}
Result ComponentType::pointer_up(UiState& ui, const PointerRelease& release) const { return ui.invoke(release.id); }
void ComponentType::verbs(const VerbContext& c, std::vector<Verb>& out) const {
    if (c.item) return;
    if (c.ui.has_menu(c.control) && !c.ui.renderer_failed()) out.push_back(Verb::context_menu);
    if (c.admits(Operation::focus)) out.push_back(Verb::focus);
}
const ComponentType* find_component_type(ComponentKind kind) {
    auto test = test_types().find(kind);
    if (test != test_types().end()) return test->second;
    auto it = types().find(kind);
    return it == types().end() ? nullptr : it->second;
}
const ComponentType& component_type(ComponentKind kind) {
    if (const auto* type = find_component_type(kind)) return *type;
    throw std::invalid_argument("Unknown component type");
}
ScopedComponentType::ScopedComponentType(ComponentKind kind, const ComponentType& type) : kind_(kind) {
    if (types().contains(kind) || !test_types().emplace(kind, &type).second)
        throw std::invalid_argument("Component type already registered");
}
ScopedComponentType::~ScopedComponentType() { test_types().erase(kind_); }
}
