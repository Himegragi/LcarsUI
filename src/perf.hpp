#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace lcars::detail {
struct PerfSummary { size_t count=0, over_budget=0; double p50=0, p95=0, p99=0, maximum=0; };
class PerfSamples {
    std::vector<double> warm_, steady_;
public:
    void add(double age_ms, double frame_ms) { (age_ms < 1000 ? warm_ : steady_).push_back(frame_ms); }
    void clear() { warm_.clear(); steady_.clear(); }
    PerfSummary summary(bool warm) const {
        auto values = warm ? warm_ : steady_; PerfSummary result; result.count=values.size();
        if (values.empty()) return result;
        std::sort(values.begin(), values.end());
        auto percentile=[&](double p) { return values[static_cast<size_t>(std::ceil(p*values.size()))-1]; };
        result.p50=percentile(.50); result.p95=percentile(.95); result.p99=percentile(.99); result.maximum=values.back();
        result.over_budget=static_cast<size_t>(std::count_if(values.begin(),values.end(),[](double v){return v>16.7;}));
        return result;
    }
};
// Per-stage frame times: CPU layout, display-list building, and submission plus GPU.
struct PerfStageSummary { size_t count=0; double p50=0, p95=0, p99=0, maximum=0; };
struct PerfBreakdownSummary { PerfStageSummary layout, list, gpu; };
class PerfBreakdown {
    PerfSamples layout_, list_, gpu_;
    static PerfStageSummary stage(const PerfSamples& samples, bool warm) {
        auto all=samples.summary(warm); return {all.count, all.p50, all.p95, all.p99, all.maximum};
    }
public:
    void add(double age_ms, double layout_ms, double list_ms, double gpu_ms) { layout_.add(age_ms,layout_ms); list_.add(age_ms,list_ms); gpu_.add(age_ms,gpu_ms); }
    void clear() { layout_.clear(); list_.clear(); gpu_.clear(); }
    PerfBreakdownSummary summary(bool warm) const { return {stage(layout_,warm), stage(list_,warm), stage(gpu_,warm)}; }
};
inline bool idle_pass(int presents, int paints, double cpu_ms) { return presents==0 && paints==0 && cpu_ms<=20; }
}
