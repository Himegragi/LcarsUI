#include "lcars/fonts.hpp"
#include "text.hpp"
#include <iostream>

int main() {
    const auto& catalog = lcars::system_font_catalog();
    std::cout << "Font families: " << catalog.families().size() << '\n';
    if (catalog.families().empty()) return 1;
    lcars::detail::Text text;
    auto layout = text.shape(u"笑😀心❤️", 18);
    if (layout.missing != 0) return 2;
    auto missing = text.shape(u"\ue000", 18);
    if (missing.missing != 1 || missing.width <= 0) return 3;
    lcars::detail::Canvas canvas(64, 64, {255, 255, 255});
    text.draw(canvas, missing, 0, 30, {0, 0, 0});
    bool ink = false;
    for (std::size_t i = 0; i < canvas.pixels.size(); i += 4) {
        if (canvas.pixels[i] != 255) ink = true;
        if (canvas.pixels[i] != canvas.pixels[i + 1] || canvas.pixels[i] != canvas.pixels[i + 2]) return 4;
    }
    if (!ink) return 5;
    return 0;
}
