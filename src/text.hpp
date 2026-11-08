#pragma once
#include "lcars/appearance.hpp"
#include "render.hpp"
#include "lcars/layout.hpp"
#include "lcars/diagnostics.hpp"
#include <map>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include <functional>
#include <optional>
#include "font_files.hpp"
namespace lcars::detail {
struct Glyph { unsigned font, id, cluster; float x, dx, dy, advance; };
// Convert a FreeType bitmap row into single-channel coverage. Returns false for
// unsupported pixel modes; the caller paints a missing-glyph box in that case.
bool bitmap_coverage(unsigned mode, const unsigned char* row, unsigned width,
                     std::vector<unsigned char>& coverage);
struct TextLayout {
    std::vector<Glyph> glyphs;
    std::map<unsigned,float> carets;
    float width=0;
    unsigned missing=0;
    int size=18;
    float at(unsigned offset) const;
    unsigned hit(float x) const;
};
class Canvas {
public:
    int width, height;
    std::vector<unsigned char> pixels;
    Rect clip;
    bool count_ink = false;
    std::uint64_t ink = 0; // nontransparent samples accepted by the current clip
    Canvas(int width,int height,Color background);
    void fill(Rect rect,Color color);
    void rounded(Rect rect,int radius,Color color);
    void blend(int x,int y,Color color,unsigned alpha);
};
class Text {
public:
    Text();
    Text(std::function<FontFile(std::u16string_view)> lookup,
         std::function<void(std::string_view)> diagnostic);
    Text(std::function<FontFile(std::u16string_view)> lookup,
         std::function<void(const RenderDiagnostic&)> diagnostic);
    ~Text();
    Text(const Text&)=delete;
    Text& operator=(const Text&)=delete;
    TextLayout shape(std::u16string_view text,int pixels);
    void draw(Canvas& canvas,const TextLayout& layout,int x,int baseline,Color color);
    void draw(RecordingCanvas& canvas,const TextLayout& layout,int x,int baseline,Color color);
    void label(Canvas& canvas,std::u16string_view value,Rect rect,int size,Color color);
    void test_bitmap_mode(int mode); // -1 uses the rasterizer's pixel mode
    void set_primary_family(std::u16string family);
    std::optional<float> test_axis_coordinate(unsigned font, std::uint32_t dwrite_tag);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Layout measurement from the same HarfBuzz shaping that draws, so caret stops, wrapping and
// painted glyphs agree. Fonts: Segoe UI with Microsoft JhengHei fallback (see Text).
class ShapedMeasure final : public TextMeasure {
public:
    explicit ShapedMeasure(Text& text) : text_(text) {}
    TextRun measure(std::u16string_view text, int pixels) override;
    int line_height(int pixels) override { return (pixels * 3 + 1) / 2; }
    static int baseline(int pixels) { return pixels * 23 / 20; }  // from the top of a line box
private:
    Text& text_;
};
}
