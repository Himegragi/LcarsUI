#include "perf.hpp"
int main() {
    isoduo::detail::PerfSamples samples;
    samples.add(100, 10); samples.add(1200, 20); samples.add(1300, 30);
    auto warm = samples.summary(true), steady = samples.summary(false);
    if (warm.count != 1 || warm.maximum != 10 || warm.overBudget != 0) return 1;
    if (steady.count != 2 || steady.p50 != 20 || steady.p95 != 30 || steady.maximum != 30 || steady.overBudget != 2) return 2;
    if (!isoduo::detail::idlePass(0, 0, 20) || isoduo::detail::idlePass(1, 0, 0) || isoduo::detail::idlePass(0, 1, 0) || isoduo::detail::idlePass(0, 0, 20.01)) return 3;
    // #73: the same warm-up split, kept per stage so a slow drag can be blamed on layout, list building or the GPU.
    isoduo::detail::PerfBreakdown stages;
    stages.add(100, 1, 2, 3); stages.add(1200, 4, 5, 6); stages.add(1300, 8, 9, 10);
    auto stageWarm = stages.summary(true), stageSteady = stages.summary(false);
    if (stageWarm.layout.count != 1 || stageWarm.layout.maximum != 1 || stageWarm.gpu.maximum != 3) return 4;
    if (stageSteady.layout.count != 2 || stageSteady.layout.p50 != 4 || stageSteady.layout.maximum != 8) return 5;
    if (stageSteady.list.p95 != 9 || stageSteady.list.maximum != 9 || stageSteady.gpu.p50 != 6 || stageSteady.gpu.maximum != 10) return 6;
}
