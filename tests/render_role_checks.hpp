#pragma once

#include "renderer.hpp"
#include <algorithm>

namespace test {
inline bool hasPaintRole(const isoduo::detail::GpuComparison& result, std::string_view owner,
                         isoduo::detail::PaintRole role, bool requirePopup = false) {
    return std::any_of(result.regions.begin(), result.regions.end(), [&](const auto& region) {
        return region.owner == owner && (region.role & static_cast<unsigned>(role)) &&
            region.passed && (!requirePopup || region.popup);
    });
}

inline bool matchesVerticalLabel(const isoduo::detail::GpuComparison& result, const isoduo::Placement* label) {
    if (!label || !label->vertical) return false;
    const auto& reference = result.reference;
    auto box = isoduo::intersect(isoduo::intersect(label->bounds, label->clip),
                                 {0, 0, reference.width, reference.height});
    if (box.w <= 0 || box.h <= 0) return false;
    auto same = isoduo::detail::compareImages(reference, result.actual, {box});
    auto index = [&](int x, int y) { return (static_cast<std::size_t>(y) * reference.width + x) * 4; };
    const auto background = reference.pixels[index(box.x, box.y)];
    int ink = 0;
    for (int y = box.y; y < box.y + box.h; ++y)
        for (int x = box.x; x < box.x + box.w; ++x)
            ink += reference.pixels[index(x, y)] != background;
    return same.regionsOk && ink > box.w * box.h / 40;
}
}
