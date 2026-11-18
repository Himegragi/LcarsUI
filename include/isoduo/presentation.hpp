#pragma once
#include <optional>
#include <string>

namespace isoduo {
enum class Density { standard, compact };
// automatic: the indicator follows the user's latest real input (keys show it, a pointer press hides it).
// always: shown for every focused component regardless of input.
// never: no component draws a focus indicator, not even an editable text field, and the slider thumb does
//   not grow; it also wins over the system's "always underline keyboard cues" setting. The text caret is
//   not a focus indicator and still draws, and focus is still reported to the semantic layer and to UIA.
enum class FocusIndicatorMode { automatic, always, never };
struct ThemeVariant {
    std::string name;
    static ThemeVariant light() { return {"light"}; }
    static ThemeVariant dark() { return {"dark"}; }
    static ThemeVariant highContrast() { return {"high-contrast"}; }
    bool operator==(const ThemeVariant&) const = default;
};
// Whether on-screen transitions play. There is deliberately no "always on": automatic follows the
// operating system's animation preference (Windows "Show animations in Windows") and off never plays.
enum class Motion { automatic, off };
inline constexpr int minTextSize = 12, maxTextSize = 28;
// The focus ring's thickness and corner radius, in DIP, when the application sets them.
inline constexpr int minFocusRingWidth = 2, maxFocusRingWidth = 6;
inline constexpr int minFocusRingRadius = 0, maxFocusRingRadius = 32;
// Whether the pointer hover overlay is drawn. Off hides it for every source (real mouse or
// simulated); tooltips still follow the pointer.
enum class Hover { on, off };
struct PresentationSettings {
    std::u16string fontFamily;
    int textSize = 16;
    ThemeVariant variant = ThemeVariant::light();
    Density density = Density::standard;
    Hover hover = Hover::on;
    FocusIndicatorMode focusIndicator = FocusIndicatorMode::automatic;
    // The focus ring around the focused component (DIP, 2-6 and 0-32). Empty = the appearance's own value
    // (3 DIP thick; the component's corner radius). A value outside the range makes setPresentation fail
    // with invalidValue and apply nothing. Padding grows with the thickness, so layout reflows once on a change.
    std::optional<int> focusRingWidth;
    std::optional<int> focusRingRadius;
    // Accessibility (WCAG 1.4.8): when on, every justified or distributed text is drawn left-aligned instead.
    // The components keep their property values; only what is drawn (and reported to UIA) changes.
    bool justifyAsLeft = false;
    Motion motion = Motion::automatic;
    bool operator==(const PresentationSettings&) const = default;
};
}
