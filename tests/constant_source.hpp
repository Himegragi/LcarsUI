#pragma once
#include "isoduo/core.hpp"

namespace test {
template <class T> isoduo::Source<T> constantSource(T value) {
    return {[value] { return value; }, [](std::function<void()>) { return [] {}; }};
}
}
