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
    return 0;
}
