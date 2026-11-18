#pragma once
#include "component_type.hpp"
#include "core_internal.hpp"

// A test-only type supplies its own geometry, painting, hit region and input policy.
class TestComponent final : public isoduo::detail::ComponentType {
public:
    isoduo::ComponentHandle handle(isoduo::detail::UiState& ui, const isoduo::detail::Component& control) const override {
        return ui.typedHandle<isoduo::ButtonHandle>(control);
    }
    void arrange(isoduo::detail::ComponentLayout& layout) const override {
        layout.placement.bounds = {0, 0, 80, 30};
        layout.placement.lines.push_back({{0, 0, 80, 20}, layout.control.value});
    }
    bool hit(const isoduo::Placement& p, int x, int y) const override {
        return p.clip.contains(x, y) && isoduo::Rect{p.bounds.x, p.bounds.y, 40, p.bounds.h}.contains(x, y);
    }
    void paint(isoduo::detail::ComponentPaint& paint) const override {
        paint.fill(paint.placement.bounds, {17, 91, 173});
    }
    isoduo::Result keyDown(isoduo::detail::UiState& ui, isoduo::KeyPress key) const override { return isoduo::detail::commandKeyDown(ui, key); }
    void pointerDown(isoduo::detail::UiState& ui, const isoduo::detail::Component&, std::optional<std::size_t>) const override { isoduo::detail::commandPointerDown(ui); }
};
