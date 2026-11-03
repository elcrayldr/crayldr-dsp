#pragma once
#include "Types.h"

namespace crayldr {

// Streaming feature extractor. Real-time safe: no allocation, no locks.
// Emits one HopFeatures per COMPLETE 5 ms hop. A hop that was only partially
// observed (seek landed mid-hop, block ended at a discontinuity) is discarded,
// never stored half-filled. Any position discontinuity resets the stream.
class Analyzer {
public:
    void prepare(double sampleRate) {
        sr_ = sampleRate;
        const double twoPi = 6.283185307179586;
        a800_ = static_cast<float>(1.0 - std::exp(-twoPi * 800.0 / sr_));
        a4k_  = static_cast<float>(1.0 - std::exp(-twoPi * 4000.0 / sr_));
        resetStream();
    }
    void resetStream() noexcept {
        hopValid_ = false; expectedNext_ = -1;
        for (auto& s : st_) s = State{};
    }

    // Sink: void(int64_t hopIndex, const HopFeatures&)
    template <class Sink>
    void process(const float* const* ch, int nch, int n, int64_t pos, Sink&& sink) noexcept {
        if (n <= 0 || nch <= 0 || sr_ <= 0.0) return;
        nch = std::min(nch, 2);
        if (pos != expectedNext_) resetStream();
        expectedNext_ = pos + n;
        int i = 0;
        if (pos < 0) { i = static_cast<int>(std::min<int64_t>(n, -pos)); hopValid_ = false; }
        while (i < n) {
            const int64_t p = pos + i;
            if (!hopValid_) beginHop(p);
            const int seg = static_cast<int>(std::min<int64_t>(n - i, curEnd_ - p));
            accumulate(ch, nch, i, seg);
            i += seg;
            if (pos + i == curEnd_) {
                if (accFromStart_ && accCount_ == curEnd_ - curStart_) sink(curHop_, finish(nch));
                advanceHop();
            }
        }
    }

private:
    struct State { float lp1 = 0, lp1b = 0, lp2 = 0, lp2b = 0; };

    void beginHop(int64_t p) noexcept {
        curHop_ = hopIndexOfSample(p, sr_);
        curStart_ = hopStartSample(curHop_, sr_);
        curEnd_ = hopStartSample(curHop_ + 1, sr_);
        clearAcc(); accFromStart_ = (p == curStart_); hopValid_ = true;
    }
    void advanceHop() noexcept {
        ++curHop_; curStart_ = curEnd_; curEnd_ = hopStartSample(curHop_ + 1, sr_);
        clearAcc(); accFromStart_ = true;
    }
    void clearAcc() noexcept { sumSq_ = lowSq_ = hfSq_ = 0.0; peak_ = 0.0f; accCount_ = 0; }

    void accumulate(const float* const* ch, int nch, int off, int seg) noexcept {
        for (int c = 0; c < nch; ++c) {
            State& s = st_[c];
            const float* x = ch[c] + off;
            double sq = 0, lo = 0, hi = 0; float pk = peak_;
            for (int k = 0; k < seg; ++k) {
                const float v = x[k];
                s.lp1  += a800_ * (v - s.lp1);   s.lp1b += a800_ * (s.lp1 - s.lp1b);
                s.lp2  += a4k_  * (v - s.lp2);   s.lp2b += a4k_  * (s.lp2 - s.lp2b);
                const float h = v - s.lp2b;
                sq += static_cast<double>(v) * v;
                lo += static_cast<double>(s.lp1b) * s.lp1b;
                hi += static_cast<double>(h) * h;
                pk = std::max(pk, std::fabs(v));
            }
            sumSq_ += sq; lowSq_ += lo; hfSq_ += hi; peak_ = pk;
        }
        accCount_ += seg;
    }
    HopFeatures finish(int nch) const noexcept {
        HopFeatures f;
        const double rms = std::sqrt(sumSq_ / (static_cast<double>(accCount_) * nch));
        f.rmsDb  = toDb(static_cast<float>(rms));
        f.peakDb = toDb(peak_);
        if (sumSq_ > 1.0e-12) {
            f.lf = static_cast<float>(std::min(1.0, lowSq_ / sumSq_));
            f.hf = static_cast<float>(std::min(1.0, hfSq_ / sumSq_));
        }
        return f;
    }

    double sr_ = 0.0;
    float a800_ = 0.f, a4k_ = 0.f;
    State st_[2];
    bool hopValid_ = false, accFromStart_ = false;
    int64_t expectedNext_ = -1, curHop_ = 0, curStart_ = 0, curEnd_ = 0, accCount_ = 0;
    double sumSq_ = 0, lowSq_ = 0, hfSq_ = 0; float peak_ = 0.f;
};

} // namespace crayldr
