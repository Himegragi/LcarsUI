#pragma once
#include "lcars/core.hpp"

namespace test {
template <class T> lcars::Source<T> constantSource(T value) {
    return {[value] { return value; }, [](std::function<void()>) { return [] {}; }};
}
}
