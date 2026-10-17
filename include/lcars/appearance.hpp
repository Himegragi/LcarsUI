#pragma once
#include <string_view>
namespace lcars {
struct Color { unsigned char r, g, b; };
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool operator==(const Rect&) const = default;
};
inline Rect intersect(Rect a, Rect b) {
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w, bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return {x, y, right > x ? right - x : 0, bottom > y ? bottom - y : 0};
}
struct FieldParts { Rect label, surface, editor; bool underline = false; };
struct Palette { Color background, panel, surface, text, muted, border, accent, on_accent, selection, error; };
// Measured text blocks a field must host. Heights are physical pixels from the text backend.
struct FieldText {
    int label = 0, description = 0, error = 0;  // wrapped block heights; 0 when absent
    int editor = 0;                            // one line of editable text
};
// Where an appearance puts a field's role carriers. Rects are relative to the field origin.
struct FieldLayout {
    Rect label, surface, editor, description, error;
    int height = 0;
    bool underline = false;
};
// Visual structure only: an appearance arranges role carriers and colors. It never validates,
// edits text or handles events, so a second appearance cannot change behavior.
class Appearance {
public:
    virtual ~Appearance() = default;
    virtual std::string_view name() const = 0;
    virtual Palette palette() const = 0;
    virtual FieldParts field(Rect bounds, int text_size) const = 0;
    virtual int radius() const = 0;
    // S1 form arrangement. scale converts the appearance's DIP spacing to physical pixels;
    // labels and errors keep their association with the field in every arrangement.
    virtual FieldLayout arrange_field(int width, const FieldText& text, float scale) const = 0;
    virtual int button_height(int line_height, float scale) const = 0;
    virtual int button_padding(float scale) const = 0;
    virtual int text_size() const { return 16; }  // body text in DIP
};
const Appearance& web_appearance();
const Appearance& material_appearance();
}
