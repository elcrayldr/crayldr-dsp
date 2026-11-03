#pragma once
#include "Types.h"

namespace crayldr {

// Position-indexed store of per-hop features. Seeks, loops and punch-in
// re-analysis simply overwrite/extend hops. Memory is allocated once, off the
// audio thread (allocate()); set() never allocates.
class AnalysisGrid {
public:
    void allocate() { if (hops_.empty()) hops_.assign(static_cast<std::size_t>(kMaxHops), HopFeatures{}); }
    bool allocated() const noexcept { return !hops_.empty(); }
    std::size_t capacity() const noexcept { return hops_.size(); }

    void clear() noexcept {
        if (covered_ > 0 && first_ <= last_)
            for (int64_t h = first_; h <= last_; ++h) hops_[static_cast<std::size_t>(h)] = HopFeatures{};
        first_ = std::numeric_limits<int64_t>::max(); last_ = -1; covered_ = 0;
    }
    // 0 = out of range, 1 = newly covered, 2 = overwritten
    int set(int64_t h, const HopFeatures& f) noexcept {
        if (h < 0 || h >= static_cast<int64_t>(hops_.size())) return 0;
        HopFeatures& d = hops_[static_cast<std::size_t>(h)];
        const int r = d.covered() ? 2 : 1;
        d = f;
        if (r == 1) { ++covered_; if (h < first_) first_ = h; if (h > last_) last_ = h; }
        return r;
    }
    const HopFeatures& at(int64_t h) const noexcept { return hops_[static_cast<std::size_t>(h)]; }
    int64_t first() const noexcept { return first_; }
    int64_t last() const noexcept { return last_; }
    int64_t covered() const noexcept { return covered_; }

private:
    std::vector<HopFeatures> hops_;
    int64_t first_ = std::numeric_limits<int64_t>::max(), last_ = -1, covered_ = 0;
};

} // namespace crayldr
