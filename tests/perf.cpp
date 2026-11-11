#include "perf.hpp"
int main() {
    lcars::detail::PerfSamples samples;
    samples.add(100, 10); samples.add(1200, 20); samples.add(1300, 30);
    auto warm = samples.summary(true), steady = samples.summary(false);
    if (warm.count != 1 || warm.maximum != 10 || warm.over_budget != 0) return 1;
    if (steady.count != 2 || steady.p50 != 20 || steady.p95 != 30 || steady.maximum != 30 || steady.over_budget != 2) return 2;
    if (!lcars::detail::idle_pass(0, 0, 20) || lcars::detail::idle_pass(1, 0, 0) || lcars::detail::idle_pass(0, 1, 0) || lcars::detail::idle_pass(0, 0, 20.01)) return 3;
    // #73: the same warm-up split, kept per stage so a slow drag can be blamed on layout, list building or the GPU.
    lcars::detail::PerfBreakdown stages;
    stages.add(100, 1, 2, 3); stages.add(1200, 4, 5, 6); stages.add(1300, 8, 9, 10);
    auto stage_warm = stages.summary(true), stage_steady = stages.summary(false);
    if (stage_warm.layout.count != 1 || stage_warm.layout.maximum != 1 || stage_warm.gpu.maximum != 3) return 4;
    if (stage_steady.layout.count != 2 || stage_steady.layout.p50 != 4 || stage_steady.layout.maximum != 8) return 5;
    if (stage_steady.list.p95 != 9 || stage_steady.list.maximum != 9 || stage_steady.gpu.p50 != 6 || stage_steady.gpu.maximum != 10) return 6;
}
