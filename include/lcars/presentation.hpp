#pragma once
#include <optional>
#include <string>

namespace lcars {
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
    static ThemeVariant high_contrast() { return {"high-contrast"}; }
    bool operator==(const ThemeVariant&) const = default;
};
// Whether on-screen transitions play. There is deliberately no "always on": automatic follows the
// operating system's animation preference (Windows "Show animations in Windows") and off never plays.
enum class Motion { automatic, off };
inline constexpr int min_text_size = 12, max_text_size = 28;
// The focus ring's thickness and corner radius, in DIP, when the application sets them.
inline constexpr int min_focus_ring_width = 2, max_focus_ring_width = 6;
inline constexpr int min_focus_ring_radius = 0, max_focus_ring_radius = 32;
// Whether the pointer hover overlay is drawn. Off hides it for every source (real mouse or
// simulated); tooltips still follow the pointer.
enum class Hover { on, off };
struct PresentationSettings {
    std::u16string font_family;
    int text_size = 16;
    ThemeVariant variant = ThemeVariant::light();
    Density density = Density::standard;
    Hover hover = Hover::on;
    FocusIndicatorMode focus_indicator = FocusIndicatorMode::automatic;
    // The focus ring around the focused component (DIP, 2-6 and 0-32). Empty = the appearance's own value
    // (3 DIP thick; the component's corner radius). A value outside the range makes set_presentation fail
    // with invalid_value and apply nothing. Padding grows with the thickness, so layout reflows once on a change.
    std::optional<int> focus_ring_width;
    std::optional<int> focus_ring_radius;
    // Accessibility (WCAG 1.4.8): when on, every justified or distributed text is drawn left-aligned instead.
    // The components keep their property values; only what is drawn (and reported to UIA) changes.
    bool justify_as_left = false;
    Motion motion = Motion::automatic;
    bool operator==(const PresentationSettings&) const = default;
};
}
