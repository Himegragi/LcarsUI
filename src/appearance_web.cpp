#include "lcars/appearance.hpp"
#include <cmath>
namespace lcars {
namespace {
class Web final : public Appearance {
    std::string_view name() const override { return "Minimal Web"; }
    Palette palette() const override {
        return {{245,247,250},{255,255,255},{255,255,255},{24,33,48},{91,105,126},
                {211,218,228},{30,79,187},{255,255,255},{200,220,255},{172,37,49}};
    }
    FieldParts field(Rect b, int size) const override {
        return {{b.x,b.y,b.w,size+8}, {b.x,b.y+size+13,b.w,size+24},
                {b.x+12,b.y+size+23,b.w-24,size+4},false};
    }
    int radius() const override { return 6; }
    // Label above an outlined box; description and error stacked below it.
    FieldLayout arrange_field(int width, const FieldText& text, float scale) const override {
        auto d = [&](int v) { return static_cast<int>(std::lround(v * scale)); };
        FieldLayout f;
        int y = 0;
        f.label = {0, y, width, text.label};
        y += text.label + (text.label ? d(6) : 0);
        f.surface = {0, y, width, text.editor + d(20)};
        f.editor = {d(12), y + d(10), width - d(24), text.editor};
        y += f.surface.h;
        if (text.description) { y += d(6); f.description = {0, y, width, text.description}; y += text.description; }
        if (text.error) { y += d(6); f.error = {0, y, width, text.error}; y += text.error; }
        f.height = y;
        return f;
    }
    int button_height(int line_height, float scale) const override { return line_height + static_cast<int>(std::lround(16 * scale)); }
    int button_padding(float scale) const override { return static_cast<int>(std::lround(16 * scale)); }
};
}
const Appearance& web_appearance() { static Web skin; return skin; }
}
