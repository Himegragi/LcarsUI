#pragma once
#include "lcars/presentation.hpp"
#include <string_view>
#include <string>
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
struct Palette { Color background, panel, surface, text, muted, border, accent, on_accent, selection, error; Color scrim{0,0,0}; unsigned char scrim_alpha = 110; };
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
// Appearance-owned wording and DIP dimensions used by common component presentations.
struct ComponentPresentation {
    std::u16string read_only_suffix = u"\uff08\u552f\u8b80\uff09";
    std::u16string disabled_suffix = u"\uff08\u505c\u7528\uff09";
    std::u16string executing_suffix = u"\uff08\u57f7\u884c\u4e2d\uff09";
    std::u16string conflict_suffix = u"\uff08\u4f86\u6e90\u5df2\u66f4\u65b0\uff09";
    std::u16string no_selection = u"\u672a\u9078\u53d6";
    std::u16string no_items = u"\u6c92\u6709\u9805\u76ee";
    std::u16string no_options = u"\u6c92\u6709\u53ef\u7528\u9078\u9805";
    std::u16string selected_marker = u"\u2713 ", unselected_marker = u"  ";
    std::u16string invalid_tab_marker = u"! ", disabled_tab_marker = u"[-] ", dropdown_marker = u"\u25be";
    int thin = 1, inset = 2, focus = 3, gap_small = 4, gap = 8, slider_pad = 10;
    int row_padding = 12, indicator = 14, body_spacing = 16, slider_height = 18;
    int dropdown_marker_space = 20, checkbox_extra = 24, radio_label_offset = 26;
};
// Visual structure only: an appearance arranges role carriers and colors. It never validates,
// edits text or handles events, so a second appearance cannot change behavior.
class Appearance {
public:
    virtual ~Appearance() = default;
    virtual std::string_view name() const = 0;
    virtual Palette palette() const = 0;
    virtual bool supports(const ThemeVariant& variant) const { return variant == ThemeVariant::light(); }
    virtual Palette palette(const ThemeVariant&) const { return palette(); }
    virtual FieldParts field(Rect bounds, int text_size) const = 0;
    virtual int radius() const = 0;
    // S1 form arrangement. scale converts the appearance's DIP spacing to physical pixels;
    // labels and errors keep their association with the field in every arrangement.
    virtual FieldLayout arrange_field(int width, const FieldText& text, float scale) const = 0;
    virtual int button_height(int line_height, float scale) const = 0;
    virtual int button_padding(float scale) const = 0;
    virtual const ComponentPresentation& component_presentation() const {
        static const ComponentPresentation defaults;
        return defaults;
    }
    virtual int text_size() const { return 16; }  // body text in DIP
};
const Appearance& web_appearance();
const Appearance& material_appearance();
}
