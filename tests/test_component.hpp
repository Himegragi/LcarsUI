#pragma once
#include "component_type.hpp"
#include "core_internal.hpp"

// A test-only type supplies its own geometry, painting, hit region and input policy.
class TestComponent final : public lcars::detail::ComponentType {
public:
    lcars::ComponentHandle handle(lcars::detail::UiState& ui, const lcars::Control& control) const override {
        return ui.button_layout_handle(control);
    }
    void arrange(lcars::detail::ComponentLayout& layout) const override {
        layout.placement.bounds = {0, 0, 80, 30};
        layout.placement.lines.push_back({{0, 0, 80, 20}, layout.control.value});
    }
    bool hit(const lcars::Placement& p, int x, int y) const override {
        return p.clip.contains(x, y) && lcars::Rect{p.bounds.x, p.bounds.y, 40, p.bounds.h}.contains(x, y);
    }
    void paint(lcars::detail::ComponentPaint& paint) const override {
        paint.fill(paint.placement.bounds, {17, 91, 173});
    }
    lcars::Result key_down(lcars::detail::UiState& ui, lcars::KeyPress key) const override { return ui.command_key_down(key); }
    void pointer_down(lcars::detail::UiState& ui) const override { ui.command_pointer_down(); }
};
