#pragma once
#include "lcars/layout.hpp"
namespace test {
// Deterministic text backend: ASCII is half an em wide, everything else one em, surrogate
// pairs advance once. Lets layout run without fonts. A bold run is a quarter wider.
struct FakeMeasure final : lcars::TextMeasure {
    lcars::TextRun measure(std::u16string_view text, int pixels) override {
        lcars::TextRun run;
        run.stops.push_back({0, 0.f});
        float x = 0;
        for (unsigned i = 0; i < text.size();) {
            bool pair = text[i] >= 0xd800 && text[i] <= 0xdbff && i + 1 < text.size();
            x += text[i] < 0x80 ? pixels * 0.5f : static_cast<float>(pixels);
            i += pair ? 2 : 1;
            run.stops.push_back({i, x});
        }
        run.width = x;
        return run;
    }
    lcars::TextRun measure(std::u16string_view text, int pixels, lcars::FontStyle style) override {
        auto run = measure(text, pixels);
        if (!style.bold) return run;
        for (auto& stop : run.stops) stop.second *= 1.25f;
        run.width *= 1.25f;
        return run;
    }
    int line_height(int pixels) override { return pixels * 3 / 2; }
};
}
