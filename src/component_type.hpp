#pragma once
// Internal dispatch for the component types implemented by this slice. Applications continue
// to use the typed handles in the public headers; this is not a public plug-in API.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"

namespace lcars::detail {
class UiState;
struct ComponentLayout {
    UiState& ui;
    const Control& control;
    const Appearance& appearance;
    TextMeasure& measure;
    Placement& placement;
    int width;
    float scale;
    bool settle = true; // measurement probes must not change persistent view state
};
enum class PaintRole : unsigned { content = 0, focus_indicator = 1, caret = 2, selection = 4, composition = 8, error_text = 16, error_indicator = 32, hover = 64 };
constexpr PaintRole operator|(PaintRole a, PaintRole b) {
    return static_cast<PaintRole>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
class ComponentPaint {
public:
    ComponentPaint(UiState& state, const Placement& item, const Semantics& semantic,
                   const Appearance& look, float render_scale, bool active_window)
        : ui(state), placement(item), semantics(semantic), appearance(look), scale(render_scale), window_focused(active_window) {}
    virtual ~ComponentPaint() = default;
    UiState& ui;
    const Placement& placement;
    const Semantics& semantics;
    const Appearance& appearance;
    float scale;
    bool window_focused;
    // The only source of paint colors: the appearance's palette for the current theme variant,
    // with the Ui's palette overrides applied.
    Palette palette() const;
    // Pointer hover: the overlay is drawn for the hovered component (hovered()) or for one of its items
    // (hovered(item)); both are false when the presentation switch is off or the target is not admitted.
    bool hovered() const;
    bool hovered(const OptionValue& item) const;
    // Lays the palette's hover color over rect under PaintRole::hover (nothing when its alpha is 0).
    void hover(Rect rect, int radius = 0);
    virtual void role(PaintRole) {}
    // Translucent fill: the color at alpha (0-255) over what is already drawn. Hosts blend it; the
    // default here only keeps test probes that do not care about alpha compiling.
    virtual void overlay(Rect rect, int radius, Color color, unsigned char alpha);
    virtual void fill(Rect rect, Color color) = 0;
    virtual void rounded(Rect rect, int radius, Color color) = 0;
    virtual void clip(Rect rect) = 0;
    // The y coordinate is the top of the line box, not its baseline.
    virtual void text(std::u16string_view value, int pixels, int x, int y, Color color) = 0;
};
// Type-level applicability, independent of any instance's current semantic state.
// Modal and tooltip content are declared by page membership, not by component kind.
enum class ComponentState { normal, disabled, read_only, selected, candidate, error, conflict, executing };
class ComponentType {
public:
    virtual ~ComponentType() = default;
    virtual bool supports_state(ComponentState state) const { return state == ComponentState::normal; }
    virtual ComponentHandle handle(UiState&, const Control&) const = 0;
    virtual void arrange(ComponentLayout&) const = 0;
    virtual void paint(ComponentPaint&) const = 0;
    // A negative width means the component shares its row's remaining space.
    virtual int natural_width(const ComponentLayout&) const { return -1; }
    virtual bool hit(const Placement& p, int x, int y) const {
        return p.bounds.contains(x, y) && p.clip.contains(x, y);
    }
    virtual bool can_focus() const { return true; }
    virtual bool can_target() const { return false; }
    virtual bool accepts_pointer() const { return true; }
    virtual bool selects_text() const { return false; }
    virtual bool has_activate_operation() const { return false; }
    virtual Result key_down(UiState&, KeyPress) const { return {}; }
    virtual void pointer_down(UiState&) const {}
};
const ComponentType& component_type(Kind kind);

// Temporary internal registration supports extension tests without changing any dispatch point.
// Its lifetime must encompass every UI using the registered type, on the UI thread.
class ScopedComponentType {
public:
    ScopedComponentType(Kind kind, const ComponentType& type);
    ~ScopedComponentType();
    ScopedComponentType(const ScopedComponentType&) = delete;
    ScopedComponentType& operator=(const ScopedComponentType&) = delete;
private:
    Kind kind_;
};
}
