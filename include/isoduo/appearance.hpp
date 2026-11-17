#pragma once
#include "isoduo/presentation.hpp"
#include <optional>
#include <string_view>
#include <string>
#include <variant>
#include <vector>
namespace isoduo {
struct Color { unsigned char r, g, b; bool operator==(const Color&) const = default; };
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
// border: decorative lines and dividers (light). outline: the edge that identifies a component (text field box,
// checkbox, radio circle, slider track, scrollbar thumb, ...), >= 3:1 against surface, background and panel.
struct Palette {
    Color background, panel, surface, text, muted, border, outline, accent, onAccent, selection, error; Color scrim{0,0,0}; unsigned char scrimAlpha = 110;
    // Pointer hover: this color at hoverAlpha (0-255) is laid over the hovered component or item.
    // Fields left out of a custom palette keep a faint black overlay (6%).
    Color hover{0,0,0}; unsigned char hoverAlpha = 15;
    // The focus ring's color; >= 3:1 on background, panel and surface. Left empty it is the accent, whatever the accent is
    // when the palette is used (focusColor()): a palette that assigns `accent` after construction, or an accent override,
    // moves the ring with it. Setting `focus` pins the ring to its own color. Read the ring color through focusColor().
    std::optional<Color> focus;
    Color focusColor() const { return focus.value_or(accent); }
    bool operator==(const Palette&) const = default;
};
// Palette roles are addressed by name so an override list never needs a field list of its own:
// every named color in Palette, plus "scrimAlpha" and "hoverAlpha" (0-255, int values), is a role.
using PaletteValue = std::variant<Color, int>;
std::vector<std::string_view> paletteRoles();
std::optional<PaletteValue> paletteValue(const Palette& palette, std::string_view role);
// False (palette untouched) for an unknown role, a value of the wrong kind or an alpha role outside 0-255.
bool setPaletteValue(Palette& palette, std::string_view role, const PaletteValue& value);
// WCAG 2.x contrast ratio (1 to 21) between two sRGB colors.
double contrastRatio(Color a, Color b);
struct ContrastFailure { std::string pair; double ratio = 0, required = 0; };  // pair such as "text/surface"
// Pairs below their WCAG threshold: text on background, panel, surface and selection, muted and error
// on surface, onAccent on accent (4.5:1 each), accent on surface and outline on surface, background and
// panel (3:1 each). border is a decorative divider color and is not checked. Empty when the palette passes.
std::vector<ContrastFailure> checkPaletteContrast(const Palette& palette);
// One entry of Ui::setPaletteOverrides: appearance name as reported by Appearance::name(), one
// theme variant, one palette role, and the replacement value (Color, or int for "scrimAlpha").
struct PaletteOverride {
    std::string appearance;
    ThemeVariant variant;
    std::string role;
    PaletteValue value;
    bool operator==(const PaletteOverride&) const = default;
};
// Measured text blocks a text field must host. Heights are physical pixels from the text backend.
struct TextFieldHeights {
    int label = 0, description = 0, error = 0;  // wrapped block heights; 0 when absent
    int editor = 0;                            // one line of editable text
};
// Where an appearance puts a text field's role carriers. Rects are relative to the text field origin.
struct TextFieldLayout {
    Rect label, surface, editor, description, error;
    int height = 0;
    bool underline = false;
};
// Appearance-owned wording and DIP dimensions used by common component presentations.
struct ComponentPresentation {
    std::u16string readOnlySuffix = u"\uff08\u552f\u8b80\uff09";
    std::u16string disabledSuffix = u"\uff08\u505c\u7528\uff09";
    std::u16string executingSuffix = u"\uff08\u57f7\u884c\u4e2d\uff09";
    std::u16string conflictSuffix = u"\uff08\u4f86\u6e90\u5df2\u66f4\u65b0\uff09";
    std::u16string noSelection = u"\u672a\u9078\u53d6";
    std::u16string noItems = u"\u6c92\u6709\u9805\u76ee";
    std::u16string noOptions = u"\u6c92\u6709\u53ef\u7528\u9078\u9805";
    std::u16string selectedMarker = u"\u2713 ", unselectedMarker = u"  ";
    std::u16string invalidTabMarker = u"! ", disabledTabMarker = u"[-] ", dropdownMarker = u"\u25be";
    int thin = 1, inset = 2, focus = 3, gapSmall = 4, gap = 8, sliderPad = 10;
    int rowPadding = 12, indicator = 14, bodySpacing = 16, sliderHeight = 18;
    int dropdownMarkerSpace = 20, checkboxExtra = 24, radioLabelOffset = 26;
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
    virtual int radius() const = 0;
    // S1 form arrangement. scale converts the appearance's DIP spacing to physical pixels;
    // labels and errors keep their association with the text field in every arrangement.
    virtual TextFieldLayout arrangeTextField(int width, const TextFieldHeights& text, float scale) const = 0;
    virtual int buttonHeight(int lineHeight, float scale) const = 0;
    virtual int buttonPadding(float scale) const = 0;
    virtual const ComponentPresentation& componentPresentation() const {
        static const ComponentPresentation defaults;
        return defaults;
    }
    virtual TextFieldLayout arrangeTextField(int width, const TextFieldHeights& text, float scale, Density) const { return arrangeTextField(width, text, scale); }
    virtual int buttonHeight(int lineHeight, float scale, Density) const { return buttonHeight(lineHeight, scale); }
    virtual int buttonPadding(float scale, Density) const { return buttonPadding(scale); }
    virtual ComponentPresentation componentPresentation(Density density) const {
        auto p = componentPresentation();
        if (density == Density::compact) {
            p.gap = 4; p.gapSmall = 2; p.rowPadding = 6; p.bodySpacing = 8; p.sliderPad = 6;
        }
        return p;
    }
    virtual int textSize() const { return 16; }  // body text in DIP
};
const Appearance& webAppearance();
const Appearance& materialAppearance();
namespace detail {
// Built-in appearances register themselves when their module is linked, so the Ui can resolve the
// appearance names in a palette override list without depending on the modules.
inline std::vector<const Appearance*>& builtinAppearances() { static std::vector<const Appearance*> list; return list; }
inline bool registerBuiltinAppearance(const Appearance& appearance) { builtinAppearances().push_back(&appearance); return true; }
}
// A built-in appearance linked into this program, found by Appearance::name(); nullptr when its module is not linked.
inline const Appearance* builtinAppearance(std::string_view name) {
    for (const auto* appearance : detail::builtinAppearances())
        if (appearance->name() == name) return appearance;
    return nullptr;
}
}
