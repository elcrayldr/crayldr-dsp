#pragma once
#include "Curve.h"
#include "Grid.h"

namespace crayldr {

struct BuildReport {
    int events = 0, groups = 0, phrases = 0;
    float referenceDb = 0.f, gateDb = 0.f;
};

// Offline, non-causal gain-curve construction from a stored analysis.
// NOT a compressor: gain is built from detected vocal events (plateaus),
// with a restrained zero-mean intra-event ride on voiced nuclei and symmetric
// look-ahead smoothing. Unvoiced regions are protected from positive gain.
// "Events/groups/phrases" are ACOUSTIC units (energy nuclei, short-gap
// clusters, long-gap clusters); no linguistic syllable/word recognition is claimed.
bool buildCurve(const AnalysisGrid& grid, const Settings& settings, CurveData& out,
                BuildReport* report = nullptr);

} // namespace crayldr
