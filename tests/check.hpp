#pragma once
// Minimal case runner shared by the behavior suites; no third-party test framework.
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace test {
inline int checks = 0;
inline void check(bool condition, const char* expectation) {
    ++checks;
    if (!condition) throw std::runtime_error(expectation);
}
struct Case { const char* name; void (*body)(); };
template <std::size_t N>
int run(const char* suite, const Case (&cases)[N]) {
    int failures = 0;
    for (const auto& c : cases) {
        try { c.body(); std::cout << "PASS " << c.name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cout << "FAIL " << c.name << ": " << e.what() << '\n'; }
    }
    std::cout << suite << ": " << N - failures << '/' << N << " cases, " << checks << " checks\n";
    return failures ? 1 : 0;
}
}
