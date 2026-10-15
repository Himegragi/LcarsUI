#pragma once
#include <string_view>
namespace lcars {
struct Color { unsigned char r, g, b; };
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
struct FieldParts { Rect label, surface, editor; bool underline = false; };
struct Palette { Color background, panel, surface, text, muted, border, accent, on_accent, selection, error; };
class Appearance {
public:
    virtual ~Appearance() = default;
    virtual std::string_view name() const = 0;
    virtual Palette palette() const = 0;
    virtual FieldParts field(Rect bounds, int text_size) const = 0;
    virtual int radius() const = 0;
};
const Appearance& web_appearance();
const Appearance& material_appearance();
}
