#include "lcars/appearance.hpp"
#include <cmath>
namespace lcars {
namespace {
class Web final : public Appearance {
    std::string_view name() const override { return "Minimal Web"; }
    Palette palette() const override {
        return {{245,247,250},{255,255,255},{255,255,255},{24,33,48},{91,105,126},
                {211,218,228},{118,128,145},{30,79,187},{255,255,255},{200,220,255},{172,37,49},{0,0,0},110,{0,0,0},15};
    }
    bool supports(const ThemeVariant& variant) const override { return variant == ThemeVariant::light() || variant == ThemeVariant::dark(); }
    Palette palette(const ThemeVariant& variant) const override {
        if (variant == ThemeVariant::dark()) return {{16,19,25},{24,28,36},{30,35,45},{231,235,241},{156,166,182},
                {66,75,92},{128,138,156},{122,166,255},{12,22,44},{44,72,128},{255,128,128},{0,0,0},160,{255,255,255},20};
        return palette();
    }
    FieldParts field(Rect b, int size) const override {
        return {{b.x,b.y,b.w,size+8}, {b.x,b.y+size+13,b.w,size+24},
                {b.x+12,b.y+size+23,b.w-24,size+4},false};
    }
    int radius() const override { return 6; }
    // Label above an outlined box; description and error stacked below it.
    FieldLayout arrange_field(int width, const FieldText& text, float scale) const override { return arrange_field(width, text, scale, Density::standard); }
    FieldLayout arrange_field(int width, const FieldText& text, float scale, Density density) const override {
        bool compact = density == Density::compact;
        auto d = [&](int v) { return static_cast<int>(std::lround(v * scale)); };
        FieldLayout f;
        int y = 0;
        f.label = {0, y, width, text.label};
        y += text.label + (text.label ? d(compact ? 4 : 6) : 0);
        f.surface = {0, y, width, text.editor + d(compact ? 10 : 20)};
        f.editor = {d(12), y + d(compact ? 5 : 10), width - d(24), text.editor};
        y += f.surface.h;
        if (text.description) { y += d(compact ? 4 : 6); f.description = {0, y, width, text.description}; y += text.description; }
        if (text.error) { y += d(compact ? 4 : 6); f.error = {0, y, width, text.error}; y += text.error; }
        f.height = y;
        return f;
    }
    int button_height(int line_height, float scale) const override { return line_height + static_cast<int>(std::lround(16 * scale)); }
    int button_padding(float scale) const override { return static_cast<int>(std::lround(16 * scale)); }
    int button_height(int line_height, float scale, Density density) const override { return line_height + static_cast<int>(std::lround((density == Density::compact ? 8 : 16) * scale)); }
    int button_padding(float scale, Density density) const override { return static_cast<int>(std::lround((density == Density::compact ? 12 : 16) * scale)); }
};
}
const Appearance& web_appearance() { static Web skin; return skin; }
namespace { const bool registered = detail::register_builtin_appearance(web_appearance()); }
}
