#include "lcars/appearance.hpp"
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
};
}
const Appearance& material_appearance() { static Material skin; return skin; }
}
