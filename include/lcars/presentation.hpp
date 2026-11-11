#pragma once
#include <string>

namespace lcars {
enum class Density { standard, compact };
// automatic: the indicator follows the user's latest real input (keys show it, a pointer press hides it).
// always: shown for every focused component regardless of input. There is deliberately no "never".
enum class FocusIndicatorMode { automatic, always };
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
    FocusIndicatorMode focus_indicator = FocusIndicatorMode::automatic;
    // Accessibility (WCAG 1.4.8): when on, every justified or distributed text is drawn left-aligned instead.
    // The components keep their property values; only what is drawn (and reported to UIA) changes.
    bool justify_as_left = false;
    bool operator==(const PresentationSettings&) const = default;
};
}
