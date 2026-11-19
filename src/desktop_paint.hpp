#pragma once
#include "core_internal.hpp"
#include "component_type.hpp"
#include "text.hpp"
#include <cmath>
#include <type_traits>

namespace isoduo::detail {
struct PaintObservation {
    enum class Type { text, fill, rounded } type;
    std::string owner;
    std::u16string text;
    Rect bounds, clip;
    Color color;
    bool visible = false, popup = false, tooltip = false;
    bool bold = false, italic = false;  // text: the face it was drawn in
};
template<class Target> void paintDesktop(Target& canvas, const Frame& frame, float s, bool windowFocused,
                  Ui& ui, const Appearance& appearance, Text& text, TextMeasure& measure,
                  std::vector<PaintObservation>* observations = nullptr) {
    UiState::OperationScope operation(&access(ui));
    const auto p = detail::access(ui).effectivePalette(appearance);
    const Rect whole{0, 0, canvas.width, canvas.height};
    struct PaintAdapter final : ComponentPaint {
        Target& canvas;
        Text& renderer;
        std::vector<PaintObservation>* observations;
        PaintAdapter(UiState& ui, const Placement& item, const ComponentSnapshot& semantics, const Appearance& appearance,
                     float scale, bool windowFocused, Target& target, Text& text, std::vector<PaintObservation>* events)
            : ComponentPaint(ui, item, semantics, appearance, scale, windowFocused), canvas(target), renderer(text), observations(events) {}
        void role(PaintRole value) override { if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.currentRole=value; }
        void observe(DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin, bool bold = false, bool italic = false) {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                if (observations) canvas.list.observations.push_back({type, placement.id, std::move(text), bounds,
                    canvas.clip, color, placement.popup, placement.tooltip, begin, canvas.list.commands.size(), bold, italic});
            }
        }
        std::size_t cursor() const {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
            return 0;
        }
        void fill(Rect rect, Color color) override {
            auto begin=cursor(); canvas.fill(rect, color);
            observe(DrawObservation::Type::fill, {}, rect, color, begin);
        }
        void rounded(Rect rect, int radius, Color color) override {
            auto begin=cursor(); canvas.rounded(rect, radius, color);
            observe(DrawObservation::Type::rounded, {}, rect, color, begin);
        }
        void overlay(Rect rect, int radius, Color color, unsigned char alpha) override {
            auto begin=cursor();
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.overlay(rect, radius, color, alpha);
            else if (radius > 0) canvas.rounded(rect, radius, color, alpha);
            else canvas.fill(rect, color, alpha);
            observe(radius > 0 ? DrawObservation::Type::rounded : DrawObservation::Type::fill, {}, rect, color, begin);
        }
        void clip(Rect rect) override { canvas.clip = intersect(canvas.clip, rect); }
        void verticalText(std::u16string_view value, VerticalForm form, int pixels, Rect cell, Color color) override {
            auto begin=cursor();
            renderer.drawVertical(canvas, value, form, pixels, cell, color);
            observe(DrawObservation::Type::text, std::u16string(value), cell, color, begin);
        }
        void text(std::u16string_view value, int pixels, int x, int y, Color color) override { styledText(value, pixels, {}, x, y, color); }
        void styledText(std::u16string_view value, int pixels, FontStyle style, int x, int y, Color color) override {
            auto layout = renderer.shape(value, pixels, style);
            auto begin=cursor();
            renderer.draw(canvas, layout, x, y + ShapedMeasure::baseline(pixels), color);
            observe(DrawObservation::Type::text, std::u16string(value),
                {x, y, static_cast<int>(std::ceil(layout.width)), (pixels * 3 + 1) / 2}, color, begin, style.bold, style.italic);
        }
    };
    for (const auto& item : frame.items) {
        canvas.clip = intersect(item.clip, whole);
        auto semantics = detail::access(ui).describe(item.id);
        PaintAdapter adapter{detail::access(ui), item, *semantics, appearance, s, windowFocused, canvas, text, observations};
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id,item.popup,item.tooltip);
        detail::componentType(item.kind).paint(adapter);
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
    }
    for (const auto& region : frame.regions) {
        canvas.clip = intersect(region.clip, whole);
        if (region.thumb.h) {
            canvas.rounded(region.track, region.track.w / 2, p.panel);
            canvas.rounded(region.thumb, region.thumb.w / 2, p.outline);
        }
    }
    if (frame.modal) {
        canvas.clip = whole;
        if constexpr (std::is_same_v<Target, RecordingCanvas>) {
            canvas.list.setClip(whole); canvas.list.fillRect(whole, {p.scrim.r, p.scrim.g, p.scrim.b, p.scrimAlpha});
        } else {
            for (int y = 0; y < canvas.height; ++y)
                for (int x = 0; x < canvas.width; ++x) canvas.blend(x, y, p.scrim, p.scrimAlpha);
        }
        canvas.rounded(frame.modalPanel, static_cast<int>(appearance.radius() * s), p.surface);
    }
    for (const auto& item : frame.overlays) {
        canvas.clip = intersect(item.clip, whole);
        if (item.menu) {
            // The open context menu: a bordered panel, one row per item, a scrollbar when it overflows.
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id, true, false);
            auto cursor = [&]() -> std::size_t {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
                return 0;
            };
            auto observeMenu = [&](DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin) {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                    if (observations) canvas.list.observations.push_back({type, item.id, std::move(text), bounds,
                        canvas.clip, color, true, false, begin, canvas.list.commands.size()});
                }
            };
            const int radius = static_cast<int>(appearance.radius() * s);
            const int border = std::max(1, static_cast<int>(std::lround(appearance.componentPresentation(ui.presentation().density).thin * s)));
            auto begin = cursor();
            canvas.rounded(item.bounds, radius, p.border);
            observeMenu(DrawObservation::Type::rounded, {}, item.bounds, p.border, begin);
            Rect panel{item.bounds.x + border, item.bounds.y + border, std::max(0, item.bounds.w - 2 * border), std::max(0, item.bounds.h - 2 * border)};
            begin = cursor();
            canvas.rounded(panel, radius, p.surface);
            observeMenu(DrawObservation::Type::rounded, {}, panel, p.surface, begin);
            canvas.clip = intersect(canvas.clip, item.menuInner);
            const int line = measure.lineHeight(item.textSize);
            for (const auto& row : item.menuRows) {
                if (row.bounds.y + row.bounds.h <= item.menuInner.y || row.bounds.y >= item.menuInner.y + item.menuInner.h) continue;
                if (row.separator) {
                    Rect rule{row.bounds.x + item.menuPad, row.bounds.y + row.bounds.h / 2, std::max(0, row.bounds.w - 2 * item.menuPad), std::max(1, static_cast<int>(std::lround(s)))};
                    begin = cursor(); canvas.fill(rule, p.border); observeMenu(DrawObservation::Type::fill, {}, rule, p.border, begin);
                    continue;
                }
                if (row.highlighted) {
                    const Color fill = row.enabled ? p.selection : p.panel;
                    begin = cursor(); canvas.fill(row.bounds, fill); observeMenu(DrawObservation::Type::fill, {}, row.bounds, fill, begin);
                }
                const Color color = row.enabled ? p.text : p.muted;
                const int top = row.bounds.y + (row.bounds.h - line) / 2;
                auto drawText = [&](const std::u16string& value, int x, Color paintColor) {
                    auto layout = text.shape(value, item.textSize);
                    begin = cursor();
                    text.draw(canvas, layout, x, top + ShapedMeasure::baseline(item.textSize), paintColor);
                    observeMenu(DrawObservation::Type::text, value, {x, top, static_cast<int>(std::ceil(layout.width)), line}, paintColor, begin);
                    return static_cast<int>(std::ceil(layout.width));
                };
                drawText(row.label, row.bounds.x + item.menuPad, color);
                if (!row.hint.empty()) {
                    const int width = static_cast<int>(std::ceil(text.shape(row.hint, item.textSize).width));
                    drawText(row.hint, row.bounds.x + row.bounds.w - item.menuPad - width, p.muted);
                }
            }
            if (item.menuThumb.h) {
                canvas.rounded(item.menuTrack, item.menuTrack.w / 2, p.panel);
                canvas.rounded(item.menuThumb, item.menuThumb.w / 2, p.outline);
            }
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
            continue;
        }
        if (item.tooltip) {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id,true,true);
            auto observeTooltip = [&](DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin) {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                    if (observations) canvas.list.observations.push_back({type, item.id, std::move(text), bounds,
                        canvas.clip, color, true, true, begin, canvas.list.commands.size()});
                }
            };
            auto cursor = [&]() -> std::size_t {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
                return 0;
            };
            auto begin = cursor();
            canvas.rounded(item.bounds, static_cast<int>(appearance.radius() * s), p.border);
            observeTooltip(DrawObservation::Type::rounded, {}, item.bounds, p.border, begin);
            int inset = std::max(1, static_cast<int>(std::lround(1 * s)));
            Rect inner{item.bounds.x + inset, item.bounds.y + inset,
                       std::max(0, item.bounds.w - 2 * inset), std::max(0, item.bounds.h - 2 * inset)};
            begin = cursor();
            canvas.rounded(inner, static_cast<int>(appearance.radius() * s), p.surface);
            observeTooltip(DrawObservation::Type::rounded, {}, inner, p.surface, begin);
            canvas.clip = intersect(canvas.clip, inner);
            for (const auto& line : item.lines) {
                begin = cursor();
                text.draw(canvas, text.shape(line.text, item.textSize), line.bounds.x,
                           line.bounds.y + ShapedMeasure::baseline(item.textSize), p.text);
                observeTooltip(DrawObservation::Type::text, line.text, line.bounds, p.text, begin);
            }
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
            continue;
        }
        auto semantics = detail::access(ui).describe(item.id);
        if (!semantics || (!frame.modal && !semantics->expanded)) continue;
        PaintAdapter adapter{detail::access(ui), item, *semantics, appearance, s, windowFocused, canvas, text, observations};
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id,item.popup,item.tooltip);
        detail::componentType(item.kind).paint(adapter);
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
        if (item.dropdownPanel) for (const auto& region : frame.regions) {
            if (region.state != &detail::access(ui).dropdownScroll(item.id) || !region.thumb.h) continue;
            canvas.rounded(region.track, region.track.w / 2, p.panel);
            canvas.rounded(region.thumb, region.thumb.w / 2, p.outline);
        }
    }
    canvas.clip = whole;
}

}
