#include "CurveBuilder.h"
#include <cstdint>
#include <utility>

namespace crayldr {
namespace {

struct Profile { float wPhrase, wGroup, wEvent; int halfKernel; float maxSlopeDbPerSec; float prominenceDb; };

Profile profileFor(Mode m) {
    // EXTREME pulls every event all the way to the reference level (weights 1/1/1)
    // and splits syllables more finely (1.5 dB valleys), like riding the fader by hand.
    return m == Mode::Natural ? Profile{0.85f, 0.55f, 0.30f, 5, 300.0f, 3.0f}
                              : Profile{1.00f, 1.00f, 1.00f, 2, 1200.0f, 1.5f};
}

float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    const std::size_t k = static_cast<std::size_t>(clampf(p, 0.f, 1.f) * static_cast<float>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

float weightedPercentile(std::vector<std::pair<float, float>> v, float p) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    double total = 0; for (auto& e : v) total += e.second;
    const double target = clampf(p, 0.f, 1.f) * total;
    double cum = 0;
    for (auto& e : v) { cum += e.second; if (cum >= target) return e.first; }
    return v.back().first;
}

// Exact s-Lipschitz envelopes (two-pass distance transform).
void lipMin(std::vector<float>& v, float s) {
    for (std::size_t i = 1; i < v.size(); ++i) v[i] = std::min(v[i], v[i - 1] + s);
    for (std::size_t i = v.size() - 1; i-- > 0;) v[i] = std::min(v[i], v[i + 1] + s);
}
void lipMax(std::vector<float>& v, float s) {
    for (std::size_t i = 1; i < v.size(); ++i) v[i] = std::max(v[i], v[i - 1] - s);
    for (std::size_t i = v.size() - 1; i-- > 0;) v[i] = std::max(v[i], v[i + 1] - s);
}

// Splits a voiced run into energy nuclei at valleys that are at least
// `prom` dB below both neighbouring peaks. Segments shorter than minSeg
// are never created.
void splitRun(const std::vector<float>& s, float prom, int minSeg, std::vector<int>& cuts) {
    cuts.clear();
    const int n = static_cast<int>(s.size());
    if (n < 2 * minSeg) return;
    std::vector<int> b;
    for (int i = 1; i < n - 1; ++i)
        if (s[i] <= s[i - 1] && s[i] <= s[i + 1] && (s[i] < s[i - 1] || s[i] < s[i + 1])) b.push_back(i);
    std::vector<float> mx;
    while (!b.empty()) {
        const int k = static_cast<int>(b.size());
        mx.assign(static_cast<std::size_t>(k) + 1, -1.0e9f);
        int start = 0;
        for (int j = 0; j <= k; ++j) {
            const int end = (j < k) ? b[j] : n;
            for (int i = start; i < end; ++i) mx[j] = std::max(mx[j], s[i]);
            start = end;
        }
        int worst = -1; float worstDepth = 1.0e9f;
        for (int j = 0; j < k; ++j) {
            const int segL = b[j] - (j == 0 ? 0 : b[j - 1]);
            const int segR = (j + 1 < k ? b[j + 1] : n) - b[j];
            float depth = std::min(mx[j], mx[j + 1]) - s[b[j]];
            if (segL < minSeg || segR < minSeg) depth = -1.0e8f;
            if (depth < worstDepth) { worstDepth = depth; worst = j; }
        }
        if (worstDepth >= prom) break;
        b.erase(b.begin() + worst);
    }
    cuts = b;
}

struct Event { int b = 0, e = 0; float level = 0, peak = 0; int group = 0, phrase = 0; };

} // namespace

bool buildCurve(const AnalysisGrid& grid, const Settings& st, CurveData& out, BuildReport* rep) {
    out = CurveData{};
    BuildReport local; BuildReport& R = rep ? *rep : local; R = BuildReport{};
    if (grid.covered() < 40) return false;

    const Profile pf = profileFor(st.mode);
    const int H = pf.halfKernel;
    const int pad = 80;
    const int64_t lo = grid.first() - pad, hi = grid.last() + pad;
    const int N = static_cast<int>(hi - lo + 1);

    std::vector<float> db(N, kUncovered), pk(N, kUncovered), hf(N, 0.f), lf(N, 0.f), covDb;
    covDb.reserve(static_cast<std::size_t>(grid.covered()));
    for (int64_t h = grid.first(); h <= grid.last(); ++h) {
        const HopFeatures& f = grid.at(h);
        if (!f.covered()) continue;
        const int i = static_cast<int>(h - lo);
        db[i] = f.rmsDb; pk[i] = f.peakDb; hf[i] = f.hf; lf[i] = f.lf;
        covDb.push_back(f.rmsDb);
    }

    // 1. Adaptive voice gate with hysteresis and hold.
    const float p10 = percentile(covDb, 0.10f), p95 = percentile(covDb, 0.95f);
    const float gate = std::max(st.noiseFloorDb, std::min(p10 + 10.0f, p95 - 28.0f));
    R.gateDb = gate;
    std::vector<uint8_t> active(N, 0), cls(N, 0);   // cls: 0 silent, 1 voiced-like, 2 unvoiced
    {
        bool open = false; int hold = 0;
        for (int i = 0; i < N; ++i) {
            if (db[i] >= gate) { open = true; hold = 0; }
            else if (open) { if (db[i] < gate - 3.0f) { if (++hold >= 6) open = false; } else hold = 0; }
            active[i] = open ? 1 : 0;
        }
    }
    // 2. Conservative acoustic classification from spectral balance.
    // v0.2 used fixed LF>=0.30 and HF<0.60 thresholds. Those values were
    // too restrictive for bright/high-pitched vocals and could reject a
    // legitimate voiced nucleus. v0.3 widens the voiced region while keeping
    // a high-frequency-dominant guard for fricatives. This is still an
    // acoustic heuristic, NOT pitch tracking or phoneme recognition.
    for (int i = 0; i < N; ++i) {
        if (!active[i]) continue;
        const float lowEnough = lf[i] >= 0.20f;
        const float notPureFricative = hf[i] < 0.76f;
        const float lowToHighBalance = lf[i] >= hf[i] * 0.34f;
        cls[i] = (lowEnough && notPureFricative && lowToHighBalance) ? 1 : 2;
    }
    // bridge isolated unvoiced hops (<= 2) between voiced hops
    for (int i = 0; i < N;) {
        if (cls[i] != 2) { ++i; continue; }
        int j = i; while (j < N && cls[j] == 2) ++j;
        if (i > 0 && j < N && cls[i - 1] == 1 && cls[j] == 1 && (j - i) <= 2)
            for (int k = i; k < j; ++k) cls[k] = 1;
        i = j;
    }

    // 3. Events = energy nuclei inside voiced runs. The 30 ms minimum
    // suppresses isolated consonant/noise bursts; it does not imply that an
    // event corresponds one-to-one with a linguistic syllable.
    const int minEvent = 6;
    std::vector<Event> ev;
    std::vector<int> cuts;
    for (int i = 0; i < N;) {
        if (cls[i] != 1) { ++i; continue; }
        int j = i; while (j < N && cls[j] == 1) ++j;
        const int n = j - i;
        if (n >= minEvent) {
            std::vector<float> s(static_cast<std::size_t>(n));
            for (int k = 0; k < n; ++k) {
                float acc = 0; int c = 0;
                for (int d = -1; d <= 1; ++d) { const int q = k + d; if (q >= 0 && q < n) { acc += db[i + q]; ++c; } }
                s[static_cast<std::size_t>(k)] = acc / static_cast<float>(c);
            }
            splitRun(s, pf.prominenceDb, minEvent, cuts);
            int a = 0;
            for (std::size_t c = 0; c <= cuts.size(); ++c) {
                const int b = (c < cuts.size()) ? cuts[c] : n;
                if (b - a >= minEvent) {
                    std::vector<float> seg(db.begin() + i + a, db.begin() + i + b);
                    Event e; e.b = i + a; e.e = i + b;
                    e.level = percentile(seg, 0.80f);
                    e.peak = *std::max_element(pk.begin() + i + a, pk.begin() + i + b);
                    if (e.level >= gate + 6.0f) ev.push_back(e);
                }
                a = b;
            }
        }
        i = j;
    }
    R.events = static_cast<int>(ev.size());
    if (ev.empty()) return false;

    // 4. Groups (short gaps) and phrases (long gaps).
    const int groupGap = 16, phraseGap = 60;
    int g = 0, p = 0;
    for (std::size_t k = 1; k < ev.size(); ++k) {
        const int gap = ev[k].b - ev[k - 1].e;
        if (gap > phraseGap) { ++p; ++g; } else if (gap > groupGap) ++g;
        ev[k].group = g; ev[k].phrase = p;
    }
    R.groups = g + 1; R.phrases = p + 1;

    // 5. Hierarchical level targets.
    std::vector<std::pair<float, float>> lv;
    for (auto& e : ev) lv.push_back({e.level, static_cast<float>(e.e - e.b)});
    const float ref = weightedPercentile(lv, st.targetPercentile);
    R.referenceDb = ref;
    std::vector<double> gS(static_cast<std::size_t>(g) + 1, 0), gW(gS), pS(static_cast<std::size_t>(p) + 1, 0), pW(pS);
    for (auto& e : ev) {
        const double w = e.e - e.b;
        gS[e.group] += e.level * w; gW[e.group] += w; pS[e.phrase] += e.level * w; pW[e.phrase] += w;
    }
    const float amount = clampf(st.amount, 0.f, 1.f);
    // The RANGE knob is the only limit on boost/cut. (Earlier versions silently
    // clamped it to 5 dB in NATURAL and 8 dB in EXTREME, so moving the knob past
    // those values did nothing.) NATURAL stays gentler through its weights.
    const float range = std::max(0.f, st.rangeDb);
    std::vector<float> tgt(N, 0.0f);
    std::vector<uint8_t> defined(N, 0);
    for (auto& e : ev) {
        const float Lg = static_cast<float>(gS[e.group] / gW[e.group]);
        const float Lp = static_cast<float>(pS[e.phrase] / pW[e.phrase]);
        float T = amount * (pf.wPhrase * (ref - Lp) + pf.wGroup * (Lp - Lg) + pf.wEvent * (Lg - e.level));
        if (T > 0.0f) T *= clampf((e.level - gate) / 15.0f, 0.0f, 1.0f);   // never lift noise
        T = clampf(T, -range, range);
        for (int i = e.b; i < e.e; ++i) { tgt[i] = T; defined[i] = 1; }
    }

    // 5b. Intra-event automatic riding.
    // The previous engine assigned one constant gain value to each event. That
    // balanced syllable averages but could not reproduce a hand-drawn vocal ride.
    // Build a short, non-causal local envelope and continuously correct voiced
    // nuclei toward the take's weighted reference level. The correction is
    // deliberately limited; consonants, breaths and unvoiced regions are not
    // lifted. The later peak guard remains authoritative.
    {
        const int radius = (st.mode == Mode::Natural) ? 3 : 2; // 35 ms / 25 ms total window
        std::vector<float> local(N, 0.0f);
        for (int i = 0; i < N; ++i) {
            if (cls[i] != 1) continue;
            float sum = 0.0f;
            int count = 0;
            for (int k = std::max(0, i - radius); k <= std::min(N - 1, i + radius); ++k) {
                if (cls[k] == 1 && db[k] > -999.0f) { sum += db[k]; ++count; }
            }
            if (count > 0) local[i] = sum / static_cast<float>(count);
        }

        // Use the take-level event reference, but avoid chasing isolated peaks.
        // Detail correction is strongest on sustained voiced energy and fades
        // near high-crest-factor attacks (plosives/consonants).
        const float detailScale = (st.mode == Mode::Natural) ? 0.58f : 0.78f;
        const float detailLimit = (st.mode == Mode::Natural) ? 3.0f : 4.5f;
        for (const auto& e : ev) {
            for (int i = e.b; i < e.e; ++i) {
                if (cls[i] != 1 || local[i] <= 0.0f) continue;
                const float crest = pk[i] - db[i];
                const float transientProtection = clampf(1.0f - (crest - 8.0f) / 12.0f, 0.20f, 1.0f);
                float detail = (e.level - local[i]) * detailScale * transientProtection * amount;
                detail = clampf(detail, -detailLimit, detailLimit);
                tgt[i] = clampf(tgt[i] + detail, -range, range);
            }
        }
    }

    // 6. Fill between events. Short gaps: interpolate. Long gaps: hold the
    //    neighbouring plateau for H hops (so the transition happens fully
    //    inside the silence) and return to 0 dB. Unvoiced hops never get boost.
    auto dil = [&](float T, int i) { return cls[i] == 2 ? std::min(T, 0.0f) : T; };
    for (int i = 0; i < N;) {
        if (defined[i]) { ++i; continue; }
        int j = i; while (j < N && !defined[j]) ++j;
        const int len = j - i; const bool hasA = i > 0, hasB = j < N;
        int nInact = 0; for (int k = i; k < j; ++k) if (!active[k]) ++nInact;
        if (hasA && hasB && len <= 30 && nInact <= 12) {
            for (int k = i; k < j; ++k) {
                const float t = static_cast<float>(k - i + 1) / static_cast<float>(len + 1);
                float v = tgt[i - 1] + (tgt[j] - tgt[i - 1]) * t;
                if (cls[k] == 2) v = std::min(v, 0.0f);
                tgt[k] = v;
            }
        } else {
            int left = 0, right = 0;
            if (hasA && hasB) { left = std::min(H, (len + 1) / 2); right = std::min(H, len - left); }
            else if (hasA) left = std::min(H, len);
            else if (hasB) right = std::min(H, len);
            for (int k = 0; k < left; ++k) tgt[i + k] = dil(tgt[i - 1], i + k);
            for (int k = 0; k < right; ++k) tgt[j - 1 - k] = dil(tgt[j], j - 1 - k);
        }
        i = j;
    }

    // 7. Symmetric (non-causal) Hann smoothing in dB.
    std::vector<float> w(static_cast<std::size_t>(2 * H + 1));
    float wsum = 0;
    for (int k = -H; k <= H; ++k) { w[static_cast<std::size_t>(k + H)] = 0.5f * (1.0f + std::cos(3.14159265f * static_cast<float>(k) / static_cast<float>(H + 1))); wsum += w[static_cast<std::size_t>(k + H)]; }
    for (auto& x : w) x /= wsum;
    std::vector<float> gain(N, 0.0f);
    for (int i = 0; i < N; ++i) {
        float acc = 0;
        for (int k = -H; k <= H; ++k) { const int q = i + k; if (q >= 0 && q < N) acc += w[static_cast<std::size_t>(k + H)] * tgt[q]; }
        gain[i] = acc;
    }

    // 8. Hard slope limit (exact Lipschitz bound, zero lag).
    const float s = pf.maxSlopeDbPerSec * static_cast<float>(kHopSeconds);
    {
        std::vector<float> a = gain, b = gain;
        lipMin(a, s); lipMax(b, s);
        for (int i = 0; i < N; ++i) gain[i] = 0.5f * (a[i] + b[i]);
    }

    // 9. Peak-ceiling guard: boosts can never push a known peak above the ceiling.
    {
        std::vector<float> ceilArr(N, 60.0f);
        for (int i = 0; i < N; ++i) {
            float m = pk[i]; if (i > 0) m = std::max(m, pk[i - 1]);
            if (m > -999.0f) ceilArr[i] = std::max(0.0f, st.ceilingDb - m);
        }
        lipMin(ceilArr, s);
        for (int i = 0; i < N; ++i) gain[i] = std::min(gain[i], ceilArr[i]);
    }

    out.firstHop = lo;
    out.gainDb = std::move(gain);
    return true;
}

} // namespace crayldr
