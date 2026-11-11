#include "lcars/appearance.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace lcars {
namespace {
struct ColorRole { std::string_view name; Color Palette::* member; };
// Every named color in Palette. A new Color field must be added here: the static_assert below
// fails until it is, so new palette colors are overridable without any other change.
constexpr std::array color_roles{
    ColorRole{"background", &Palette::background}, ColorRole{"panel", &Palette::panel}, ColorRole{"surface", &Palette::surface},
    ColorRole{"text", &Palette::text}, ColorRole{"muted", &Palette::muted}, ColorRole{"border", &Palette::border},
    ColorRole{"accent", &Palette::accent}, ColorRole{"on_accent", &Palette::on_accent}, ColorRole{"selection", &Palette::selection},
    ColorRole{"error", &Palette::error}, ColorRole{"scrim", &Palette::scrim},
};
constexpr std::string_view alpha_role = "scrim_alpha";
static_assert(sizeof(Palette) == color_roles.size() * sizeof(Color) + sizeof(unsigned char),
              "Palette gained a field that palette_roles() does not list; add it to color_roles in palette.cpp");

double linear(unsigned char channel) {
    double c = channel / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(Color c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }
}
std::vector<std::string_view> palette_roles() {
    std::vector<std::string_view> names;
    for (const auto& role : color_roles) names.push_back(role.name);
    names.push_back(alpha_role);
    return names;
}
std::optional<PaletteValue> palette_value(const Palette& palette, std::string_view role) {
    if (role == alpha_role) return PaletteValue{static_cast<int>(palette.scrim_alpha)};
    for (const auto& entry : color_roles)
        if (entry.name == role) return PaletteValue{palette.*entry.member};
    return std::nullopt;
}
bool set_palette_value(Palette& palette, std::string_view role, const PaletteValue& value) {
    if (role == alpha_role) {
        const int* alpha = std::get_if<int>(&value);
        if (!alpha || *alpha < 0 || *alpha > 255) return false;
        palette.scrim_alpha = static_cast<unsigned char>(*alpha);
        return true;
    }
    const Color* color = std::get_if<Color>(&value);
    if (!color) return false;
    for (const auto& entry : color_roles)
        if (entry.name == role) { palette.*entry.member = *color; return true; }
    return false;
}
double contrast_ratio(Color a, Color b) {
    double x = luminance(a), y = luminance(b);
    if (x < y) std::swap(x, y);
    return (x + 0.05) / (y + 0.05);
}
std::vector<ContrastFailure> check_palette_contrast(const Palette& p) {
    struct Pair { const char* name; Color foreground, background; double required; };
    const Pair pairs[] = {
        {"text/background", p.text, p.background, 4.5}, {"text/panel", p.text, p.panel, 4.5},
        {"text/surface", p.text, p.surface, 4.5}, {"text/selection", p.text, p.selection, 4.5},
        {"muted/surface", p.muted, p.surface, 4.5}, {"error/surface", p.error, p.surface, 4.5},
        {"on_accent/accent", p.on_accent, p.accent, 4.5}, {"accent/surface", p.accent, p.surface, 3.0},
    };
    std::vector<ContrastFailure> failures;
    for (const auto& pair : pairs) {
        double ratio = contrast_ratio(pair.foreground, pair.background);
        if (ratio < pair.required) failures.push_back({pair.name, ratio, pair.required});
    }
    return failures;
}
}
