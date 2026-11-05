#pragma once
// B01-B03 minimal layout: column/row, grid and one scroll region, measured through a host
// text backend. The same inputs give the same frame, which the host both draws and hit-tests,
// so measurement, painting and pointer targets cannot disagree. arrange settles view state as it
// goes: it clamps the scroll offset and each field's display offset before placing anything.
#include "lcars/appearance.hpp"
#include "lcars/core.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcars {
// Caret stops for one shaped line, in physical pixels. Drawing and hit-testing share it.
struct TextRun {
    std::vector<std::pair<unsigned, float>> stops;  // UTF-16 offset -> x, ascending, starts {0, 0}
    float width = 0;
    float x(unsigned offset) const;    // x of the last stop at or before offset
    unsigned hit(float x) const;       // nearest stop
};
class TextMeasure {
public:
    virtual ~TextMeasure() = default;
    virtual TextRun measure(std::u16string_view text, int pixels) = 0;
    virtual int line_height(int pixels) = 0;
};
// Breaks at '\n' and before the first character that would overflow width (CJK-friendly).
std::vector<std::u16string> wrap(TextMeasure& measure, std::u16string_view text, int pixels, int width);

namespace layout {
struct Node {
    enum class Type { control, column, row, grid, scroll, stack, pinned } type = Type::column;
    ComponentHandle component;   // control: non-owning typed reference
    int text_size = 0;   // control: label size override in DIP, 0 = appearance default
    int gap = 0;         // DIP between children
    int padding = 0;     // DIP around children
    int min_column = 0;  // grid: as many columns as fit at this width, at least one
    std::vector<Node> children;
};
Node control(ComponentHandle component, int text_size = 0);
Node column(std::vector<Node> children, int gap = 12, int padding = 0);
Node row(std::vector<Node> children, int gap = 12);  // buttons keep natural width, others share the rest
Node grid(std::vector<Node> children, int min_column, int gap = 16);
Node scroll(Node child);  // S1 supports one scroll region per frame
Node pinned(Node header, Node content); // fixed header above a scroll region
Node stack(std::vector<Node> children); // later children are painted and hit above earlier children
}

struct Line { Rect bounds; std::u16string text; };
struct Placement {
    std::string id;
    Kind kind = Kind::label;
    Rect bounds, clip;            // physical pixels after scrolling; hits need bounds and clip
    bool popup = false;           // an overlay belongs to its source, outside normal layout flow
    int text_size = 0;            // physical pixels
    int caption_size = 0;         // text fields: label, description and error text size
    std::vector<Line> lines;      // labels and command titles
    std::vector<Rect> options;   // radio option hit and paint bounds
    Rect slider_track;           // slider track in frame coordinates
    FieldLayout field;            // text fields, translated to frame coordinates
    std::vector<Line> label_lines, description_lines, error_lines;
    TextRun run;                  // text fields: visible text (composition included)
    float offset = 0;             // text fields: horizontal display offset in pixels
    int caret_x(unsigned offset) const;  // text fields: frame x of a caret, as painted and hit
};
struct ScrollState { int offset = 0; };  // physical pixels, clamped by every arrange
struct Frame {
    std::vector<Placement> items;
    std::vector<Placement> overlays; // painted and hit above normal content and scrollbars
    Rect viewport, track, thumb;  // scroll region and its bar; thumb empty when nothing scrolls
    int extent = 0, maximum = 0, offset = 0;
    const Placement* find(const TextHandle& handle) const;
    const Placement* find(const ButtonHandle& handle) const;
    const Placement* find(const LabelHandle& handle) const;
    const Placement* find(const CheckboxHandle& handle) const;
    const Placement* find(const RadioGroupHandle& handle) const;
    const Placement* find(const SliderHandle& handle) const;
    const Placement* find(const DropdownHandle& handle) const;
    const Placement* find(const TabsHandle& handle) const;
    const Placement* find(const ListHandle& handle) const;
    std::optional<std::size_t> option_at(const Placement& item, int x, int y) const;
    const Placement* hit(int x, int y) const;        // topmost focusable-or-label item under the point
    unsigned text_offset(const TextHandle& handle, int x) const;  // field caret offset for a pointer x
    const detail::UiState* owner_ = nullptr; // identity only; never dereferenced
    int offset_for_thumb(int thumb_top) const;       // scroll offset when the thumb top is dragged here
};
// reveal: when not empty, scroll so that control is fully visible (top-aligned if taller).
// Updates each focused field's display offset in ui so its caret stays visible.
Frame arrange(const layout::Node& root, Ui& ui, const Appearance& appearance, TextMeasure& measure,
              int width, int height, float scale, ScrollState& scroll, std::string_view reveal = {});
}
