#pragma once
// What a focus ring and an enlarged slider thumb look like in a paint, defined once (CODING_STANDARDS: a property asserted
// through more than one test seam uses one shared predicate). Tests with their own paint probes (focus_ring, focus_visibility,
// animation) feed these what they recorded.
#include "lcars/appearance.hpp"
#include <algorithm>
#include <cmath>
namespace focus_marks {
// A focus ring is two rounded fills both marked focus_indicator: the band in the ring color, then what it frames.
inline constexpr int fills_per_ring = 2;
// How many whole rings this many focus_indicator fills make; -1 when they do not come in whole rings (a leftover strip, bar or dot).
inline int rings(int marked_fills) { return marked_fills >= 0 && marked_fills % fills_per_ring == 0 ? marked_fills / fills_per_ring : -1; }
inline bool is_one_ring(int marked_fills) { return rings(marked_fills) == 1; }
// The slider thumb among the plain (content role) rounded fills of a slider's paint: the one square fill wider than the track's bar.
inline bool is_thumb(lcars::Rect fill) { return fill.w == fill.h && fill.w > 8; }
// The thumb's width in pixels: the appearance's body spacing while the focus indicator shows, its row padding otherwise.
inline int scaled(float scale, int dip) { return std::max(1, static_cast<int>(std::lround(dip * scale))); }
inline int enlarged_thumb(const lcars::Appearance& appearance, float scale = 1.f) { return scaled(scale, appearance.component_presentation().body_spacing); }
inline int plain_thumb(const lcars::Appearance& appearance, float scale = 1.f) { return scaled(scale, appearance.component_presentation().row_padding); }
}
