#pragma once
#include "Types.h"
#include <memory>

namespace crayldr {

struct CurveData {
    int64_t firstHop = 0;          // timeline hop of gainDb[0]
    std::vector<float> gainDb;     // one node per 5 ms hop
    bool empty() const noexcept { return gainDb.empty(); }
};

// Immutable once published. The audio thread only reads it.
class CurveSnapshot {
public:
    static std::unique_ptr<CurveSnapshot> create(const CurveData& d) {
        if (d.gainDb.size() < 2) return nullptr;
        std::unique_ptr<CurveSnapshot> s(new CurveSnapshot());
        s->firstHop_ = d.firstHop;
        s->db_ = d.gainDb;
        s->lin_.resize(d.gainDb.size());
        for (std::size_t i = 0; i < d.gainDb.size(); ++i)
            s->lin_[i] = std::pow(10.0f, d.gainDb[i] / 20.0f);
        return s;
    }
    int64_t firstHop() const noexcept { return firstHop_; }
    std::size_t size() const noexcept { return lin_.size(); }
    const std::vector<float>& gainDb() const noexcept { return db_; }

    // Linear gain at an absolute timeline sample. Pure function of (p, sr).
    float gainLinearAt(int64_t p, double sr) const noexcept {
        return gainAtV(static_cast<double>(p) * (kHopsPerSecond / sr) - static_cast<double>(firstHop_));
    }
    // Multiplies every channel by the curve. Gain depends only on the absolute
    // sample position, so the result is independent of block partitioning.
    void apply(float* const* ch, int nch, int n, int64_t pos, double sr) const noexcept {
        const double hps = kHopsPerSecond / sr;
        const double fh = static_cast<double>(firstHop_);
        for (int i = 0; i < n; ++i) {
            const float g = gainAtV(static_cast<double>(pos + i) * hps - fh);
            for (int c = 0; c < nch; ++c) ch[c][i] *= g;
        }
    }

private:
    CurveSnapshot() = default;
    float gainAtV(double v) const noexcept {
        if (v <= 0.0 || v >= static_cast<double>(lin_.size() - 1)) return 1.0f;   // 0 dB outside
        const int idx = static_cast<int>(v);
        const float fr = static_cast<float>(v - idx);
        return lin_[static_cast<std::size_t>(idx)] +
               (lin_[static_cast<std::size_t>(idx) + 1] - lin_[static_cast<std::size_t>(idx)]) * fr;
    }
    int64_t firstHop_ = 0;
    std::vector<float> db_, lin_;
};

} // namespace crayldr
