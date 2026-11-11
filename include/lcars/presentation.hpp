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
// Whether on-screen transitions play. There is deliberately no "always on": automatic follows the
// operating system's animation preference (Windows "Show animations in Windows") and off never plays.
enum class Motion { automatic, off };
inline constexpr int min_text_size = 12, max_text_size = 28;
struct PresentationSettings {
    std::u16string font_family;
    int text_size = 16;
    ThemeVariant variant = ThemeVariant::light();
    Density density = Density::standard;
    Motion motion = Motion::automatic;
    bool operator==(const PresentationSettings&) const = default;
};
}
