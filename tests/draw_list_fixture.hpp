#pragma once

#include "render.hpp"

namespace test {
using namespace isoduo;
inline detail::DrawList layeredDrawList() {
    detail::RecordingCanvas canvas(8, 6, {});
    auto& list = canvas.list;
    list.clear({0, 0, 0, 0});
    list.setClip({0, 0, 7, 6});
    list.fillRect({0, 0, 8, 2}, {200, 100, 50, 255});
    list.roundedRect({0, 2, 4, 4}, 2, {255, 255, 255, 255});
    canvas.clip = isoduo::intersect({0, 0, 7, 6}, {1, 0, 5, 6});
    auto page = std::make_shared<detail::AtlasPage>();
    page->width = page->height = 2;
    page->pixels = {64, 255, 0, 128};
    canvas.glyph({4, 1, 2, 2}, {0, 0, 255}, page, {0, 0, 2, 2});
    canvas.overlay({5, 0, 3, 3}, 0, {0, 255, 0}, 128);
    // More distinct pages than the OpenGL renderer's upload slots.
    for (unsigned char coverage : {255, 0, 0, 0, 128}) {
        auto batch = std::make_shared<detail::AtlasPage>();
        batch->width = batch->height = 1;
        batch->pixels = {coverage};
        canvas.glyph({3, 0, 1, 1}, coverage == 255 ? Color{255, 0, 0} : Color{0, 255, 0},
                     batch, {0, 0, 1, 1});
    }
    canvas.clip = isoduo::intersect(canvas.clip, {7, 0, 1, 6});
    canvas.fill({0, 0, 8, 6}, {255, 0, 255});
    return std::move(list);
}

inline bool matchesLayeredDrawList(const detail::Image& image) {
    if (image.width != 8 || image.height != 6 || image.pixels.size() != 8 * 6 * 4) return false;
    const auto pixel = [&](int x, int y, detail::Rgba color) {
        const auto offset = (y * 8 + x) * 4;
        return image.pixels[offset] == color.r && image.pixels[offset + 1] == color.g &&
            image.pixels[offset + 2] == color.b && image.pixels[offset + 3] == color.a;
    };
    return pixel(0, 0, {200, 100, 50, 255}) && pixel(7, 0, {0, 0, 0, 0}) &&
        pixel(0, 2, {96, 96, 96, 96}) && pixel(1, 3, {255, 255, 255, 255}) &&
        pixel(3, 0, {127, 128, 0, 255}) &&
        pixel(4, 1, {149, 74, 101, 255}) && pixel(5, 1, {0, 128, 127, 255}) &&
        pixel(4, 2, {0, 0, 0, 0}) && pixel(5, 2, {0, 128, 63, 191}) &&
        pixel(6, 1, {200, 100, 50, 255}) && pixel(6, 2, {0, 0, 0, 0});
}
}
