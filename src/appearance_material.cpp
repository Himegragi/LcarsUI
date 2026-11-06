#include "lcars/appearance.hpp"
#include <cmath>
namespace lcars {
namespace {
class Material final : public Appearance {
    std::string_view name() const override { return "Material-inspired"; }
    Palette palette() const override {
        return {{247,244,251},{255,251,255},{235,229,242},{35,30,44},{105,94,118},
                {202,193,213},{105,74,153},{255,255,255},{221,205,249},{174,40,51}};
    }
    FieldParts field(Rect b, int size) const override {
        return {{b.x+14,b.y+9,b.w-28,size+4}, {b.x,b.y,b.w,size*2+34},
                {b.x+14,b.y+size+21,b.w-28,size+4},true};
    }
    int radius() const override { return 16; }
    // Filled surface carrying the label inside, underline indicator, supporting text below.
    FieldLayout arrange_field(int width, const FieldText& text, float scale) const override {
        auto d = [&](int v) { return static_cast<int>(std::lround(v * scale)); };
        FieldLayout f;
        f.underline = true;
        int inner = d(14), y = d(8);
        f.label = {inner, y, width - 2 * inner, text.label};
        y += text.label + (text.label ? d(4) : 0);
        f.editor = {inner, y, width - 2 * inner, text.editor};
        y += text.editor + d(10);
        f.surface = {0, 0, width, y};
        if (text.error) { y += d(4); f.error = {inner, y, width - 2 * inner, text.error}; y += text.error; }
        if (text.description) { y += d(4); f.description = {inner, y, width - 2 * inner, text.description}; y += text.description; }
        f.height = y;
        return f;
    }
    int button_height(int line_height, float scale) const override { return line_height + static_cast<int>(std::lround(20 * scale)); }
    int button_padding(float scale) const override { return static_cast<int>(std::lround(24 * scale)); }
};
}
const Appearance& material_appearance() { static Material skin; return skin; }
}
