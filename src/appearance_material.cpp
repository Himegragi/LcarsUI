#include "lcars/appearance.hpp"
#include <cmath>
namespace lcars {
namespace {
class Material final : public Appearance {
    std::string_view name() const override { return "Material-inspired"; }
    Palette palette() const override {
        return {{247,244,251},{255,251,255},{235,229,242},{35,30,44},{105,94,118},
                {202,193,213},{121,116,126},{105,74,153},{255,255,255},{221,205,249},{174,40,51},{0,0,0},110,{35,30,44},10};
    }
    bool supports(const ThemeVariant& variant) const override { return variant == ThemeVariant::light() || variant == ThemeVariant::dark(); }
    Palette palette(const ThemeVariant& variant) const override {
        if (variant == ThemeVariant::dark()) return {{20,18,24},{29,27,33},{43,40,49},{232,225,234},{202,196,208},
                {88,82,98},{147,143,153},{208,188,255},{56,30,114},{79,55,139},{242,184,181},{0,0,0},160,{232,225,234},20};
        return palette();
    }
    int radius() const override { return 16; }
    // Filled surface carrying the label inside, underline indicator, supporting text below.
    TextFieldLayout arrangeTextField(int width, const TextFieldHeights& text, float scale) const override { return arrangeTextField(width, text, scale, Density::standard); }
    TextFieldLayout arrangeTextField(int width, const TextFieldHeights& text, float scale, Density density) const override {
        bool compact = density == Density::compact;
        auto d = [&](int v) { return static_cast<int>(std::lround(v * scale)); };
        TextFieldLayout f;
        f.underline = true;
        int inner = d(14), y = d(compact ? 4 : 8);
        f.label = {inner, y, width - 2 * inner, text.label};
        y += text.label + (text.label ? d(compact ? 2 : 4) : 0);
        f.editor = {inner, y, width - 2 * inner, text.editor};
        y += text.editor + d(compact ? 6 : 10);
        f.surface = {0, 0, width, y};
        if (text.error) { y += d(compact ? 2 : 4); f.error = {inner, y, width - 2 * inner, text.error}; y += text.error; }
        if (text.description) { y += d(compact ? 2 : 4); f.description = {inner, y, width - 2 * inner, text.description}; y += text.description; }
        f.height = y;
        return f;
    }
    int buttonHeight(int lineHeight, float scale) const override { return lineHeight + static_cast<int>(std::lround(20 * scale)); }
    int buttonPadding(float scale) const override { return static_cast<int>(std::lround(24 * scale)); }
    int buttonHeight(int lineHeight, float scale, Density density) const override { return lineHeight + static_cast<int>(std::lround((density == Density::compact ? 10 : 20) * scale)); }
    int buttonPadding(float scale, Density density) const override { return static_cast<int>(std::lround((density == Density::compact ? 16 : 24) * scale)); }
};
}
const Appearance& materialAppearance() { static Material skin; return skin; }
namespace { const bool registered = detail::registerBuiltinAppearance(materialAppearance()); }
}
