#pragma once
#include <string>

namespace lcars {
enum class Density { standard, compact };
struct ThemeVariant {
    std::string name;
    static ThemeVariant light() { return {"light"}; }
    static ThemeVariant dark() { return {"dark"}; }
    static ThemeVariant high_contrast() { return {"high-contrast"}; }
    bool operator==(const ThemeVariant&) const = default;
};
inline constexpr int min_text_size = 12, max_text_size = 28;
struct PresentationSettings {
    std::u16string font_family;
    int text_size = 16;
    ThemeVariant variant = ThemeVariant::light();
    Density density = Density::standard;
    bool operator==(const PresentationSettings&) const = default;
};
}
