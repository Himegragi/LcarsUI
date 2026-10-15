#include "lcars/appearance.hpp"
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
};
}
const Appearance& web_appearance() { static Web skin; return skin; }
}
