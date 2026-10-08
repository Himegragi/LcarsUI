#include "core.hpp"
#include <iostream>
int main() {
    std::cout << "C++26 pack indexing + static library consumer: "
              << verified_cxx26_value() << '\n';
    return verified_cxx26_value() == 26 ? 0 : 1;
}
