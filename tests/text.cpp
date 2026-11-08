#include "lcars/fonts.hpp"
#include <iostream>

int main() {
    const auto& catalog = lcars::system_font_catalog();
    std::cout << "Font families: " << catalog.families().size() << '\n';
    if (catalog.families().empty()) return 1;
    return 0;
}
