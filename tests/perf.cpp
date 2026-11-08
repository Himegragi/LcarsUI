#include "perf.hpp"
int main() {
    lcars::detail::PerfSamples samples;
    samples.add(100, 10); samples.add(1200, 20); samples.add(1300, 30);
    auto warm = samples.summary(true), steady = samples.summary(false);
    if (warm.count != 1 || warm.maximum != 10 || warm.over_budget != 0) return 1;
    if (steady.count != 2 || steady.p50 != 20 || steady.p95 != 30 || steady.maximum != 30 || steady.over_budget != 2) return 2;
    if (!lcars::detail::idle_pass(0, 0, 20) || lcars::detail::idle_pass(1, 0, 0) || lcars::detail::idle_pass(0, 1, 0) || lcars::detail::idle_pass(0, 0, 20.01)) return 3;
}
