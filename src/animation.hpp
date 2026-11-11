#pragma once
// The framework-internal shared animation clock (#75). A component registers "this display
// property goes from A to B over a duration"; the clock only answers what the value is at the
// time source's current time. It never schedules frames itself: the host asks whether anything
// is still running and requests the next frame only then, so an idle UI never repaints.
// The time source is replaceable so tests can place time anywhere (a fake clock).
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lcars::detail {
class AnimationClock {
public:
    using Time = std::uint64_t;  // milliseconds on the time source's own timeline
    static constexpr Time standard_duration = 150;
    // Cubic ease-out: fast start, gentle stop. 0 -> 0, 1 -> 1, and past half way at half the time.
    static double ease_out(double t) {
        const double rest = 1.0 - std::clamp(t, 0.0, 1.0);
        return 1.0 - rest * rest * rest;
    }
    // Without a time source nothing can be animated: callers see instant changes.
    void time_source(std::function<Time()> source) {
        source_ = std::move(source);
        if (!source_) tracks_.clear();  // without a timeline nothing can still be running
    }
    bool has_time_source() const { return static_cast<bool>(source_); }
    Time now() const { return source_ ? source_() : 0; }
    // Register or retarget. The caller passes where the property is displayed right now, so a
    // retarget turns from the visible position and restarts the timing.
    void start(const std::string& key, double from, double to, Time duration = standard_duration) {
        if (!source_) return;
        tracks_.insert_or_assign(key, Track{from, to, now(), duration});
    }
    // The animated value, or nothing once the track has finished or was never registered.
    std::optional<double> value(std::string_view key) const {
        auto it = tracks_.find(key);
        if (it == tracks_.end()) return std::nullopt;
        const Track& track = it->second;
        const Time at = now();
        const Time elapsed = at > track.begin ? at - track.begin : 0;
        if (elapsed >= track.duration) return std::nullopt;
        return track.from + (track.to - track.from) * ease_out(static_cast<double>(elapsed) / static_cast<double>(track.duration));
    }
    bool active(std::string_view key) const { return value(key).has_value(); }
    void cancel(std::string_view key) { if (auto it = tracks_.find(key); it != tracks_.end()) tracks_.erase(it); }
    // Every running animation ends at once (minimized window, animations switched off).
    void finish_all() { tracks_.clear(); }
private:
    struct Track { double from, to; Time begin, duration; };
    std::function<Time()> source_;
    std::map<std::string, Track, std::less<>> tracks_;
};
}
