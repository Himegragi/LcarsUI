#pragma once
// What a focus ring and an enlarged slider thumb look like in a paint, defined once (CODING_STANDARDS: a property asserted
// through more than one test seam uses one shared predicate). Tests with their own paint probes (focusRing, focusVisibility,
// animation) feed these what they recorded.
#include "lcars/appearance.hpp"
#include <algorithm>
#include <cmath>
namespace focusMarks {
// A focus ring is two rounded fills both marked focusIndicator: the band in the ring color, then what it frames.
inline constexpr int fillsPerRing = 2;
// How many whole rings this many focusIndicator fills make; -1 when they do not come in whole rings (a leftover strip, bar or dot).
inline int rings(int markedFills) { return markedFills >= 0 && markedFills % fillsPerRing == 0 ? markedFills / fillsPerRing : -1; }
inline bool isOneRing(int markedFills) { return rings(markedFills) == 1; }
// The slider thumb among the plain (content role) rounded fills of a slider's paint: the one square fill wider than the track's bar.
inline bool isThumb(lcars::Rect fill) { return fill.w == fill.h && fill.w > 8; }
// The thumb's width in pixels: the appearance's body spacing while the focus indicator shows, its row padding otherwise.
inline int scaled(float scale, int dip) { return std::max(1, static_cast<int>(std::lround(dip * scale))); }
inline int enlargedThumb(const lcars::Appearance& appearance, float scale = 1.f) { return scaled(scale, appearance.componentPresentation().bodySpacing); }
inline int plainThumb(const lcars::Appearance& appearance, float scale = 1.f) { return scaled(scale, appearance.componentPresentation().rowPadding); }
}
