#pragma once
// Pointer routing that every desktop host shares (ADR 0016: hit and component behavior belong to shared UI).
// A host translates its native events into these calls in client pixels and keeps only what is native:
// capture, window focus, touch filtering, timers and IME.
#include "core_internal.hpp"
#include <optional>

namespace isoduo::detail {
enum class PointerAction { move, press, release, rightPress, rightRelease };

// While the context menu is open it takes every pointer action. Moving highlights the row under the pointer,
// a press and release on one row runs it, and a right release on a row runs it at once. A press of either
// button outside the menu only closes it: the function returns true and the host swallows the matching
// release, so the click reaches nothing underneath.
bool routeMenuPointer(UiState& ui, const Frame& frame, PointerAction action, int x, int y);

// The component a right click opens a context menu on, read before anything can arrange the frame again.
struct PointerMenuTarget {
    Id id;
    ComponentKind kind = ComponentKind::label;
    std::optional<std::size_t> option;  // the item under the pointer
};
// A right click at a point with no menu open. Closes a dropdown the point is not in, then names the component
// whose context menu opens there; none over empty space, a dropdown panel or a tooltip.
std::optional<PointerMenuTarget> pointerMenuTarget(UiState& ui, const Frame& frame, int x, int y);
// Opens the target's context menu at the point. A text field also gets the text position under the point,
// on the line the pointer is over.
Result openPointerMenu(UiState& ui, const Frame& frame, const PointerMenuTarget& target, int x, int y);

// A scrollbar thumb held by the pointer. grabOffset is the distance from the thumb's start to the pointer.
struct ThumbGrab {
    ScrollState* state = nullptr;
    ComponentHandle component;
    int grabOffset = 0;
};
// A left press on a scrollbar track. A press on the thumb grabs it where it is; a press elsewhere on the
// track centers the thumb on the pointer. The innermost region under the point wins. An open dialog blocks
// the page's scrollbars, not its own; an open dropdown panel takes the press for its own scrollbar only.
// Returns no grab when no scrollbar takes the press, which then goes to the component under it.
std::optional<ThumbGrab> pressThumb(Ui& ui, const Frame& frame, const Placement* hit, int x, int y);
// Moves a held thumb with the pointer. True when the region scrolled.
bool dragThumb(Ui& ui, const Frame& frame, const ThumbGrab& grab, int x, int y);
}
