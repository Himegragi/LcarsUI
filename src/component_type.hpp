#pragma once
// Internal dispatch for the component types implemented by this slice. Applications continue
// to use the typed handles in the public headers; this is not a public plug-in API.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    int vertical_height = 0; // physical pixels given by layout::vertical_text; 0 = columns never wrap
    // The appearance's sizes and texts for the density in force now.
    ComponentPresentation presentation() const;
};
enum class PaintRole : unsigned { content = 0, focus_indicator = 1, caret = 2, selection = 4, composition = 8, error_text = 16, error_indicator = 32, hover = 64 };
constexpr PaintRole operator|(PaintRole a, PaintRole b) {
    return static_cast<PaintRole>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
class ComponentPaint {
public:
    ComponentPaint(UiState& state, const Placement& item, const Semantics& semantic,
                   const Appearance& look, float render_scale, bool active_window);
    virtual ~ComponentPaint() = default;
    UiState& ui;
    const Placement& placement;
    const Semantics& semantics;
    const Appearance& appearance;
    float scale;
    bool window_focused;
    // The appearance's sizes and texts for the density in force now.
    ComponentPresentation presentation() const;
    // The only source of paint colors: the appearance's palette for the current theme variant,
    // with the Ui's palette overrides applied.
    Palette palette() const;
    // Pointer hover: the overlay is drawn for the hovered component (hovered()) or for one of its items
    // (hovered(item)); both are false when the presentation switch is off or the target is not admitted.
    bool hovered() const;
    bool hovered(const OptionValue& item) const;
    // Lays the palette's hover color over rect under PaintRole::hover (nothing when its alpha is 0).
    void hover(Rect rect, int radius = 0);

    // Whether this component draws its focus indicator now. Built-in components draw the indicator
    // from this value alone, never from semantics.focused.
    bool focus_visible;

    // Rounded frames, thin or the focus ring (#90). All sizes are physical pixels. A frame is two rounded fills, the
    // `edge` color over the whole outer rect and the `fill` color over what is left after the band, so it is closed
    // on every side; a radius larger than half the shorter side becomes a capsule.
    struct RingShape { Rect inner; int outer_radius = 0, inner_radius = 0; };
    static RingShape ring_shape(Rect outer, int radius, int width);
    // The focus ring's radius now: the presentation setting, else `own`, the component's own corner radius.
    int ring_radius(int own) const;
    // What lies behind a component that paints no background of its own: the page, or the open dialog's panel.
    Color backdrop() const;
    // A thin frame: `edge` under `role`, then `fill`. Returns the shape inside the band for hover and content.
    RingShape frame(Rect outer, int radius, int width, Color edge, Color fill, PaintRole role = PaintRole::content);
    // The focus ring around `outer`, drawn from the ring thickness and radius: the focus color (the error color
    // when `invalid`) over the band and `fill` inside, both marked focus_indicator. Draw it before the content it frames.
    RingShape focus_ring(Rect outer, int own_radius, Color fill, bool invalid = false);
    virtual void role(PaintRole) {}
    // Translucent fill: the color at alpha (0-255) over what is already drawn. Hosts blend it. The default
    // draws the shape through fill()/rounded() (a Color has no alpha) and, for exactly that call, reports the
    // alpha through overlay_alpha(), so a canvas that overrides only those two still sees a translucent fill.
    virtual void overlay(Rect rect, int radius, Color color, unsigned char alpha);
    // 255 except inside the default overlay(): the alpha of the translucent fill being drawn.
    unsigned char overlay_alpha() const { return overlay_alpha_; }
    virtual void fill(Rect rect, Color color) = 0;
    virtual void rounded(Rect rect, int radius, Color color) = 0;
    virtual void clip(Rect rect) = 0;
    // The y coordinate is the top of the line box, not its baseline.
    virtual void text(std::u16string_view value, int pixels, int x, int y, Color color) = 0;
    // One piece of vertical text in its cell (see VerticalForm). The default draws it as horizontal text at the
    // cell's corner; backends that can rotate and center glyphs override it.
    virtual void vertical_text(std::u16string_view value, VerticalForm, int pixels, Rect cell, Color color) { text(value, pixels, cell.x, cell.y, color); }
    // The same in a bold and/or italic face. A painter that has no such faces draws the text plain.
    virtual void styled_text(std::u16string_view value, int pixels, FontStyle style, int x, int y, Color color) { (void)style; text(value, pixels, x, y, color); }
private:
    unsigned char overlay_alpha_ = 255;
};
// Type-level applicability, independent of any instance's current semantic state.
// Modal and tooltip content are declared by page membership, not by component kind.
enum class ComponentState { normal, disabled, read_only, selected, candidate, error, conflict, executing };
// What a pointer press armed, handed back to the type when the button is released over the same component.
struct PointerRelease {
    std::string id;                                        // the armed and released component
    std::optional<std::size_t> pressed_option, option;     // the row under the press and under the release
    std::optional<OptionValue> pressed_value;              // the row's value when the press recorded one
};
// What a type needs to say which verbs one semantic node offers; the node is enabled, visible and not behind a modal.
struct VerbContext {
    const UiState& ui;
    const Control& control;
    const Semantics& semantics;
    const SemanticNode& node;
    bool item;                                             // the node is one item of the control
    bool admits(Operation operation) const;                // semantics.operations holds it
};
// One semantic verb arriving at a type. invalid_argument() is the common answer for a verb whose argument does not fit.
struct PerformContext {
    UiState& ui;
    const NodeRef& ref;
    Verb verb;
    const Argument& argument;
    const SemanticNode& node;
    std::function<Result()> invalid_argument;
};
class ComponentType {
public:
    virtual ~ComponentType() = default;
    virtual bool supports_state(ComponentState state) const { return state == ComponentState::normal; }
    virtual ComponentHandle handle(UiState&, const Control&) const = 0;
    // ---- Identity and creation rules. The defaults are what a type with nothing special says.
    // The name diagnostics use ("custom" for a type that does not name itself).
    virtual std::string_view name() const { return "custom"; }
    // The role of the node, and of its items when it has items.
    virtual Role role() const { return Role::label; }
    virtual std::optional<Role> item_role() const { return std::nullopt; }
    // A control of this type cannot be supplied as data: it is created through its typed handle.
    virtual bool requires_typed_creation() const { return false; }
    // Alignment, justification and spacing; distribute is for labels only (a field respread at every key would be unusable).
    virtual bool supports_text_flow() const { return false; }
    virtual bool admits_distribute() const { return false; }
    // Control::rich is for multi-line fields of such a type, Control::writing_mode vertical for labels.
    virtual bool supports_rich_text() const { return false; }
    virtual bool supports_vertical_writing() const { return false; }
    // Ui::set_error is refused for a type that reports no errors of its own; a focusable control of such a type needs a name.
    virtual bool accepts_error() const { return true; }
    virtual bool needs_name() const { return true; }
    // ---- Input.
    // Whether the focused control takes this key itself (key_down runs) or leaves it to the application's shortcuts.
    virtual bool handles_key(const UiState&, const Control&, KeyPress key) const { return !key.ctrl || !key.letter; }
    // A press on this row of the control is ignored altogether (no focus change either).
    virtual bool ignores_press(const UiState&, const Control&, std::optional<std::size_t> /*option*/) const { return false; }
    virtual void pointer_down(UiState&, const Control&, std::optional<std::size_t> /*option*/) const {}
    // The release over the armed component; the default invokes the component's action.
    virtual Result pointer_up(UiState&, const PointerRelease&) const;
    // ---- Semantics.
    // Fills the type's part of the control's description (values, options, conflicts) and, afterwards, the operations it admits.
    virtual void describe(const UiState&, const Control&, Semantics&) const {}
    virtual void describe_operations(const UiState&, const Control&, Semantics&) const {}
    // The text the control shows when it is not the control's own value (nullopt: control.value).
    virtual std::optional<std::u16string> display_text(const UiState&, const Control&) const { return std::nullopt; }
    // The type's own node properties (accelerators, selection, writing mode, text selection...).
    virtual void describe_node(const UiState&, const Control&, const Semantics&, const NodeRef&, SemanticNode&) const {}
    // The verbs of the node, in order. The default offers focus and the context menu where the control admits them.
    virtual void verbs(const VerbContext&, std::vector<Verb>&) const;
    // A verb arriving at the control or one of its items; nullopt: not this type's verb.
    virtual std::optional<Result> perform(const PerformContext&) const { return std::nullopt; }
    virtual bool confirm_pending(const UiState&, const Control&) const { return false; }
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
};
// The press protocol of a command-like type (a button): Enter runs the component's action, Space arms it for the key
// release, a pointer press arms it for the release over the same component. The release itself is ComponentType::pointer_up's
// default, which invokes the action.
Result command_key_down(UiState& ui, KeyPress key);
void command_pointer_down(UiState& ui);
const ComponentType& component_type(Kind kind);
// The same, or null for a Kind no type is registered for (the one place a stray Kind value is tolerated).
const ComponentType* find_component_type(Kind kind);
// The focus ring's thickness in physical pixels for these settings. Layout reserves at least this much room
// around whatever a ring frames, whether or not the component is focused, so focus never moves anything.
int focus_ring_width(const UiState& ui, const Appearance& appearance, float scale);
// Text flow (#80), in physical pixels. A row is what one display line occupies: the font's line height
// times the line spacing (never less than the line height). The paragraph gap is the extra space after
// a paragraph ended by an LF, scaled from DIP, never rounded up to a pixel when it is 0.
int text_row(int line_height, double line_spacing);
int paragraph_gap(const Control& control, float scale);
// The alignment to draw now (#84): the control's own, except that the user's "justified text as left"
// setting turns justify and distribute into left. Reads of the property itself stay unchanged.
TextAlign drawn_text_align(const Control& control, const PresentationSettings& settings);
// The same rule for an alignment value alone (what UIA reports from a semantic node): the one place that knows it.
TextAlign drawn_text_align(TextAlign align, const PresentationSettings& settings);

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
