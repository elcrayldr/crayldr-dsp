#pragma once
#include "Analyzer.h"
#include "CurveBuilder.h"
#include "Serialization.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace crayldr {

struct TransportInfo {
    bool    playing = false;
    bool    positionValid = false;
    int64_t position = 0;          // absolute host timeline sample of the first sample of the block
    bool    offlineRender = false; // host is rendering offline; position is still required for curve alignment
};

// Thread model
//  * processBlock(): audio thread only. No locks, no allocation, no syscalls.
//  * Everything else: control (message) thread, serialised by an internal mutex.
//  * Control -> audio hand-off uses a run-mode flag + "quiescence" handshake
//    (busy_/seq_) so the control thread never mutates data the audio thread may
//    be reading, and retired curve snapshots are freed only when provably unused.
class VoxLevelerCore {
public:
    VoxLevelerCore() = default;
    ~VoxLevelerCore() = default;

    // ---- control thread ----
    void prepare(double sampleRate);
    bool startAnalysis();                       // clears previous analysis + curve
    bool stopAnalysis();                        // back to idle, keeps analysis
    bool rebuildCurve(const Settings& s);       // false if no usable voice / analyzing
    void clearAll();
    std::vector<uint8_t> saveState();
    bool loadState(const uint8_t* data, std::size_t size);
    const CurveSnapshot* snapshotForUI() const noexcept { return owned_.get(); }   // control thread only
    const AnalysisGrid& gridForUI() const noexcept { return grid_; }               // only when !isAnalyzing()
    BuildReport lastReport() const { return report_; }

    // ---- audio thread ----
    void processBlock(float* const* ch, int numChannels, int numSamples,
                      const TransportInfo& t, bool applyEnabled) noexcept;

    // ---- status (any thread) ----
    bool isAnalyzing() const noexcept { return runMode_.load() == kAnalyzing; }
    bool hasCurve() const noexcept { return active_.load() != nullptr; }
    bool consumeStopRequest() noexcept { return stopRequested_.exchange(false); }
    int64_t coveredHops() const noexcept { return coveredHops_.load(std::memory_order_relaxed); }
    int64_t lastPosition() const noexcept { return lastPosition_.load(std::memory_order_relaxed); }
    bool truncated() const noexcept { return truncated_.load(std::memory_order_relaxed); }
    bool isStale() const noexcept { return compared_.load() >= 400 && staleEma_.load() > 0.30f; }
    uint64_t appliedSamples() const noexcept { return appliedSamples_.load(std::memory_order_relaxed); }
    double sampleRate() const noexcept { return sr_; }

private:
    enum { kIdle = 0, kAnalyzing = 1, kLocked = 2 };
    bool waitQuiescent(int timeoutMs);
    bool lockAudio(int& previous);
    void unlockAudio(int mode) noexcept { runMode_.store(mode); }
    void publishCurve(std::unique_ptr<CurveSnapshot> next);

    double sr_ = 48000.0;
    Analyzer analyzer_;
    AnalysisGrid grid_;
    BuildReport report_;

    std::atomic<int> runMode_{kIdle};
    std::atomic<bool> busy_{false};
    std::atomic<uint64_t> seq_{0};
    std::atomic<const CurveSnapshot*> active_{nullptr};
    std::unique_ptr<CurveSnapshot> owned_;
    std::vector<std::unique_ptr<CurveSnapshot>> retired_;
    std::mutex ctrl_;

    bool wasActive_ = false;                  // audio thread only
    std::atomic<bool> stopRequested_{false};
    std::atomic<int64_t> coveredHops_{0}, lastPosition_{0};
    std::atomic<bool> truncated_{false};
    std::atomic<float> staleEma_{0.0f};
    std::atomic<int> compared_{0};
    std::atomic<uint64_t> appliedSamples_{0};
};

} // namespace crayldr
