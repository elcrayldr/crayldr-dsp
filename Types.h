#pragma once
// CRAY LDR VOX LEVELER - core types. No JUCE dependency.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace crayldr {

// The analysis/gain grid is defined in TIME (5 ms hops), not in samples, so a
// stored analysis is valid at any project sample rate.
constexpr double  kHopsPerSecond = 200.0;
constexpr double  kHopSeconds    = 1.0 / kHopsPerSecond;
constexpr int64_t kMaxHops       = 30LL * 60LL * 200LL;   // 30 minutes of timeline
constexpr float   kUncovered     = -1000.0f;

enum class Mode { Natural = 0, Extreme = 1 };

struct Settings {
    Mode  mode             = Mode::Natural;
    float amount           = 1.0f;    // 0..1 global scale of all corrections
    float rangeDb          = 12.0f;    // max boost and max cut (dB)
    float ceilingDb        = -0.5f;   // boosts never push a peak above this
    float noiseFloorDb     = -60.0f;  // absolute lower bound of the voice gate
    float targetPercentile = 0.5f;    // reference level = percentile of event levels
};

struct HopFeatures {
    float rmsDb  = kUncovered;
    float peakDb = kUncovered;
    float hf     = 0.0f;   // fraction of energy above ~4 kHz
    float lf     = 0.0f;   // fraction of energy below ~800 Hz
    bool covered() const noexcept { return rmsDb > -999.0f; }
};

inline int64_t hopStartSample(int64_t hop, double sr) noexcept {
    return static_cast<int64_t>(std::ceil(static_cast<double>(hop) * sr / kHopsPerSecond));
}
inline int64_t hopIndexOfSample(int64_t p, double sr) noexcept {
    int64_t h = static_cast<int64_t>(std::floor(static_cast<double>(p) * kHopsPerSecond / sr));
    while (hopStartSample(h + 1, sr) <= p) ++h;
    while (hopStartSample(h, sr) > p) --h;
    return h;
}
inline float toDb(float x) noexcept { return 20.0f * std::log10(std::max(x, 1.0e-6f)); }

} // namespace crayldr
