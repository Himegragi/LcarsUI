#include "pointer_routing.hpp"
#include "component_type.hpp"
#include <algorithm>

namespace isoduo::detail {
bool routeMenuPointer(UiState& ui, const Frame& frame, PointerAction action, int x, int y) {
    const auto row = frame.menuItemAt(x, y);
    switch (action) {
    case PointerAction::move:
        if (row) ui.menuHighlight(*row);
        return false;
    case PointerAction::press:
    case PointerAction::rightPress:
        if (!frame.menuContains(x, y)) {
            ui.closeMenu();
            return true;
        }
        if (action == PointerAction::press && row) ui.menuPointerDown(*row);
        return false;
    case PointerAction::release:
        // Releasing over the row that was pressed runs it; separators and disabled items do nothing.
        ui.menuPointerUp(row ? *row : std::string{});
        return false;
    case PointerAction::rightRelease:
        if (row) {
            ui.menuPointerDown(*row);
            ui.menuPointerUp(*row);
        }
        return false;
    }
    return false;
}

std::optional<PointerMenuTarget> pointerMenuTarget(UiState& ui, const Frame& frame, int x, int y) {
    const auto* hit = frame.hit(x, y);
    std::optional<PointerMenuTarget> target;
    if (hit && !hit->dropdownPanel && !hit->tooltip) target = PointerMenuTarget{hit->id, hit->kind, frame.optionAt(*hit, x, y)};
    // Copy what the menu needs before closing the popup, so the target does not depend on frame storage.
    if (!hit || !hit->dropdownPanel) ui.closePopup();
    return target;
}

Result openPointerMenu(UiState& ui, const Frame& frame, const PointerMenuTarget& target, int x, int y) {
    std::optional<unsigned> offset;
    if (componentType(target.kind).selectsText()) offset = placementTextHit(frame, target.id, x, y).offset;
    return ui.menuPointerOpen(target.id, target.option, offset, {x, y});
}

std::optional<ThumbGrab> pressThumb(Ui& ui, const Frame& frame, const Placement* hit, int x, int y) {
    auto& state = access(ui);
    const bool onPanel = hit && hit->dropdownPanel;
    for (auto it = frame.regions.rbegin(); it != frame.regions.rend(); ++it) {
        if (!it->state || state.retiredComponent(it->component)) continue;
        // An open dropdown panel lies above everything else: it takes the press for its own scrollbar only.
        const bool ownPanel = onPanel && hit->component == it->component;
        if (onPanel && !ownPanel) continue;
        // An open dialog blocks the page's scrollbars but not its own. A component on the dialog does not
        // unblock the page's scrollbar underneath it: the press then reaches that component.
        if (frame.modal && !it->modalContent && !ownPanel) continue;
        if (!it->thumb.h || !it->clip.contains(x, y) || !it->track.contains(x, y)) continue;
        const int along = it->horizontal ? x : y;
        const int start = it->horizontal ? it->thumb.x : it->thumb.y;
        const int grabOffset = it->thumb.contains(x, y) ? along - start : (it->horizontal ? it->thumb.w : it->thumb.h) / 2;
        ThumbGrab grab{it->state, it->component, grabOffset};
        frame.scrollTo(ui, *it, it->offsetForThumb(along - grabOffset));
        return grab;
    }
    return std::nullopt;
}

bool dragThumb(Ui& ui, const Frame& frame, const ThumbGrab& grab, int x, int y) {
    auto region = std::find_if(frame.regions.begin(), frame.regions.end(), [&](const ScrollRegion& item) {
        return item.state == grab.state && item.component == grab.component;
    });
    if (region == frame.regions.end()) return false;
    const int along = region->horizontal ? x : y;
    return frame.scrollTo(ui, *region, region->offsetForThumb(along - grab.grabOffset));
}
}
