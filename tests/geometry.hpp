#pragma once
#include "isoduo/layout.hpp"
#include <algorithm>
#include <string_view>

namespace test {
inline bool inside(isoduo::Rect inner, isoduo::Rect outer) {
    return isoduo::intersect(inner, outer) == inner;
}
struct PopupCheck {
    const isoduo::Placement* popup = nullptr;
    bool inside = false;
    bool hit = false;
    explicit operator bool() const { return popup && inside && hit; }
};
// Overlays may extend over the scroll viewport, but must fit in the window client area.
inline PopupCheck popupInClient(const isoduo::Frame& frame, isoduo::Rect client, std::string_view id) {
    auto it = std::find_if(frame.overlays.begin(), frame.overlays.end(), [&](const auto& p) {
        return p.popup && !p.tooltip && p.id == id;
    });
    if (it == frame.overlays.end()) return {};
    auto r = it->bounds;
    auto target = frame.hit(r.x + r.w / 2, r.y + r.h / 2);
    return {&*it, r.w > 0 && r.h > 0 && inside(r, client), target && target->popup && target->id == id};
}
}
