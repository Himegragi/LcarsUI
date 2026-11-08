#include "render.hpp"
#include <stdexcept>
using namespace lcars::detail;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    DrawList list;
    list.clear({10, 20, 30, 255});
    list.set_clip({1, 0, 1, 2});
    list.fill_rect({0, 0, 2, 1}, {200, 100, 50, 255});
    auto image = render_cpu(list, 2, 2);
    const std::vector<unsigned char> expected{10, 20, 30, 255, 200, 100, 50, 255,
                                              10, 20, 30, 255, 10, 20, 30, 255};
    require(image.pixels == expected, "top-down pixels and clipping");

    require(command_ink(list, 1, list.commands.size(), {0, 0, 2, 2}, 2, 2), "visible draw command has ink");
    DrawList clipped;
    clipped.set_clip({0, 0, 0, 0});
    clipped.fill_rect({0, 0, 2, 2}, {255, 255, 255, 255});
    require(!command_ink(clipped, 0, clipped.commands.size(), {0, 0, 2, 2}, 2, 2), "empty clip has no ink");

    DrawList translucent;
    translucent.clear({200, 100, 50, 255});
    translucent.fill_rect({0, 0, 1, 1}, {0, 0, 0, 110});
    auto blended = render_cpu(translucent, 1, 1);
    require(blended.pixels == std::vector<unsigned char>{113, 56, 28, 255}, "alpha blending");

    DrawList transparent;
    transparent.clear({0, 0, 0, 0});
    transparent.fill_rect({0, 0, 1, 1}, {255, 0, 0, 128});
    auto partial = render_cpu(transparent, 2, 1);
    require(partial.pixels == std::vector<unsigned char>{128, 0, 0, 128, 0, 0, 0, 0}, "transparent clear and resulting alpha");

    DrawList rounded;
    rounded.clear({0, 0, 0, 255});
    rounded.rounded_rect({0, 0, 4, 4}, 2, {255, 255, 255, 255});
    auto corner = render_cpu(rounded, 4, 4);
    require(corner.pixels[0] == 96 && corner.pixels[4] == 234 && corner.pixels[20] == 255,
            "rounded corner coverage and solid interior");
}
