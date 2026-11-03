#include "VoxLevelerCore.h"
#include <chrono>
#include <thread>

namespace crayldr {

// ------------------------------ control thread ------------------------------

bool VoxLevelerCore::waitQuiescent(int timeoutMs) {
    if (!busy_.load()) return true;
    const uint64_t s = seq_.load();
    const auto t0 = std::chrono::steady_clock::now();
    while (busy_.load() && seq_.load() == s) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(timeoutMs)) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

bool VoxLevelerCore::lockAudio(int& previous) {
    previous = runMode_.exchange(kLocked);
    if (!waitQuiescent(2000)) { runMode_.store(previous); return false; }
    return true;
}

void VoxLevelerCore::publishCurve(std::unique_ptr<CurveSnapshot> next) {
    std::unique_ptr<CurveSnapshot> old = std::move(owned_);
    owned_ = std::move(next);
    active_.store(owned_.get());
    if (waitQuiescent(2000)) { old.reset(); retired_.clear(); }
    else if (old) retired_.push_back(std::move(old));   // never free what audio might still read
}

void VoxLevelerCore::prepare(double sampleRate) {
    std::lock_guard<std::mutex> lk(ctrl_);
    int prev;
    if (!lockAudio(prev)) return;
    sr_ = sampleRate;
    analyzer_.prepare(sampleRate);
    unlockAudio(prev);
}

bool VoxLevelerCore::startAnalysis() {
    std::lock_guard<std::mutex> lk(ctrl_);
    int prev;
    if (!lockAudio(prev)) return false;
    grid_.allocate();
    grid_.clear();
    coveredHops_.store(0); truncated_.store(false); stopRequested_.store(false);
    staleEma_.store(0.0f); compared_.store(0);
    analyzer_.resetStream();
    publishCurve(nullptr);
    unlockAudio(kAnalyzing);
    return true;
}

bool VoxLevelerCore::stopAnalysis() {
    std::lock_guard<std::mutex> lk(ctrl_);
    int prev;
    if (!lockAudio(prev)) return false;
    analyzer_.resetStream();
    unlockAudio(kIdle);
    return true;
}

bool VoxLevelerCore::rebuildCurve(const Settings& s) {
    std::lock_guard<std::mutex> lk(ctrl_);
    if (runMode_.load() == kAnalyzing) return false;
    CurveData data; BuildReport rep;
    const bool ok = buildCurve(grid_, s, data, &rep);   // reads grid_ only; audio never writes it outside kAnalyzing
    report_ = rep;
    publishCurve(ok ? CurveSnapshot::create(data) : nullptr);
    staleEma_.store(0.0f); compared_.store(0);
    return ok;
}

void VoxLevelerCore::clearAll() {
    std::lock_guard<std::mutex> lk(ctrl_);
    int prev;
    if (!lockAudio(prev)) return;
    grid_.clear(); coveredHops_.store(0); truncated_.store(false); report_ = BuildReport{};
    publishCurve(nullptr);
    unlockAudio(kIdle);
}

std::vector<uint8_t> VoxLevelerCore::saveState() {
    std::lock_guard<std::mutex> lk(ctrl_);
    CurveData cd;
    if (owned_) { cd.firstHop = owned_->firstHop(); cd.gainDb = owned_->gainDb(); }
    if (runMode_.load() == kAnalyzing) {
        int prev;
        if (!lockAudio(prev)) return {};
        auto out = serializeState(grid_, cd, sr_);
        unlockAudio(prev);
        return out;
    }
    return serializeState(grid_, cd, sr_);
}

bool VoxLevelerCore::loadState(const uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lk(ctrl_);
    int prev;
    if (!lockAudio(prev)) return false;
    grid_.allocate();
    grid_.clear();
    CurveData cd; double rate = 0;
    const bool ok = deserializeState(data, size, grid_, cd, rate);
    if (!ok) grid_.clear();
    coveredHops_.store(grid_.covered()); truncated_.store(false);
    publishCurve(ok ? CurveSnapshot::create(cd) : nullptr);
    staleEma_.store(0.0f); compared_.store(0);
    unlockAudio(kIdle);
    return ok;
}

// -------------------------------- audio thread --------------------------------

void VoxLevelerCore::processBlock(float* const* ch, int nch, int n, const TransportInfo& t,
                                  bool applyEnabled) noexcept {
    busy_.store(true);
    const int rm = runMode_.load();
    const bool active = t.playing && t.positionValid && n > 0 && nch > 0;
    if (active) lastPosition_.store(t.position + n, std::memory_order_relaxed);

    if (rm == kAnalyzing) {
        if (active) {
            analyzer_.process(ch, nch, n, t.position, [this](int64_t h, const HopFeatures& f) {
                const int r = grid_.set(h, f);
                if (r == 1) coveredHops_.fetch_add(1, std::memory_order_relaxed);
                else if (r == 0) truncated_.store(true, std::memory_order_relaxed);
            });
        } else if (wasActive_) {
            stopRequested_.store(true);          // transport stopped while analysing
        }
    } else if (rm == kIdle && active && applyEnabled) {
        const CurveSnapshot* s = active_.load();
        if (s != nullptr) {
            if (grid_.covered() > 0) {           // stale-analysis monitor: compares INPUT (pre-gain) with stored analysis
                analyzer_.process(ch, nch, n, t.position, [this](int64_t h, const HopFeatures& f) {
                    if (h < 0 || h >= static_cast<int64_t>(grid_.capacity())) return;
                    const HopFeatures& st = grid_.at(h);
                    if (!st.covered()) return;
                    if (st.rmsDb > -50.0f || f.rmsDb > -50.0f) {
                        const float mm = std::fabs(st.rmsDb - f.rmsDb) > 4.0f ? 1.0f : 0.0f;
                        staleEma_.store(0.99f * staleEma_.load(std::memory_order_relaxed) + 0.01f * mm, std::memory_order_relaxed);
                        const int c = compared_.load(std::memory_order_relaxed);
                        if (c < 1000000000) compared_.store(c + 1, std::memory_order_relaxed);
                    }
                });
            }
            s->apply(ch, nch, n, t.position, sr_);          // the ONLY place the curve is applied
            appliedSamples_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        }
    }
    wasActive_ = active;
    seq_.fetch_add(1);
    busy_.store(false);
}

} // namespace crayldr
