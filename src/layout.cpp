#include <limits>
#include "lcars/layout.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include <algorithm>
#include <cmath>
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
std::vector<std::u16string> wrap(TextMeasure& measure, std::u16string_view text, int pixels, int width) {
    std::vector<std::u16string> lines;
    std::size_t start = 0;
    while (true) {
        auto end = text.find(u'\n', start);
        auto paragraph = text.substr(start, end == std::u16string_view::npos ? std::u16string_view::npos : end - start);
        auto run = measure.measure(paragraph, pixels);
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
                lines.emplace_back(paragraph.substr(line_start, cut - line_start));
                line_start = cut;
                base = run.x(cut);
            }
            previous = stop;
        }
        lines.emplace_back(paragraph.substr(line_start));
        if (end == std::u16string_view::npos) break;
        start = end + 1;
    }
    return lines;
}

namespace layout {
Node control(ComponentHandle component, int text_size) { Node n; n.type = Node::Type::control; n.component = std::move(component); n.text_size = text_size; return n; }
Node column(std::vector<Node> children, int gap, int padding) {
    Node n; n.type = Node::Type::column; n.children = std::move(children); n.gap = gap; n.padding = padding; return n;
}
Node row(std::vector<Node> children, int gap) { Node n; n.type = Node::Type::row; n.children = std::move(children); n.gap = gap; return n; }
Node grid(std::vector<Node> children, int min_column, int gap) {
    Node n; n.type = Node::Type::grid; n.children = std::move(children); n.min_column = min_column; n.gap = gap; return n;
}
Node scroll(Node child) { Node n; n.type = Node::Type::scroll; n.children.push_back(std::move(child)); return n; }
Node scroll(Node child, ScrollState& state, int height) {
    if (height <= 0) throw std::invalid_argument("Nested scroll height must be positive");
    Node n = scroll(std::move(child)); n.scroll_state = &state; n.scroll_height = height; return n;
}
Node pinned(Node header, Node content) { Node n; n.type = Node::Type::pinned; n.children.push_back(std::move(header)); n.children.push_back(std::move(content)); return n; }
Node stack(std::vector<Node> children) { Node n; n.type = Node::Type::stack; n.children = std::move(children); return n; }
Node modal(Node background, Node content, DialogHandle dialog) {
    Node n; n.type = Node::Type::modal; n.dialog = std::move(dialog);
    n.children.push_back(std::move(background)); n.children.push_back(std::move(content)); return n;
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
    for (std::size_t i = 0; i < item.options.size(); ++i) if (item.options[i].contains(x, y)) return i;
    return std::nullopt;
}
const Placement* Frame::hit(int x, int y) const {
    for (auto it = overlays.rbegin(); it != overlays.rend(); ++it)
        if (!it->tooltip && detail::component_type(it->kind).hit(*it, x, y)) return &*it;
    if (modal) return nullptr;
    for (auto it = items.rbegin(); it != items.rend(); ++it)
        if (detail::component_type(it->kind).hit(*it, x, y)) return &*it;
    return nullptr;
}
unsigned detail::placement_text_offset(const Frame& frame, std::string_view id, int x) {
    const auto* p = detail::find_placement(frame, id);
    if (!p || !component_type(p->kind).selects_text()) return 0;
    return p->run.hit(static_cast<float>(x - p->bounds.x - p->field.editor.x) + p->offset);
}
unsigned Frame::text_offset(const TextHandle& handle, int x) const {
    const auto* p = find(handle);
    return p ? p->run.hit(static_cast<float>(x - p->bounds.x - p->field.editor.x) + p->offset) : 0;
}
int Placement::caret_x(unsigned offset) const {
    return bounds.x + field.editor.x + static_cast<int>(std::lround(run.x(offset) - this->offset));
}
int ScrollRegion::offset_for_thumb(int thumb_top) const {
    int travel = track.h - thumb.h;
    if (maximum <= 0 || travel <= 0) return 0;
    return std::clamp(static_cast<int>(std::lround(double(thumb_top - track.y) * maximum / travel)), 0, maximum);
}
bool Frame::scroll_to(Ui& ui, ScrollState& state, int offset) const {
    auto& owner = detail::access(ui);
    if (owner_ != &owner || modal || owner.modal()) return false;
    auto region = std::find_if(regions.begin(), regions.end(), [&](const ScrollRegion& r) { return r.state == &state; });
    if (region == regions.end()) return false;
    int next = std::clamp(offset, 0, region->maximum);
    if (next == state.offset) return false;
    state.offset = next;
    owner.scrolled();
    return true;
}
bool Frame::wheel(Ui& ui, int x, int y, int delta) const {
    auto& owner = detail::access(ui);
    if (modal || owner.modal() || owner_ != &owner) return false;
    // A popup owns its own wheel input; it must not scroll the background beneath it.
    for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
        if (!it->popup || it->tooltip || !it->bounds.contains(x, y) || !it->clip.contains(x, y)) continue;
        if (it->kind == Kind::dropdown) {
            auto current = detail::access(ui).describe(it->id);
            if (!current || !current->expanded) continue; // the frame can outlive a dismissed panel
        }
        return false;
    }
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->clip.contains(x, y)) continue;
        if (scroll_to(ui, *it->state, it->state->offset - delta)) return true;
    }
    return false;
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
    move(p.progress_track); move(p.progress_fill);
    for (auto& option : p.options) move(option);
    for (auto* lines : {&p.lines, &p.label_lines, &p.description_lines, &p.error_lines})
        for (auto& line : *lines) move(line.bounds);
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
    int d(int v) const { return static_cast<int>(std::lround(v * scale)); }
    int pixels(int size) const { return static_cast<int>(std::lround((size ? size / 16.f : 1.f) * ui.presentation().text_size * scale)); }

    int component_width(const Node& node) {
        auto id = node.type == Node::Type::control ? ui.layout_id(node.component) : std::nullopt;
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
        detail::component_type(control->kind).arrange(context);
        return p;
    }

    int height(const Node& node, int width) {
        switch (node.type) {
        case Node::Type::control: {
            auto id = ui.layout_id(node.component);
            if (!id && ui.retired_button(node.component)) return 0;
            if (id && ui.available(*id).status == Status::hidden) return 0;
            bool previous = measuring; measuring = true;
            int result = build(node, width).bounds.h;
            measuring = previous; return result;
        }
        case Node::Type::scroll: return node.scroll_height ? d(node.scroll_height) : 0;
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
        switch (node.type) {
        case Node::Type::control: {
            auto id = ui.layout_id(node.component);
            if (!id && ui.retired_button(node.component)) return 0;
            if (id && ui.available(*id).status == Status::hidden) return 0;
            auto p = build(node, r.w);
            translate(p, r.x, r.y);
            out.push_back(std::move(p));
            return out.back().bounds.h;
        }
        case Node::Type::column: {
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
        case Node::Type::row: {
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
        case Node::Type::grid: {
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
        case Node::Type::scroll:
            if (node.scroll_height) r.h = d(node.scroll_height);
            if (measuring) return r.h;
            return place_scroll(node, r, out);
        case Node::Type::pinned: {
            int header = place(node.children[0], {r.x, r.y, r.w, 0}, out);
            place(node.children[1], {r.x, r.y + header, r.w, std::max(0, r.h - header)}, out);
            return r.h;
        }
        case Node::Type::stack: {
            int tallest = 0;
            for (const auto& child : node.children) tallest = std::max(tallest, place(child, r, out));
            return tallest;
        }
        case Node::Type::modal: {
            place(node.children[0], r, out);
            auto open = node.dialog.is_open();
            if (!open || !*open) return r.h;
            int margin = d(20), padding = d(20);
            int w = std::max(0, std::min(d(480), r.w - 2 * margin));
            int content_width = std::max(0, w - 2 * padding);
            int h = std::min(std::max(0, r.h - 2 * margin), height(node.children[1], content_width) + 2 * padding);
            Rect panel{r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
            frame.modal = true; frame.modal_panel = panel;
            std::vector<Placement> modal_items;
            place(node.children[1], {panel.x + padding, panel.y + padding, content_width, std::max(0, h - 2 * padding)}, modal_items);
            for (auto& p : modal_items) {
                p.popup = true;
                p.clip = p.clipped ? intersect(p.clip, panel) : panel;
                p.clipped = true;
                frame.overlays.push_back(std::move(p));
            }
            return r.h;
        }
        }
        return 0;
    }
    int place_scroll(const Node& node, Rect r, std::vector<Placement>& out) {
        const auto& child = node.children.front();
        auto& state = node.scroll_state ? *node.scroll_state : scroll;
        int bar = d(10);
        int content = height(child, r.w);
        int width = content > r.h ? std::max(0, r.w - bar - d(4)) : r.w;
        if (width != r.w) content = height(child, width);
        std::vector<Placement> placed;
        auto region_start = frame.regions.size();
        place(child, {r.x, r.y, width, 0}, placed);
        ScrollRegion region; region.state = &state; region.viewport = region.clip = r;
        region.extent = content;
        region.maximum = std::max(0, content - r.h);
        int offset = std::clamp(state.offset, 0, region.maximum);
        bool size_changed = state.text_size != 0 && state.text_size != ui.presentation().text_size;
        auto target_id = !reveal.empty() ? reveal : (size_changed ? std::string_view(ui.focused()) : std::string_view{});
        if (size_changed && target_id.empty() && !state.anchor_id.empty()) {
            auto it = std::find_if(placed.begin(), placed.end(), [&](const Placement& p) { return p.id == state.anchor_id; });
            if (it != placed.end()) {
                Rect target = it->bounds;
                if (!state.anchor_option.empty()) {
                    auto semantic = ui.describe(it->id);
                    if (semantic) for (std::size_t i = 0; i < semantic->options.size() && i < it->options.size(); ++i)
                        if (semantic->options[i].value.id == state.anchor_option) { target = it->options[i]; break; }
                }
                offset = std::clamp(target.y - r.y, 0, region.maximum);
            }
        }
        if (!target_id.empty()) {
            auto it = std::find_if(placed.begin(), placed.end(), [&](const Placement& p) { return p.id == target_id; });
            if (it != placed.end()) {
                auto target = it->bounds;
                if (it->kind == Kind::list) {
                    auto s = ui.describe(it->id);
                    if (s && s->candidate) {
                        for (std::size_t i = 0; i < s->options.size() && i < it->options.size(); ++i)
                            if (s->options[i].value == *s->candidate) { target = it->options[i]; break; }
                    }
                }
                int top = target.y - r.y, bottom = top + target.h;
                if (top < offset || target.h > r.h) offset = top;
                else if (bottom > offset + r.h) offset = bottom - r.h;
                offset = std::clamp(offset, 0, region.maximum);
            }
        }
        state.text_size = ui.presentation().text_size;
        state.anchor_id.clear(); state.anchor_option.clear();
        int best = std::numeric_limits<int>::max();
        for (const auto& p : placed) {
            if (p.kind == Kind::list && !p.options.empty()) {
                auto semantic = ui.describe(p.id);
                if (semantic) for (std::size_t i = 0; i < p.options.size() && i < semantic->options.size(); ++i) {
                    const auto& row = p.options[i];
                    if (row.y + row.h > r.y + offset && row.y < best) {
                        best = row.y; state.anchor_id = p.id; state.anchor_option = semantic->options[i].value.id;
                    }
                }
            } else if (p.bounds.y + p.bounds.h > r.y + offset && p.bounds.y < best) {
                best = p.bounds.y; state.anchor_id = p.id; state.anchor_option.clear();
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
    if (!a.frame.regions.empty()) {
        const auto& outer = a.frame.regions.front();
        a.frame.viewport = outer.viewport; a.frame.track = outer.track; a.frame.thumb = outer.thumb;
        a.frame.extent = outer.extent; a.frame.maximum = outer.maximum; a.frame.offset = outer.offset;
    }
    // Overlay placement is relative to its visible source, but clipped to the window, not its scroll region.
    for (const auto& source : a.frame.items) {
        if (source.kind != Kind::dropdown) continue;
        auto s = detail::access(ui).describe(source.id);
        if (!s || !s->expanded) continue;
        if (intersect(source.bounds, source.clip).h <= 0 || intersect(source.bounds, source.clip).w <= 0) {
            detail::access(ui).close_popup();
            continue;
        }
        Placement popup; popup.id = source.id; popup.kind = source.kind; popup.popup = true;
        popup.text_size = source.text_size; popup.clip = window;
        int row = std::max(1, measure.line_height(popup.text_size) + static_cast<int>(std::lround(appearance.component_presentation().gap * scale)));
        int h = std::min(height, row * std::max(1, static_cast<int>(s->options.size())));
        int w = std::min(width, source.bounds.w);
        int x = std::clamp(source.bounds.x, 0, std::max(0, width - w));
        int below = source.bounds.y + source.bounds.h;
        int y = below + h <= height ? below : source.bounds.y - h;
        y = std::clamp(y, 0, std::max(0, height - h));
        popup.bounds = {x, y, w, h};
        if (s->options.empty()) popup.lines.push_back({{x + static_cast<int>(std::lround(appearance.component_presentation().gap * scale)), y + (row - measure.line_height(popup.text_size)) / 2, w, measure.line_height(popup.text_size)}, appearance.component_presentation().no_options});
        for (std::size_t i = 0; i < s->options.size(); ++i) {
            Rect r{x, y + static_cast<int>(i) * row, w, row};
            popup.options.push_back(r);
            popup.lines.push_back({{r.x + static_cast<int>(std::lround(appearance.component_presentation().gap * scale)), r.y + (row - measure.line_height(popup.text_size)) / 2, std::max(0, r.w - static_cast<int>(std::lround(appearance.component_presentation().body_spacing * scale))), measure.line_height(popup.text_size)}, s->options[i].label});
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
    std::map<Id, bool, std::less<>> visibility;
    auto visible = [](Rect bounds, Rect clip) { auto area = intersect(bounds, clip); return area.w > 0 && area.h > 0; };
    auto record = [&](const Placement& p) {
        visibility[p.id] = visibility[p.id] || visible(p.bounds, p.clip);
        if (p.kind != Kind::text) return;
        for (const auto& control : a.ui.controls()) {
            if (control.target != p.id) continue;
            const std::vector<Line>* lines = nullptr;
            if (control.role == TextRole::name) lines = &p.label_lines;
            else if (control.role == TextRole::description) lines = &p.description_lines;
            else if (control.role == TextRole::error) lines = &p.error_lines;
            if (lines) for (const auto& line : *lines)
                visibility[control.id] = visibility[control.id] || visible(line.bounds, p.clip);
        }
    };
    for (const auto& item : a.frame.items) record(item);
    for (const auto& item : a.frame.overlays) if (!item.tooltip) record(item);
    a.ui.frame_visibility(std::move(visibility));
    return std::move(a.frame);
}
}
