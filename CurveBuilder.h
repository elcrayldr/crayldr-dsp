#pragma once
#include "Curve.h"
#include "Grid.h"

namespace crayldr {

struct BuildReport {
    int events = 0, groups = 0, phrases = 0;
    float referenceDb = 0.f, gateDb = 0.f;
};

// Offline, non-causal gain-curve construction from a stored analysis.
// NOT a compressor: gain is piecewise-constant per detected vocal event
// (plateaus), moved only in the valleys between events with symmetric,
// look-ahead smoothing. Intra-event dynamics are untouched.
// "Events/groups/phrases" are ACOUSTIC units (energy nuclei, short-gap
// clusters, long-gap clusters); no linguistic syllable/word recognition is claimed.
bool buildCurve(const AnalysisGrid& grid, const Settings& settings, CurveData& out,
                BuildReport* report = nullptr);

} // namespace crayldr
