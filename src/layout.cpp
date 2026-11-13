#include <limits>
#include "lcars/layout.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace lcars {
float TextRun::x(unsigned offset) const {
    float result = 0;
    for (const auto& [stop, position] : stops) {
        if (stop > offset) break;
        result = position;
    }
    return result;
}
unsigned TextRun::hit(float x) const {
    unsigned best = 0;
    float distance = std::numeric_limits<float>::max();
    for (const auto& [stop, position] : stops)
        if (std::abs(position - x) < distance) { distance = std::abs(position - x); best = stop; }
    return best;
}
unsigned TextRun::cell(float x) const {
    unsigned result = 0;
    for (const auto& [stop, position] : stops) {
        if (position > x) break;
        result = stop;
    }
    return result;
}
namespace {
// Measures text[begin, end) as one run (stops relative to begin).
using RangeMeasure = std::function<TextRun(unsigned begin, unsigned end)>;
// The paragraphs of text cut into display lines of at most width: [begin, end) pairs of offsets, LFs excluded.
std::vector<std::pair<unsigned, unsigned>> wrap_ranges(std::u16string_view text, int width, const RangeMeasure& run_of) {
    std::vector<std::pair<unsigned, unsigned>> lines;
    std::size_t start = 0;
    while (true) {
        auto end = text.find(u'\n', start);
        const unsigned p0 = static_cast<unsigned>(start);
        const unsigned p1 = static_cast<unsigned>(end == std::u16string_view::npos ? text.size() : end);
        auto paragraph = text.substr(p0, p1 - p0);
        auto run = run_of(p0, p1);
        // A break may follow a space or touch a non-ASCII (CJK) character; Latin words stay whole
        // unless a single word is wider than the line.
        auto breakable = [&](unsigned k) {
            return paragraph[k - 1] == u' ' || paragraph[k - 1] >= 0x80 || (k < paragraph.size() && paragraph[k] >= 0x80);
        };
        unsigned line_start = 0, previous = 0;
        float base = 0;
        for (const auto& [stop, x] : run.stops) {
            if (stop == 0) continue;
            if (x - base > width && previous > line_start) {
                unsigned cut = previous;  // hard cut unless a caret stop on this line may break
                for (auto it = run.stops.rbegin(); it != run.stops.rend(); ++it)
                    if (it->first > line_start && it->first <= previous && breakable(it->first)) { cut = it->first; break; }
                lines.emplace_back(p0 + line_start, p0 + cut);
                line_start = cut;
                base = run.x(cut);
            }
            previous = stop;
        }
        lines.emplace_back(p0 + line_start, p1);
        if (end == std::u16string_view::npos) break;
        start = end + 1;
    }
    return lines;
}
// text[begin, end) in runs of one style, each measured with its own face; the stops continue across runs.
TextRun styled_run(TextMeasure& measure, std::u16string_view text, int pixels, const std::vector<StyleSpan>& spans,
                   unsigned begin, unsigned end, std::vector<FieldLine::Piece>* pieces) {
    TextRun out;
    out.stops.push_back({0, 0.f});
    float x = 0;
    auto emit = [&](unsigned from, unsigned to, const TextStyle& style) {
        if (to <= from) return;
        auto run = measure.measure(text.substr(from, to - from), pixels, FontStyle{style.bold, style.italic});
        for (const auto& [offset, position] : run.stops)
            if (offset > 0) out.stops.push_back({from - begin + offset, x + position});
        if (pieces) pieces->push_back({from - begin, to - begin, style, x, run.width});
        x += run.width;
    };
    unsigned cursor = begin;
    for (const auto& span : spans) {
        const unsigned from = std::max(span.start, begin), to = std::min(span.start + span.length, end);
        if (from >= to || from < cursor) continue;
        emit(cursor, from, {});
        emit(from, to, span.style());
        cursor = to;
    }
    emit(cursor, end, {});
    out.width = x;
    return out;
}
}
std::vector<std::u16string> wrap(TextMeasure& measure, std::u16string_view text, int pixels, int width) {
    std::vector<std::u16string> lines;
    for (auto [begin, end] : wrap_ranges(text, width, [&](unsigned a, unsigned b) { return measure.measure(text.substr(a, b - a), pixels); }))
        lines.emplace_back(text.substr(begin, end - begin));
    return lines;
}
std::vector<WrappedLine> wrap_marked(TextMeasure& measure, std::u16string_view text, int pixels, int width) {
    std::vector<WrappedLine> out;
    std::size_t pos = 0;
    for (auto& piece : wrap(measure, text, pixels, width)) {
        WrappedLine line;
        pos += piece.size();
        line.text = std::move(piece);
        // wrap() drops the LF between paragraphs; a wrapped piece is followed by more of its own text.
        if (pos < text.size() && text[pos] == u'\n') ++pos;
        else line.soft = pos < text.size();
        out.push_back(std::move(line));
    }
    return out;
}
std::vector<FieldLine> wrap_lines(TextMeasure& measure, std::u16string_view text, int pixels, int width, const std::vector<StyleSpan>* spans) {
    RangeMeasure run_of = [&](unsigned a, unsigned b) {
        return spans ? styled_run(measure, text, pixels, *spans, a, b, nullptr) : measure.measure(text.substr(a, b - a), pixels);
    };
    std::vector<FieldLine> out;
    for (auto [begin, end] : wrap_ranges(text, width, run_of)) {
        FieldLine line;
        line.begin = begin;
        line.end = end;
        // The LF between paragraphs is not part of a line; a wrapped piece is followed by more of its own text.
        if (end < text.size() && text[end] != u'\n') line.soft = true;
        line.run = spans ? styled_run(measure, text, pixels, *spans, begin, end, &line.pieces) : measure.measure(text.substr(begin, end - begin), pixels);
        out.push_back(std::move(line));
    }
    return out;
}
unsigned FieldView::line_of(unsigned offset) const {
    if (lines.empty()) return 0;
    auto it = std::upper_bound(lines.begin(), lines.end(), offset, [](unsigned o, const FieldLine& line) { return o < line.begin; });
    unsigned index = it == lines.begin() ? 0 : static_cast<unsigned>(it - lines.begin()) - 1;
    if (upstream && index > 0 && lines[index - 1].soft && lines[index - 1].end == offset) --index;
    return index;
}
std::pair<unsigned, bool> FieldView::hit_in_line(unsigned line, float x) const {
    if (lines.empty()) return {0, false};
    const auto& item = lines[std::min<std::size_t>(line, lines.size() - 1)];
    unsigned offset = item.begin + item.run.hit(x - item.shift);
    return {offset, item.soft && offset == item.end};
}
unsigned FieldView::cell_in_line(unsigned line, float x) const {
    if (lines.empty()) return 0;
    const auto& item = lines[std::min<std::size_t>(line, lines.size() - 1)];
    unsigned offset = item.begin + item.run.cell(x - item.shift);
    if (item.soft && offset == item.end && offset > item.begin) --offset;  // right of a wrapped line: its last character
    return offset;
}
float FieldView::x_in_line(unsigned line, unsigned offset) const {
    if (lines.empty()) return 0;
    const auto& item = lines[std::min<std::size_t>(line, lines.size() - 1)];
    return item.shift + item.run.x(std::clamp(offset, item.begin, item.end) - item.begin);
}
int FieldView::row_y(unsigned line, unsigned first_line) const {
    if (lines.empty()) return text.y;
    const auto at = [&](unsigned i) { return lines[std::min<std::size_t>(i, lines.size() - 1)].top; };
    return text.y + at(line) - at(first_line);
}
unsigned FieldView::line_at(int y, unsigned first_line) const {
    if (lines.empty()) return 0;
    const unsigned last = static_cast<unsigned>(std::min<std::size_t>(first_line + std::max(1u, visible), lines.size())) - 1;
    unsigned found = std::min(first_line, last);
    for (unsigned i = found + 1; i <= last && row_y(i, first_line) <= y; ++i) found = i;
    return found;
}

namespace layout {
Node component(ComponentHandle component, int text_size) { Node n; n.kind = Node::Kind::component; n.component = std::move(component); n.text_size = text_size; return n; }
Node column(std::vector<Node> children, int gap, int padding) {
    Node n; n.kind = Node::Kind::column; n.children = std::move(children); n.gap = gap; n.padding = padding; return n;
}
Node row(std::vector<Node> children, int gap) { Node n; n.kind = Node::Kind::row; n.children = std::move(children); n.gap = gap; return n; }
Node grid(std::vector<Node> children, int min_column, int gap) {
    Node n; n.kind = Node::Kind::grid; n.children = std::move(children); n.min_column = min_column; n.gap = gap; return n;
}
Node scroll(Node child) { Node n; n.kind = Node::Kind::scroll; n.children.push_back(std::move(child)); return n; }
Node scroll(Node child, ScrollState& state, int height) {
    if (height <= 0) throw std::invalid_argument("Nested scroll height must be positive");
    Node n = scroll(std::move(child)); n.scroll_state = &state; n.scroll_height = height; return n;
}
Node scroll_text(LabelHandle label, ScrollState& state, int height) {
    Node n = scroll(component(label), state, height); n.scrollable_text = true; return n;
}
Node vertical_text(LabelHandle label, int height) {
    if (height < 0) throw std::invalid_argument("Vertical text height must not be negative");
    Node n = component(label); n.vertical_height = height; return n;
}
Node pinned(Node header, Node content) { Node n; n.kind = Node::Kind::pinned; n.children.push_back(std::move(header)); n.children.push_back(std::move(content)); return n; }
Node stack(std::vector<Node> children) { Node n; n.kind = Node::Kind::stack; n.children = std::move(children); return n; }
Node modal(Node background, std::vector<DialogLayout> dialogs) {
    Node n; n.kind = Node::Kind::modal; n.children.push_back(std::move(background));
    for (auto& entry : dialogs) { n.dialogs.push_back(std::move(entry.dialog)); n.children.push_back(std::move(entry.content)); }
    return n;
}
Node modal(Node background, Node content, DialogHandle dialog) {
    return modal(std::move(background), {{std::move(dialog), std::move(content)}});
}
}

const Placement* detail::find_placement(const Frame& frame, std::string_view id) {
    if (frame.modal) {
        auto overlay = std::find_if(frame.overlays.begin(), frame.overlays.end(), [&](const Placement& p) { return p.id == id; });
        if (overlay != frame.overlays.end()) return &*overlay;
    }
    auto it = std::find_if(frame.items.begin(), frame.items.end(), [&](const Placement& p) { return p.id == id; });
    return it == frame.items.end() ? nullptr : &*it;
}
const Placement* Frame::find(const TextHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const ButtonHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const LabelHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const CheckboxHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const RadioGroupHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const SliderHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const ProgressHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const DropdownHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const TabsHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const NavigationListHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
const Placement* Frame::find(const ListHandle& handle) const {
    return handle.ui_ == owner_ && !handle.lifetime_.expired() ? detail::find_placement(*this, handle.id_) : nullptr;
}
std::optional<std::size_t> Frame::option_at(const Placement& item, int x, int y) const {
    if (!item.clip.contains(x, y)) return std::nullopt;
    for (std::size_t i = 0; i < item.options.size(); ++i) if (item.options[i].contains(x, y)) return item.option_start + i;
    return std::nullopt;
}
const Placement* Frame::menu_panel() const {
    auto it = std::find_if(overlays.begin(), overlays.end(), [](const Placement& p) { return p.menu; });
    return it == overlays.end() ? nullptr : &*it;
}
bool Frame::menu_contains(int x, int y) const {
    const auto* panel = menu_panel();
    return panel && panel->bounds.contains(x, y);
}
std::optional<std::string> Frame::menu_item_at(int x, int y) const {
    const auto* panel = menu_panel();
    if (!panel || !panel->menu_inner.contains(x, y)) return std::nullopt;
    for (const auto& row : panel->menu_rows) if (row.bounds.contains(x, y)) return row.id;
    return std::nullopt;
}
const Placement* Frame::hit(int x, int y) const {
    for (auto it = overlays.rbegin(); it != overlays.rend(); ++it)
        if (!it->tooltip && !it->menu && detail::component_type(it->kind).hit(*it, x, y)) return &*it;
    if (modal) return nullptr;
    for (auto it = items.rbegin(); it != items.rend(); ++it)
        if (detail::component_type(it->kind).hit(*it, x, y)) return &*it;
    return nullptr;
}
detail::TextHit detail::placement_text_hit(const Frame& frame, std::string_view id, int x, int y) {
    const auto* p = detail::find_placement(frame, id);
    if (!p || !component_type(p->kind).selects_text()) return {};
    if (!p->multiline) return {p->run.hit(static_cast<float>(x - p->bounds.x - p->field.editor.x) - p->shift + p->offset), false};
    auto [offset, upstream] = p->view.hit_in_line(p->view.line_at(y), static_cast<float>(x - p->view.text.x));
    return {offset, upstream};
}
unsigned detail::placement_text_cell(const Frame& frame, std::string_view id, int x, int y) {
    const auto* p = detail::find_placement(frame, id);
    if (!p || !component_type(p->kind).selects_text()) return 0;
    if (!p->multiline) return p->run.cell(static_cast<float>(x - p->bounds.x - p->field.editor.x) - p->shift + p->offset);
    return p->view.cell_in_line(p->view.line_at(y), static_cast<float>(x - p->view.text.x));
}
unsigned detail::placement_text_offset(const Frame& frame, std::string_view id, int x) {
    const auto* p = detail::find_placement(frame, id);
    if (!p || !component_type(p->kind).selects_text()) return 0;
    if (p->multiline) return placement_text_hit(frame, id, x, p->view.text.y).offset;
    return p->run.hit(static_cast<float>(x - p->bounds.x - p->field.editor.x) - p->shift + p->offset);
}
unsigned Frame::text_offset(const TextHandle& handle, int x) const {
    const auto* p = find(handle);
    if (!p) return 0;
    if (p->multiline) return detail::placement_text_hit(*this, p->id, x, p->view.text.y).offset;
    return p->run.hit(static_cast<float>(x - p->bounds.x - p->field.editor.x) - p->shift + p->offset);
}
unsigned Frame::text_offset(const TextHandle& handle, int x, int y) const {
    const auto* p = find(handle);
    return p ? detail::placement_text_hit(*this, p->id, x, y).offset : 0;
}
int Placement::caret_x(unsigned offset) const {
    if (multiline) return view.text.x + static_cast<int>(std::lround(view.x_in_line(view.line_of(offset), offset)));
    return bounds.x + field.editor.x + static_cast<int>(std::lround(shift + run.x(offset) - this->offset));
}
Rect Placement::caret_rect(unsigned offset) const {
    if (!multiline) return {caret_x(offset), bounds.y + field.editor.y, 1, field.editor.h};
    const unsigned last = static_cast<unsigned>(std::min<std::size_t>(view.first + std::max(1u, view.visible), view.lines.size())) - 1;
    return {caret_x(offset), view.row_y(std::clamp(view.line_of(offset), std::min(view.first, last), last)), 1, view.row_height};
}
int Placement::caret_y(unsigned offset) const {
    if (!multiline) return bounds.y + field.editor.y;
    return view.row_y(view.line_of(offset));
}
int ScrollRegion::offset_for_thumb(int thumb_position) const {
    if (horizontal) {
        // The text starts at the right end: the thumb at the right is offset 0, at the left the last column.
        int travel = track.w - thumb.w;
        if (maximum <= 0 || travel <= 0) return 0;
        return std::clamp(maximum - static_cast<int>(std::lround(double(thumb_position - track.x) * maximum / travel)), 0, maximum);
    }
    int travel = track.h - thumb.h;
    if (maximum <= 0 || travel <= 0) return 0;
    return std::clamp(static_cast<int>(std::lround(double(thumb_position - track.y) * maximum / travel)), 0, maximum);
}
bool Frame::scroll_to(Ui& ui, ScrollState& state, int offset) const {
    auto& owner = detail::access(ui);
    if (owner_ != &owner) return false;
    bool popup_scroll = std::any_of(overlays.begin(), overlays.end(), [&](const Placement& p) { return p.dropdown_panel && &owner.dropdown_scroll(p.id) == &state; });
    auto region = std::find_if(regions.begin(), regions.end(), [&](const ScrollRegion& r) { return r.state == &state; });
    if (region == regions.end()) return false;
    // An open dialog blocks the page behind it, but scrolls its own content.
    if ((modal || owner.modal()) && !popup_scroll && !region->modal_content) return false;
    int next = std::clamp(offset, 0, region->maximum);
    if (next == state.offset) return false;
    if (!region->field.empty()) {  // a text field's scrollbar moves by whole lines, at least one per request
        int step = std::max(1, region->step), lines = (next + step / 2) / step, current = state.offset / step;
        if (lines == current) lines += next > state.offset ? 1 : -1;
        if (!owner.scroll_lines_to(region->field, static_cast<unsigned>(std::clamp(lines, 0, region->maximum / step)))) return false;
        owner.scrolled();
        return true;
    }
    state.offset = next;
    if (popup_scroll) owner.popup_scrolled();
    else owner.scrolled();
    return true;
}
bool Frame::wheel(Ui& ui, int x, int y, int delta, bool sideways_only) const {
    auto& owner = detail::access(ui);
    if (owner_ != &owner) return false;
    // An open context menu owns the wheel: it scrolls itself and never the page behind it.
    if (const auto* menu = menu_panel()) {
        auto* state = owner.menu_scroll();
        if (!state || !menu->bounds.contains(x, y)) return false;
        int next = std::clamp(state->offset - delta, 0, menu->menu_maximum);
        if (next == state->offset) return false;
        state->offset = next;
        owner.popup_scrolled();
        return true;
    }
    // A popup owns its own wheel input; it must not scroll the background beneath it.
    for (auto it = overlays.rbegin(); it != overlays.rend() && !sideways_only; ++it) {
        if (!it->dropdown_panel || it->tooltip || !it->bounds.contains(x, y) || !it->clip.contains(x, y)) continue;
        const auto& type = detail::component_type(it->kind);
        if (type.opens_popup()) {
            auto current = detail::access(ui).describe(it->id);
            if (!current || !current->expanded) continue; // the frame can outlive a dismissed panel
            auto& state = owner.dropdown_scroll(it->id);
            return scroll_to(ui, state, state.offset - delta);
        }
        return false;
    }
    const bool blocked = modal || owner.modal();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->clip.contains(x, y) || (blocked && !it->modal_content) || (sideways_only && !it->horizontal)) continue;
        if (scroll_to(ui, *it->state, it->state->offset - delta)) return true;
    }
    return false;
}

bool Frame::hwheel(Ui& ui, int x, int y, int delta) const {
    auto& owner = detail::access(ui);
    if (owner_ != &owner) return false;
    const bool blocked = modal || owner.modal();
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->horizontal || !it->clip.contains(x, y) || (blocked && !it->modal_content)) continue;
        if (scroll_to(ui, *it->state, it->state->offset - delta)) return true;
    }
    return false;
}
bool Frame::select_to(Ui& ui, const TextHandle& handle, int x, int y) const {
    auto& owner = detail::access(ui);
    if (owner_ != &owner) return false;
    const auto* p = find(handle);
    auto* session = p ? owner.edit(p->id) : nullptr;
    if (!p || !session) return false;
    const auto before = session->selection;
    const bool upstream_before = session->caret_upstream;
    bool scrolled = false;
    if (!p->multiline) owner.text_pointer_drag(p->id, detail::placement_text_hit(*this, p->id, x, y), detail::placement_text_cell(*this, p->id, x, y));
    else {
        const auto& view = p->view;
        const auto* current = owner.field_view(p->id);  // this frame may be older than the last scroll
        const unsigned count = static_cast<unsigned>(view.lines.size());
        unsigned first = current ? current->first : view.first;
        const int top = view.text.y, bottom = top + view.text.h;
        if (y < top && first > 0) { if ((scrolled = owner.scroll_lines_to(p->id, first - 1))) --first; }
        else if (y >= bottom && first + view.visible < count) { if ((scrolled = owner.scroll_lines_to(p->id, first + 1))) ++first; }
        unsigned line = y < top ? first : y >= bottom ? std::min(first + view.visible, count) - 1 : view.line_at(y);
        auto [offset, upstream] = view.hit_in_line(line, static_cast<float>(x - view.text.x));
        const unsigned cell = view.cell_in_line(line, static_cast<float>(x - view.text.x));
        owner.text_pointer_drag(p->id, {offset, upstream}, cell);
    }
    if (scrolled) owner.scrolled();
    const auto after = session->selection;
    return scrolled || after.anchor != before.anchor || after.caret != before.caret || session->caret_upstream != upstream_before;
}

int Frame::offset_for_thumb(int thumb_top) const {
    return ScrollRegion{nullptr, viewport, viewport, track, thumb, extent, maximum, offset}.offset_for_thumb(thumb_top);
}

namespace {
using layout::Node;
void translate(Placement& p, int dx, int dy) {
    auto move = [&](Rect& r) { r.x += dx; r.y += dy; };
    move(p.bounds);
    if (p.clipped) move(p.clip);
    move(p.slider_track);
    move(p.view.text); move(p.view.track); move(p.view.thumb);
    move(p.progress_track); move(p.progress_fill);
    for (auto& option : p.options) move(option);
    for (auto* lines : {&p.lines, &p.label_lines, &p.description_lines, &p.error_lines})
        for (auto& line : *lines) move(line.bounds);
    for (auto& column : p.column_layout.columns) move(column.bounds);
    move(p.column_layout.track); move(p.column_layout.thumb);
}
struct Arranger {
    detail::UiState& ui;
    const Appearance& appearance;
    TextMeasure& measure;
    float scale;
    ScrollState& scroll;
    std::string_view reveal;
    Frame frame;
    bool measuring = false;
    bool in_modal = false;  // placing the open dialog's content
    int d(int v) const { return static_cast<int>(std::lround(v * scale)); }
    int pixels(int size) const { return static_cast<int>(std::lround((size ? size / 16.f : 1.f) * ui.presentation().text_size * scale)); }

    int component_width(const Node& node) {
        auto id = node.kind == Node::Kind::component ? ui.layout_id(node.component) : std::nullopt;
        auto control = id ? ui.find(*id) : nullptr;
        if (!control) return -1;
        Placement p;
        p.text_size = pixels(node.text_size);
        detail::ComponentLayout context{ui, *control, appearance, measure, p, 0, scale};
        return detail::component_type(control->kind).natural_width(context);
    }
    Placement build(const Node& node, int width) {
        auto id = ui.layout_id(node.component);
        const auto* control = id ? ui.find(*id) : nullptr;
        if (!control) throw std::invalid_argument("Layout names an unknown control");
        Placement p;
        p.id = control->id; p.kind = control->kind;
        p.text_size = pixels(node.text_size);
        detail::ComponentLayout context{ui, *control, appearance, measure, p, width, scale, !measuring};
        context.vertical_height = d(node.vertical_height);
        detail::component_type(control->kind).arrange(context);
        return p;
    }

    int height(const Node& node, int width) {
        switch (node.kind) {
        case Node::Kind::component: {
            auto id = ui.layout_id(node.component);
            if (!id && ui.retired_button(node.component)) return 0;
            if (id && ui.available(*id).status == Status::hidden) return 0;
            bool previous = measuring; measuring = true;
            int result = build(node, width).bounds.h;
            measuring = previous; return result;
        }
        case Node::Kind::scroll: return node.scroll_height ? d(node.scroll_height) : 0;
        default: {
            std::vector<Placement> scratch;
            bool previous = measuring; measuring = true;
            int result = place(node, {0, 0, width, 0}, scratch);
            measuring = previous; return result;
        }
        }
    }
    // Places node into r (r.h ignored except for scroll); returns the used height.
    int place(const Node& node, Rect r, std::vector<Placement>& out) {
        int pad = d(node.padding), gap = d(node.gap);
        switch (node.kind) {
        case Node::Kind::component: {
            auto id = ui.layout_id(node.component);
            if (!id && ui.retired_button(node.component)) return 0;
            if (id && ui.available(*id).status == Status::hidden) return 0;
            auto p = build(node, r.w);
            translate(p, r.x, r.y);
            if (p.vertical) {
                // A vertical label never draws outside its own box; too many columns scroll sideways inside it.
                p.clip = p.bounds; p.clipped = true;
                if (!measuring && p.column_layout.maximum > 0) {
                    ScrollRegion region; region.state = &ui.vertical_scroll(p.id); region.horizontal = true;
                    region.viewport = region.clip = p.bounds;
                    region.extent = p.column_layout.extent; region.maximum = p.column_layout.maximum; region.offset = p.column_layout.offset;
                    region.track = p.column_layout.track; region.thumb = p.column_layout.thumb;
                    frame.regions.push_back(region);
                }
            }
            if (p.multiline && !measuring) frame.regions.push_back(field_region(p));
            out.push_back(std::move(p));
            return out.back().bounds.h;
        }
        case Node::Kind::column: {
            int y = r.y + pad;
            bool first = true;
            for (const auto& child : node.children) {
                int used = place(child, {r.x + pad, first ? y : y + gap, r.w - 2 * pad, 0}, out);
                if (used == 0) continue;
                y += (first ? 0 : gap) + used;
                first = false;
            }
            return y - r.y + pad;
        }
        case Node::Kind::row: {
            int fixed = 0, flexible = 0;
            for (const auto& child : node.children) {
                if (auto width = component_width(child); width >= 0) fixed += width; else ++flexible;
            }
            int total_gap = gap * std::max(0, static_cast<int>(node.children.size()) - 1);
            int share = flexible ? std::max(0, (r.w - fixed - total_gap) / flexible) : 0;
            std::vector<int> widths, heights;
            int x = r.x, tallest = 0;
            for (const auto& child : node.children) {
                auto natural = component_width(child);
                widths.push_back(natural >= 0 ? std::min(natural, std::max(0, r.x + r.w - x)) : share);
                heights.push_back(height(child, widths.back()));
                tallest = std::max(tallest, heights.back());
                x += widths.back() + gap;
            }
            x = r.x;
            for (std::size_t i = 0; i < node.children.size(); ++i) {  // centered on the cross axis
                place(node.children[i], {x, r.y + (tallest - heights[i]) / 2, widths[i], 0}, out);
                x += widths[i] + gap;
            }
            return tallest;
        }
        case Node::Kind::grid: {
            int min = d(node.min_column), n = static_cast<int>(node.children.size());
            int columns = std::clamp((r.w + gap) / std::max(1, min + gap), 1, std::max(1, n));
            int w = (r.w - gap * (columns - 1)) / columns, y = r.y;
            for (int start = 0; start < n; start += columns) {
                int tallest = 0;
                for (int c = 0; c < columns && start + c < n; ++c)
                    tallest = std::max(tallest, place(node.children[start + c], {r.x + c * (w + gap), y, w, 0}, out));
                y += tallest + (start + columns < n ? gap : 0);
            }
            return y - r.y;
        }
        case Node::Kind::scroll:
            if (node.scroll_height) r.h = d(node.scroll_height);
            if (measuring) return r.h;
            return place_scroll(node, r, out);
        case Node::Kind::pinned: {
            int header = place(node.children[0], {r.x, r.y, r.w, 0}, out);
            place(node.children[1], {r.x, r.y + header, r.w, std::max(0, r.h - header)}, out);
            return r.h;
        }
        case Node::Kind::stack: {
            int tallest = 0;
            for (const auto& child : node.children) tallest = std::max(tallest, place(child, r, out));
            return tallest;
        }
        case Node::Kind::modal: {
            place(node.children[0], r, out);
            std::size_t selected = node.dialogs.size();
            for (std::size_t i = 0; i < node.dialogs.size(); ++i) {
                auto open = node.dialogs[i].is_open();
                if (open && *open) { selected = i; break; }
            }
            if (selected == node.dialogs.size()) return r.h;
            const auto& content = node.children[selected + 1];
            int margin = d(20), padding = d(20);
            int w = std::max(0, std::min(d(480), r.w - 2 * margin));
            int content_width = std::max(0, w - 2 * padding);
            int h = std::min(std::max(0, r.h - 2 * margin), height(content, content_width) + 2 * padding);
            Rect panel{r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
            frame.modal = true; frame.modal_panel = panel;
            std::vector<Placement> modal_items;
            auto region_start = frame.regions.size();
            in_modal = true;
            place(content, {panel.x + padding, panel.y + padding, content_width, std::max(0, h - 2 * padding)}, modal_items);
            in_modal = false;
            for (auto i = region_start; i < frame.regions.size(); ++i) {
                frame.regions[i].modal_content = true;
                frame.regions[i].clip = intersect(frame.regions[i].clip, panel);
            }
            for (auto& p : modal_items) {
                p.popup = true; p.dialog = true;
                p.clip = p.clipped ? intersect(p.clip, panel) : panel;
                p.clipped = true;
                frame.overlays.push_back(std::move(p));
            }
            return r.h;
        }
        }
        return 0;
    }
    // A multi-line field scrolls like any viewport: wheel, track and thumb find it among the regions.
    ScrollRegion field_region(const Placement& p) {
        const auto& v = p.view;
        ScrollRegion region;
        region.state = &ui.line_scroll(p.id);
        region.field = p.id; region.step = v.row_height; region.modal_content = in_modal;
        region.viewport = region.clip = {v.text.x, v.text.y, v.track.x + v.track.w - v.text.x, v.text.h};
        region.track = v.track; region.thumb = v.thumb;
        const int count = static_cast<int>(v.lines.size()), visible = static_cast<int>(v.visible);
        region.extent = count * v.row_height;   // scrolling is by whole lines: a uniform row stands in for the gaps
        region.maximum = std::max(0, count - visible) * v.row_height;
        region.offset = static_cast<int>(v.first) * v.row_height;
        return region;
    }
    int place_scroll(const Node& node, Rect r, std::vector<Placement>& out) {
        const auto& child = node.children.front();
        auto& state = node.scroll_state ? *node.scroll_state : scroll;
        int bar = d(10);
        // A scrolling text is framed by the focus ring at the edge of its viewport: it keeps a ring's thickness from the
        // sides whether or not it is focused (when it does not overflow it cannot be focused, and stays as padded).
        const int side = node.scrollable_text ? detail::focus_ring_width(ui, appearance, scale) : 0;
        int content = height(child, std::max(0, r.w - 2 * side));
        int width = content > r.h ? std::max(0, r.w - bar - d(4)) : r.w;
        if (width != r.w) content = height(child, std::max(0, width - 2 * side));
        std::vector<Placement> placed;
        auto region_start = frame.regions.size();
        place(child, {r.x + side, r.y, std::max(0, width - 2 * side), 0}, placed);
        ScrollRegion region; region.state = &state; region.viewport = region.clip = r;
        region.extent = content;
        region.maximum = std::max(0, content - r.h);
        if (node.scrollable_text && placed.size() == 1) {  // one Up/Down step is one row, line spacing included
            const auto* label = ui.find(placed.front().id);
            ui.set_scrollable_text(placed.front().id, state, region.maximum,
                                   detail::text_row(measure.line_height(placed.front().text_size), label ? label->line_spacing : 1.0));
        }
        int offset = std::clamp(state.offset, 0, region.maximum);
        bool size_changed = state.text_size != 0 && state.text_size != ui.presentation().text_size;
        auto target_id = !reveal.empty() ? reveal : (size_changed ? std::string_view(ui.focused()) : std::string_view{});
        if (size_changed && target_id.empty() && !state.anchor_id.empty()) {
            auto it = std::find_if(placed.begin(), placed.end(), [&](const Placement& p) { return p.id == state.anchor_id; });
            if (it != placed.end()) {
                Rect target = it->bounds;
                if (!state.anchor_item.empty()) {
                    auto semantic = ui.describe(it->id);
                    if (semantic) for (std::size_t i = 0; i < semantic->items.size() && i < it->options.size(); ++i)
                        if (semantic->items[i].value.id == state.anchor_item) { target = it->options[i]; break; }
                }
                offset = std::clamp(target.y - r.y, 0, region.maximum);
            }
        }
        if (!target_id.empty()) {
            auto it = std::find_if(placed.begin(), placed.end(), [&](const Placement& p) { return p.id == target_id; });
            if (it != placed.end()) {
                auto target = it->bounds;
                if (detail::component_type(it->kind).has_row_cursor()) {
                    auto s = ui.describe(it->id);
                    if (s && s->candidate) {
                        for (std::size_t i = 0; i < s->items.size() && i < it->options.size(); ++i)
                            if (s->items[i].value == *s->candidate) { target = it->options[i]; break; }
                    }
                }
                int top = target.y - r.y, bottom = top + target.h;
                if (top < offset || target.h > r.h) offset = top;
                else if (bottom > offset + r.h) offset = bottom - r.h;
                offset = std::clamp(offset, 0, region.maximum);
            }
        }
        state.text_size = ui.presentation().text_size;
        state.anchor_id.clear(); state.anchor_item.clear();
        int best = std::numeric_limits<int>::max();
        for (const auto& p : placed) {
            if (detail::component_type(p.kind).has_row_cursor() && !p.options.empty()) {
                auto semantic = ui.describe(p.id);
                if (semantic) for (std::size_t i = 0; i < p.options.size() && i < semantic->items.size(); ++i) {
                    const auto& row = p.options[i];
                    if (row.y + row.h > r.y + offset && row.y < best) {
                        best = row.y; state.anchor_id = p.id; state.anchor_item = semantic->items[i].value.id;
                    }
                }
            } else if (p.bounds.y + p.bounds.h > r.y + offset && p.bounds.y < best) {
                best = p.bounds.y; state.anchor_id = p.id; state.anchor_item.clear();
            }
        }
        // Revealing the focused source during arrangement (resize, DPI change) is layout, not
        // scroll input: an open popup or tooltip stays and follows its anchor (B04, D01).
        state.offset = region.offset = offset;
        for (auto& p : placed) {
            translate(p, 0, -offset);
            p.clip = p.clipped ? intersect(p.clip, r) : r;
            p.clipped = true;
            out.push_back(std::move(p));
        }
        // Nested viewports move with their content and inherit every ancestor's clip.
        for (std::size_t i = region_start; i < frame.regions.size(); ++i) {
            auto& inner = frame.regions[i];
            inner.viewport.y -= offset; inner.clip.y -= offset;
            inner.track.y -= offset; inner.thumb.y -= offset;
            inner.clip = intersect(inner.clip, r);
        }
        if (region.maximum > 0) {
            region.track = {r.x + r.w - bar, r.y, bar, r.h};
            int thumb = std::min(r.h, std::max(d(24), r.h * r.h / std::max(1, content)));
            region.thumb = {region.track.x, r.y + (r.h - thumb) * offset / region.maximum, bar, thumb};
        }
        frame.regions.insert(frame.regions.begin() + region_start, region);
        return r.h;
    }
};
// Where a keyboard-opened menu hangs: what the component's type says (a text field's caret line, a list's current row), else the component.
Rect keyboard_anchor(detail::UiState& state, const Placement& target, const std::optional<OptionValue>& item, int row_inset) {
    if (auto anchor = detail::component_type(target.kind).keyboard_anchor(state, target, item, row_inset)) return *anchor;
    auto visible = intersect(target.bounds, target.clip);
    if (visible.w <= 0 || visible.h <= 0) visible = target.bounds;
    return {visible.x, visible.y, 0, visible.h};
}
// The open context menu is one overlay above everything: placed beside its anchor, flipped left and
// up when the window edge is in the way, scrollable when even then it does not fit.
void place_menu(Frame& frame, detail::UiState& state, const Appearance& appearance, TextMeasure& measure,
                int width, int height, float scale) {
    auto view = state.menu_view();
    if (!view) return;
    const auto look = appearance.component_presentation(state.presentation().density);
    auto d = [&](int value) { return std::max(1, static_cast<int>(std::lround(value * scale))); };
    const Placement* target = detail::find_placement(frame, view->target);
    const int pixels = target && target->text_size ? target->text_size : static_cast<int>(std::lround(state.presentation().text_size * scale));
    const int line = measure.line_height(pixels);
    const int row_height = std::max(d(24), line + d(look.gap)), separator_height = d(look.gap);
    const int pad_x = d(look.gap) + d(look.gap_small), pad_y = d(look.gap_small), border = d(look.thin), hint_gap = d(24);
    int label_width = 0, hint_width = 0, rows_height = 0;
    for (const auto& entry : view->entries) {
        rows_height += entry.separator ? separator_height : row_height;
        if (entry.separator) continue;
        label_width = std::max(label_width, static_cast<int>(std::ceil(measure.measure(entry.label, pixels).width)));
        if (!entry.hint.empty()) hint_width = std::max(hint_width, static_cast<int>(std::ceil(measure.measure(entry.hint, pixels).width)));
    }
    const int extent = rows_height + 2 * pad_y;
    const int full_height = extent + 2 * border;
    const int w = std::min(width, std::max(d(96), label_width + (hint_width ? hint_gap + hint_width : 0) + 2 * pad_x + 2 * border));
    Rect anchor{0, 0, 0, 0};
    if (view->at) anchor = {view->at->x, view->at->y, 0, 0};
    else if (target) anchor = keyboard_anchor(state, *target, view->anchor_item, d(look.gap));
    int x = anchor.x;
    if (x + w > width) x = anchor.x - w;
    x = std::clamp(x, 0, std::max(0, width - w));
    const int below = anchor.y + anchor.h, space_below = height - below, space_above = anchor.y;
    int h = std::min(full_height, height), y;
    if (full_height <= space_below) y = below;
    else if (full_height <= space_above) y = anchor.y - full_height;
    else if (space_below >= h) y = below;
    else if (space_above >= h) y = anchor.y - h;
    else y = std::clamp(below, 0, std::max(0, height - h));
    Placement popup;
    popup.id = menu_node_id(view->target); popup.kind = target ? target->kind : ComponentKind::label;
    popup.menu = popup.popup = true;
    popup.text_size = pixels; popup.menu_pad = pad_x;
    popup.bounds = {x, y, w, h}; popup.clip = {0, 0, width, height}; popup.clipped = true;
    const Rect inner{x + border, y + border, std::max(0, w - 2 * border), std::max(0, h - 2 * border)};
    popup.menu_inner = inner;
    const int maximum = std::max(0, extent - inner.h);
    popup.menu_maximum = maximum;
    auto* scroll = state.menu_scroll();
    int offset = scroll ? std::clamp(scroll->offset, 0, maximum) : 0;
    if (scroll && state.menu_reveal()) {
        int top = 0;
        for (const auto& entry : view->entries) {
            const int rh = entry.separator ? separator_height : row_height;
            if (!entry.separator && entry.id == view->highlighted) {
                if (top < offset) offset = top;
                else if (top + rh + 2 * pad_y > offset + inner.h) offset = top + rh + 2 * pad_y - inner.h;
                break;
            }
            top += rh;
        }
        offset = std::clamp(offset, 0, maximum);
    }
    if (scroll) scroll->offset = offset;
    const int bar = maximum ? d(6) : 0;
    int row_y = inner.y + pad_y - offset;
    for (const auto& entry : view->entries) {
        const int rh = entry.separator ? separator_height : row_height;
        popup.menu_rows.push_back({entry.id, {inner.x, row_y, std::max(0, inner.w - bar), rh}, entry.label, entry.hint,
                                   entry.separator, entry.enabled, !entry.separator && entry.id == view->highlighted});
        row_y += rh;
    }
    if (maximum) {
        popup.menu_track = {inner.x + inner.w - bar, inner.y, bar, inner.h};
        const int thumb = std::min(inner.h, std::max(d(24), inner.h * inner.h / std::max(1, extent)));
        popup.menu_thumb = {popup.menu_track.x, inner.y + (inner.h - thumb) * offset / maximum, bar, thumb};
    }
    frame.overlays.push_back(std::move(popup));
}
}

Frame arrange(const layout::Node& root, Ui& ui, const Appearance& appearance, TextMeasure& measure,
              int width, int height, float scale, ScrollState& scroll, std::string_view reveal) {
    detail::UiState::OperationScope operation(&detail::access(ui));
    Arranger a{detail::access(ui), appearance, measure, scale, scroll, reveal, {}};
    Rect window{0, 0, width, height};
    a.frame.viewport = window;
    a.frame.owner_ = &detail::access(ui);
    std::vector<Placement> items;
    a.place(root, window, items);
    for (auto& p : items) {
        p.clip = p.clipped ? intersect(p.clip, window) : window;
    }
    a.frame.items = std::move(items);
    for (auto& region : a.frame.regions) region.clip = intersect(region.clip, window);
    // The frame-level scroll fields describe the page, never a sideways-scrolling label or a text field.
    auto page_region = std::find_if(a.frame.regions.begin(), a.frame.regions.end(), [](const ScrollRegion& r) { return !r.horizontal && r.field.empty(); });
    if (page_region != a.frame.regions.end()) {
        const auto& outer = *page_region;
        a.frame.viewport = outer.viewport; a.frame.track = outer.track; a.frame.thumb = outer.thumb;
        a.frame.extent = outer.extent; a.frame.maximum = outer.maximum; a.frame.offset = outer.offset;
    }
    // Overlay placement is relative to its visible source, but clipped to the window, not its scroll region.
    auto popup_sources = a.frame.modal ? a.frame.overlays : a.frame.items;
    for (const auto& source : popup_sources) {
        if (!detail::component_type(source.kind).opens_popup()) continue;
        auto s = detail::access(ui).describe(source.id);
        if (!s || !s->expanded) continue;
        if (intersect(source.bounds, source.clip).h <= 0 || intersect(source.bounds, source.clip).w <= 0) {
            detail::access(ui).close_popup();
            continue;
        }
        Placement popup; popup.id = source.id; popup.kind = source.kind; popup.popup = true; popup.dropdown_panel = true;
        popup.text_size = source.text_size; popup.clip = window;
        int row = std::max(static_cast<int>(std::lround(24 * scale)), measure.line_height(popup.text_size) + static_cast<int>(std::lround(appearance.component_presentation(ui.presentation().density).gap * scale)));
        int h = std::min(height, row * std::max(1, std::min(10, static_cast<int>(s->items.size()))));
        int w = std::min(width, source.bounds.w);
        int x = std::clamp(source.bounds.x, 0, std::max(0, width - w));
        int below = std::clamp(source.bounds.y + source.bounds.h, 0, height);
        int above = std::clamp(source.bounds.y, 0, height);
        int below_space = height - below;
        if (h > below_space && h > above) h = std::max(below_space, above);
        int y = h <= below_space ? below : above - h;
        popup.bounds = {x, y, w, h};
        if (s->items.empty()) popup.lines.push_back({{x + static_cast<int>(std::lround(appearance.component_presentation(ui.presentation().density).gap * scale)), y + (row - measure.line_height(popup.text_size)) / 2, w, measure.line_height(popup.text_size)}, appearance.component_presentation(ui.presentation().density).no_options});
        auto& state = detail::access(ui).dropdown_scroll(source.id);
        int extent = row * static_cast<int>(s->items.size());
        int maximum = std::max(0, extent - h);
        state.offset = std::clamp(state.offset, 0, maximum);
        if (detail::access(ui).dropdown_reveal(source.id) && s->candidate) {
            for (std::size_t i=0; i<s->items.size(); ++i) if (s->items[i].value == *s->candidate) {
                int top = static_cast<int>(i) * row;
                if (top < state.offset) state.offset = top;
                else if (top + row > state.offset + h) state.offset = std::min(maximum, top + row - h);
                break;
            }
        }
        int bar = maximum ? std::max(1, static_cast<int>(std::lround(8 * scale))) : 0;
        popup.clip = popup.bounds;
        std::size_t first = state.offset / row;
        if (first) --first;
        std::size_t last = std::min(s->items.size(), static_cast<std::size_t>((state.offset + h + row - 1) / row + 1));
        popup.option_start = first;
        for (std::size_t i = first; i < last; ++i) {
            Rect r{x, y + static_cast<int>(i) * row - state.offset, w - bar, row};
            popup.options.push_back(r);
            popup.lines.push_back({{r.x + static_cast<int>(std::lround(appearance.component_presentation(ui.presentation().density).gap * scale)), r.y + (row - measure.line_height(popup.text_size)) / 2, std::max(0, r.w - static_cast<int>(std::lround(appearance.component_presentation(ui.presentation().density).body_spacing * scale))), measure.line_height(popup.text_size)}, s->items[i].label});
        }
        if (maximum) {
            ScrollRegion region; region.state=&state; region.viewport=region.clip=popup.bounds;
            region.track={x+w-bar,y,bar,h}; region.extent=extent; region.maximum=maximum; region.offset=state.offset;
            int thumb=std::min(h,std::max(static_cast<int>(std::lround(24*scale)),h*h/extent));
            region.thumb={region.track.x,y+(h-thumb)*state.offset/maximum,bar,thumb};
            a.frame.regions.push_back(region);
        }
        a.frame.overlays.push_back(std::move(popup));
    }
    // Tooltip content follows its source's appearance and is transparent to hit testing.
    if (!a.frame.modal) for (const auto& source : a.frame.items) {
        const auto* text = detail::access(ui).tooltip_text(source.id);
        auto visible = intersect(source.bounds, source.clip);
        if (!text || visible.w <= 0 || visible.h <= 0) continue;
        int margin = std::max(1, static_cast<int>(std::lround(8 * scale)));
        int pad = margin;
        int w = std::max(0, std::min(width - 2 * margin, static_cast<int>(std::lround(320 * scale))));
        int line_height = measure.line_height(source.text_size);
        auto lines = wrap(measure, *text, source.text_size, std::max(1, w - 2 * pad));
        int h = std::min(height, static_cast<int>(lines.size()) * line_height + 2 * pad);
        int x = std::clamp(visible.x, 0, std::max(0, width - w));
        int y = visible.y + visible.h + h <= height ? visible.y + visible.h : visible.y - h;
        y = std::clamp(y, 0, std::max(0, height - h));
        Placement popup; popup.id = source.id; popup.kind = source.kind; popup.popup = popup.tooltip = true;
        popup.text_size = source.text_size; popup.bounds = {x, y, w, h}; popup.clip = window;
        for (std::size_t i = 0; i < lines.size(); ++i)
            popup.lines.push_back({{x + pad, y + pad + static_cast<int>(i) * line_height,
                                    std::max(0, w - 2 * pad), line_height}, std::move(lines[i])});
        a.frame.overlays.push_back(std::move(popup));
    }
    place_menu(a.frame, detail::access(ui), appearance, measure, width, height, scale);
    std::map<Id, bool, std::less<>> visibility;
    auto visible = [](Rect bounds, Rect clip) { auto area = intersect(bounds, clip); return area.w > 0 && area.h > 0; };
    auto record = [&](const Placement& p) {
        visibility[p.id] = visibility[p.id] || visible(p.bounds, p.clip);
        if (!detail::component_type(p.kind).labels_in_placement()) return;
        for (const auto& control : a.ui.controls()) {
            if (control.target != p.id) continue;
            const std::vector<Line>* lines = nullptr;
            if (control.label_kind == LabelKind::name) lines = &p.label_lines;
            else if (control.label_kind == LabelKind::description) lines = &p.description_lines;
            else if (control.label_kind == LabelKind::error) lines = &p.error_lines;
            if (lines) for (const auto& line : *lines)
                visibility[control.id] = visibility[control.id] || visible(line.bounds, p.clip);
        }
    };
    for (const auto& item : a.frame.items) record(item);
    for (const auto& item : a.frame.overlays) if (!item.tooltip && !item.menu) record(item);
    a.ui.frame_visibility(std::move(visibility));
    a.ui.semantic_frame(a.frame);
    a.ui.hover_refresh(a.frame);  // scrolling and reflow move components under a resting pointer
    a.ui.slider_track_refresh(a.frame);  // a held slider thumb is drawn on this frame's track
    return std::move(a.frame);
}
}
