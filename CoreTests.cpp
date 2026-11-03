// CRAY LDR VOX LEVELER - core verification suite.
// Deliberately NOT based on assert(): checks stay active in Release/NDEBUG.
#include "VoxLevelerCore.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <random>
#include <thread>

using namespace crayldr;

// ------------------------------ mini framework ------------------------------
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++g_checks; const double _a=(a), _b=(b); if (!(std::fabs(_a-_b) <= (tol))) { ++g_fail; std::printf("    FAIL %s:%d  %s=%.6f vs %s=%.6f (tol %g)\n", __FILE__, __LINE__, #a, _a, #b, _b, (double)(tol)); } } while (0)

// ------------------------- allocation tracking (RT safety) -------------------------
static std::atomic<bool> g_track{false};
static std::atomic<long> g_allocs{0};
void* operator new(std::size_t n) { if (g_track.load(std::memory_order_relaxed)) g_allocs.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// ------------------------------ synthetic voice ------------------------------
struct Span { size_t b = 0, e = 0; };
static double lin(double db) { return std::pow(10.0, db / 20.0); }

struct Voice {
    double sr; std::vector<float> x; uint32_t rng = 12345u;
    explicit Voice(double s) : sr(s) {}
    float noise() { rng = rng * 1664525u + 1013904223u; return static_cast<float>(((rng >> 8) & 0xFFFF) / 32768.0 - 1.0); }  // uniform, rms 0.577
    void room(double sec, double db = -75.0) { const size_t n = (size_t)(sec * sr); const double a = lin(db) / 0.57735; for (size_t i = 0; i < n; ++i) x.push_back((float)(a * noise())); }
    Span syl(double dur, double db, double f0 = 180.0, double peakSpike = 0.0) {
        Span sp; sp.b = x.size(); const size_t n = (size_t)(dur * sr), att = (size_t)(0.02 * sr), rel = (size_t)(0.04 * sr);
        double pw = 0; for (int k = 1; k <= 6; ++k) pw += 1.0 / (k * k); pw *= 0.5;
        const double amp = lin(db) / std::sqrt(pw);
        for (size_t i = 0; i < n; ++i) {
            const double env = i < att ? (double)i / att : (i + rel > n ? (double)(n - i) / rel : 1.0);
            double s = 0; for (int k = 1; k <= 6; ++k) s += std::sin(2 * M_PI * k * f0 * (double)i / sr) / k;
            double v = amp * env * s;
            if (peakSpike > 0 && i % (size_t)(0.1 * sr) == 50) v = peakSpike;
            x.push_back((float)v);
        }
        sp.e = x.size(); return sp;
    }
    Span fric(double dur, double db) {
        Span sp; sp.b = x.size(); const size_t n = (size_t)(dur * sr); const double a = lin(db) / (0.57735 * 1.41421); float prev = noise();
        for (size_t i = 0; i < n; ++i) { const float c = noise(); const double env = std::min(1.0, std::min((double)i, (double)(n - i)) / (0.005 * sr)); x.push_back((float)(a * env * (c - prev))); prev = c; }
        sp.e = x.size(); return sp;
    }
    Span breath(double dur, double db) {
        Span sp; sp.b = x.size(); const size_t n = (size_t)(dur * sr); const double a = lin(db) / 0.57735;
        for (size_t i = 0; i < n; ++i) { const double env = std::min(1.0, std::min((double)i, (double)(n - i)) / (0.02 * sr)); x.push_back((float)(a * env * noise())); }
        sp.e = x.size(); return sp;
    }
};

struct Take { std::vector<float> x; std::vector<Span> syl; Span fric, breath; };

static Take makeTake(double sr) {
    Voice v(sr); Take t;
    v.room(0.5);
    t.syl.push_back(v.syl(0.18, -22)); v.room(0.04); t.syl.push_back(v.syl(0.18, -30)); v.room(0.04); t.syl.push_back(v.syl(0.18, -24));
    v.room(0.10); t.breath = v.breath(0.25, -45); v.room(0.40);
    t.syl.push_back(v.syl(0.18, -36)); v.room(0.02); t.fric = v.fric(0.10, -30); v.room(0.02);
    t.syl.push_back(v.syl(0.18, -26)); v.room(0.04); t.syl.push_back(v.syl(0.18, -34));
    v.room(0.60);
    t.syl.push_back(v.syl(0.18, -20)); v.room(0.04); t.syl.push_back(v.syl(0.18, -32)); v.room(0.04); t.syl.push_back(v.syl(0.18, -28));
    v.room(0.50);
    t.x = std::move(v.x); return t;
}

// ------------------------------ helpers ------------------------------
static double rmsDb(const std::vector<float>& x, size_t b, size_t e) {
    double s = 0; for (size_t i = b; i < e; ++i) s += (double)x[i] * x[i];
    return 20 * std::log10(std::max(1e-9, std::sqrt(s / (double)(e - b))));
}
static std::vector<float> run(VoxLevelerCore& core, const std::vector<float>& in, int block, int64_t startPos, bool apply, bool playing = true, bool offlineRender = false) {
    std::vector<float> out; out.reserve(in.size()); std::vector<float> buf((size_t)block);
    for (size_t pos = 0; pos < in.size(); pos += (size_t)block) {
        const int n = (int)std::min<size_t>((size_t)block, in.size() - pos);
        std::memcpy(buf.data(), in.data() + pos, (size_t)n * sizeof(float));
        float* ch[1] = { buf.data() };
        core.processBlock(ch, 1, n, TransportInfo{ playing, true, startPos + (int64_t)pos, offlineRender }, apply);
        out.insert(out.end(), buf.begin(), buf.begin() + n);
    }
    return out;
}
static void analyze(VoxLevelerCore& core, const std::vector<float>& x, int block = 480) {
    core.startAnalysis(); run(core, x, block, 0, false); core.stopAnalysis();
}
static double gainDbAt(const CurveSnapshot& s, double sr, size_t sample) { return 20 * std::log10(s.gainLinearAt((int64_t)sample, sr)); }

static Settings natural() { Settings s; s.mode = Mode::Natural; return s; }
static Settings extreme() { Settings s; s.mode = Mode::Extreme; return s; }

// ------------------------------ tests ------------------------------
static void test_hop_grid() {
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (int64_t p : { 0LL, 1LL, 239LL, 240LL, 220LL, 221LL, 44100LL, 123457LL }) {
            const int64_t h = hopIndexOfSample(p, sr);
            CHECK(hopStartSample(h, sr) <= p); CHECK(hopStartSample(h + 1, sr) > p);
        }
}

static void test_analysis_rate_and_blocksize_independence() {
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        std::vector<float> x((size_t)(sr * 1.0));
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(0.1 * std::sin(2 * M_PI * 200 * i / sr));
        std::vector<std::vector<HopFeatures>> results;
        for (int block : { 1, 7, 64, 480, 4096 }) {
            Analyzer a; a.prepare(sr); std::vector<HopFeatures> f((size_t)kHopsPerSecond + 4); std::vector<int> seen(f.size(), 0);
            for (size_t pos = 0; pos < x.size(); pos += (size_t)block) {
                const float* ch[1] = { x.data() + pos }; const int n = (int)std::min<size_t>((size_t)block, x.size() - pos);
                a.process(ch, 1, n, (int64_t)pos, [&](int64_t h, const HopFeatures& hf) { if (h >= 0 && h < (int64_t)f.size()) { f[(size_t)h] = hf; seen[(size_t)h]++; } });
            }
            int complete = 0; for (int s : seen) { CHECK(s <= 1); complete += s; }
            CHECK(complete >= 199);
            results.push_back(f);
        }
        for (size_t r = 1; r < results.size(); ++r) for (size_t h = 0; h < 199; ++h) { CHECK(results[r][h].rmsDb == results[0][h].rmsDb); CHECK(results[r][h].lf == results[0][h].lf); }
        CHECK_NEAR(results[0][100].rmsDb, 20 * std::log10(0.1 / std::sqrt(2.0)), 0.1);
        CHECK(results[0][100].lf > 0.7f); CHECK(results[0][100].hf < 0.05f);
    }
}

static void test_seek_loop_partial_hops() {
    const double sr = 48000; VoxLevelerCore core; core.prepare(sr); core.startAnalysis();
    std::vector<float> a((size_t)sr, 0.1f), b((size_t)sr, 0.2f);
    run(core, a, 480, 0, false);                 // [0,1s)
    run(core, b, 480, (int64_t)(5.0 * sr) + 100, false);   // seek, lands mid-hop (100 samples in)
    run(core, a, 777, 0, false);                 // loop back, re-analyse
    core.stopAnalysis();
    const AnalysisGrid& g = core.gridForUI();
    CHECK(g.covered() >= 395 && g.covered() <= 400);   // ~2 s, partial hops discarded
    CHECK(!g.at(600).covered());                 // 3 s: never played
    CHECK_NEAR(g.at(1100).rmsDb, 20 * std::log10(0.2), 0.05);   // after the seek
    CHECK(g.at(1000).covered() == false || g.at(1000).rmsDb > -999);  // sanity, no crash
    CHECK(core.coveredHops() == g.covered());
    CHECK(!core.truncated());
}

static void test_segmentation_and_classes() {
    for (double sr : { 48000.0, 44100.0 }) {
        Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x);
        CHECK(core.rebuildCurve(natural()));
        const BuildReport r = core.lastReport();
        CHECK(r.events == 9);       // 9 syllable-like events; fricative and breath are NOT events
        CHECK(r.phrases == 3);
        CHECK(r.groups == 4);
    }
}

static void test_bright_high_pitch_voice_is_not_rejected() {
    // A bright upper-register synthetic voice: deliberately fewer low harmonics
    // than the baseline fixture. This tests the widened acoustic classifier,
    // not linguistic syllable recognition.
    for (double sr : { 44100.0, 48000.0 }) {
        Voice v(sr);
        v.room(0.25);
        v.syl(0.22, -25.0, 420.0);
        v.room(0.05);
        v.syl(0.22, -34.0, 460.0);
        v.room(0.25);
        VoxLevelerCore core; core.prepare(sr); analyze(core, v.x);
        CHECK(core.rebuildCurve(natural()));
        const BuildReport r = core.lastReport();
        CHECK(r.events >= 2);
        CHECK(core.snapshotForUI() != nullptr);
    }
}

static void test_leveling_natural_vs_extreme_and_no_ducking_tail() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x);
    auto spread = [&](const std::vector<float>& sig) {
        double mn = 1e9, mx = -1e9;
        for (auto& s : t.syl) { const double d = rmsDb(sig, s.b + (size_t)(0.04 * sr), s.e - (size_t)(0.06 * sr)); mn = std::min(mn, d); mx = std::max(mx, d); }
        return mx - mn;
    };
    const double before = spread(t.x);
    CHECK(core.rebuildCurve(natural()));
    const CurveSnapshot* sn = core.snapshotForUI();
    const double nat = spread(run(core, t.x, 480, 0, true));
    // v0.1 failure case: a quiet syllable right after a loud one must NOT be ducked.
    const double gLoud = gainDbAt(*sn, sr, (t.syl[0].b + t.syl[0].e) / 2), gQuiet = gainDbAt(*sn, sr, (t.syl[1].b + t.syl[1].e) / 2);
    CHECK(gQuiet - gLoud > 1.0);
    // no gain riding inside a syllable (not a compressor)
    for (auto& s : t.syl) { double mn = 1e9, mx = -1e9; for (size_t p = s.b + (size_t)(0.045 * sr); p < s.e - (size_t)(0.065 * sr); p += 48) { const double g = gainDbAt(*sn, sr, p); mn = std::min(mn, g); mx = std::max(mx, g); } CHECK(mx - mn < 0.3); }
    CHECK(core.rebuildCurve(extreme()));
    const double ext = spread(run(core, t.x, 480, 0, true));
    std::printf("    level spread: input %.2f dB | natural %.2f dB | extreme %.2f dB\n", before, nat, ext);
    CHECK(nat < before * 0.70);      // >= 30 % reduction
    CHECK(ext < before * 0.30);      // >= 70 % reduction
    CHECK(ext < nat);
}

static void test_fricative_breath_noise_not_boosted() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x);
    CHECK(core.rebuildCurve(natural())); const CurveSnapshot* sn = core.snapshotForUI();
    CHECK(gainDbAt(*sn, sr, t.syl[3].b + (t.syl[3].e - t.syl[3].b) / 2) > 2.0);   // quiet voiced syllable IS boosted
    for (size_t p = t.fric.b + (size_t)(0.02 * sr); p < t.fric.e - (size_t)(0.02 * sr); p += 48) CHECK(gainDbAt(*sn, sr, p) <= 0.02);
    for (size_t p = t.breath.b + (size_t)(0.03 * sr); p < t.breath.e - (size_t)(0.03 * sr); p += 48) CHECK(gainDbAt(*sn, sr, p) <= 0.02);
    // centre of the long silence after phrase A: room tone stays at 0 dB
    const size_t mid = (t.syl[2].e + t.syl[3].b) / 2 + (size_t)(0.0 * sr); const double gMid = gainDbAt(*sn, sr, mid);
    CHECK_NEAR(gMid, 0.0, 0.05);
}

static void test_slope_limit_and_ceiling() {
    const double sr = 48000;
    for (Settings st : { natural(), extreme() }) {
        Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x); CHECK(core.rebuildCurve(st));
        const auto& g = core.snapshotForUI()->gainDb();
        const double maxStep = (st.mode == Mode::Natural ? 300.0 : 1200.0) * kHopSeconds * 1.001 + 0.01;   // + int16 quantisation slack not needed (live curve)
        double worst = 0; for (size_t i = 1; i < g.size(); ++i) worst = std::max(worst, (double)std::fabs(g[i] - g[i - 1]));
        CHECK(worst <= maxStep);
        for (float v : g) CHECK(std::fabs(v) <= st.rangeDb + 1e-3);
    }
    // peak ceiling: quiet syllable with -3 dBFS spikes must not be boosted past -0.5 dBFS
    Voice v(sr); v.room(0.5); v.syl(0.18, -20); v.room(0.04); v.syl(0.18, -20); v.room(0.04); v.syl(0.18, -20); v.room(0.3);
    const Span quiet = v.syl(0.5, -38, 180.0, 0.7); v.room(0.3); v.syl(0.18, -20); v.room(0.04); v.syl(0.18, -20); v.room(0.4);
    VoxLevelerCore core; core.prepare(sr); analyze(core, v.x); Settings st = natural(); st.rangeDb = 12.0f; CHECK(core.rebuildCurve(st));
    const auto out = run(core, v.x, 480, 0, true);
    float mxIn = 0, mxOut = 0; for (size_t i = quiet.b; i < quiet.e; ++i) { mxIn = std::max(mxIn, std::fabs(v.x[i])); mxOut = std::max(mxOut, std::fabs(out[i])); }
    CHECK(mxIn >= 0.69f);
    CHECK(mxOut <= (float)lin(-0.5) + 1e-3f);
    CHECK(mxOut > mxIn);   // and it IS still boosted up to the ceiling
}

static void test_apply_once_blocksize_seek_loop_offline() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x); CHECK(core.rebuildCurve(natural()));
    const CurveSnapshot* sn = core.snapshotForUI();
    const std::vector<float> ones(t.x.size(), 1.0f);
    // reference: unit input -> output must equal the curve exactly (applied ONCE, not squared)
    const uint64_t before = core.appliedSamples();
    const auto ref = run(core, ones, 480, 0, true);
    CHECK(core.appliedSamples() - before == ones.size());
    double worstRef = 0, maxG = 0; for (size_t i = 0; i < ref.size(); i += 17) { worstRef = std::max(worstRef, (double)std::fabs(ref[i] - sn->gainLinearAt((int64_t)i, sr))); maxG = std::max(maxG, (double)std::fabs(ref[i] - 1.0f)); }
    CHECK(worstRef < 1e-6); CHECK(maxG > 0.2);   // curve is non-trivial, and output == g (not g^2)
    // Offline host bounce may report isPlaying=false. A valid absolute position
    // plus offlineRender=true must still apply the exact same curve.
    const uint64_t beforeOffline = core.appliedSamples();
    const auto offline = run(core, ones, 1024, 0, true, false, true);
    CHECK(offline == ref);
    CHECK(core.appliedSamples() - beforeOffline == ones.size());
    // identical output for any block partition (bit-exact vs the 480 pass)
    std::mt19937 rng(7);
    for (int block : { 1, 3, 64, 1000, 4096 }) { const auto o = run(core, ones, block, 0, true); bool same = (o == ref); CHECK(same); }
    { std::vector<float> o; size_t pos = 0; while (pos < ones.size()) { const int n = (int)std::min<size_t>(1 + rng() % 3000, ones.size() - pos); std::vector<float> b(ones.begin() + (long)pos, ones.begin() + (long)pos + n); float* ch[1] = { b.data() }; core.processBlock(ch, 1, n, TransportInfo{ true, true, (int64_t)pos }, true); o.insert(o.end(), b.begin(), b.end()); pos += (size_t)n; } CHECK(o == ref); }
    // stereo: both channels receive the same gain
    { std::vector<float> L(480, 1.0f), R(480, 1.0f); float* ch[2] = { L.data(), R.data() }; core.processBlock(ch, 2, 480, TransportInfo{ true, true, 100000 }, true); CHECK(L == R); CHECK(L[0] == ref[100000]); }
    // seek: processing at an arbitrary position after a jump equals the reference at that position
    { std::vector<float> b(256, 1.0f); float* ch[1] = { b.data() }; core.processBlock(ch, 1, 256, TransportInfo{ true, true, 200000 }, true); for (int i = 0; i < 256; ++i) CHECK(b[(size_t)i] == ref[200000 + (size_t)i]);
      std::vector<float> c(256, 1.0f); float* ch2[1] = { c.data() }; core.processBlock(ch2, 1, 256, TransportInfo{ true, true, 5000 }, true); for (int i = 0; i < 256; ++i) CHECK(c[(size_t)i] == ref[5000 + (size_t)i]); }
    // loop: same region three times -> identical
    { std::vector<float> a((size_t)sr, 1.0f); const int64_t start = 150000; auto o1 = run(core, a, 480, start, true), o2 = run(core, a, 480, start, true), o3 = run(core, a, 1024, start, true); CHECK(o1 == o2); CHECK(o1 == o3); }
    // transport stopped or position unknown: bit-exact pass-through
    { std::vector<float> x(2000); for (size_t i = 0; i < x.size(); ++i) x[i] = (float)std::sin(0.01 * i); CHECK(run(core, x, 480, 100000, true, false) == x);
      std::vector<float> b = x; float* ch[1] = { b.data() }; core.processBlock(ch, 1, (int)b.size(), TransportInfo{ true, false, 0 }, true); CHECK(b == x);
      std::vector<float> d = x; float* ch3[1] = { d.data() }; core.processBlock(ch3, 1, (int)d.size(), TransportInfo{ false, false, 0, true }, true); CHECK(d == x); // never guess a timeline position offline
      CHECK(run(core, x, 480, 100000, false, true) == x); }
}

static void test_realtime_no_allocation() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr);
    core.startAnalysis(); std::vector<float> buf(512, 0.1f); float* ch[2] = { buf.data(), buf.data() };
    g_allocs = 0; g_track = true;
    for (int k = 0; k < 300; ++k) { core.processBlock(ch, 2, 512, TransportInfo{ true, true, (int64_t)k * 512 }, false); }
    core.processBlock(ch, 2, 512, TransportInfo{ true, true, 999999 }, false);   // seek during analysis
    core.processBlock(ch, 2, 512, TransportInfo{ false, true, 0 }, false);       // stop
    core.processBlock(ch, 2, 512, TransportInfo{ true, true, (int64_t)kMaxHops * 240 + 10 }, false);   // beyond capacity
    g_track = false; CHECK(g_allocs.load() == 0); CHECK(core.truncated());
    core.stopAnalysis(); analyze(core, t.x); CHECK(core.rebuildCurve(natural()));
    std::vector<float> big(8192, 0.05f); float* c2[2] = { big.data(), big.data() };
    g_allocs = 0; g_track = true;
    for (int k = 0; k < 200; ++k) core.processBlock(c2, 2, 8192, TransportInfo{ true, true, (int64_t)(k % 40) * 8192 }, true);
    core.processBlock(c2, 2, 0, TransportInfo{ true, true, 0 }, true);
    g_track = false; CHECK(g_allocs.load() == 0);
}

static void test_serialization() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore a; a.prepare(sr); analyze(a, t.x); CHECK(a.rebuildCurve(natural()));
    const auto blob = a.saveState(); CHECK(blob.size() > 1000);
    VoxLevelerCore b; b.prepare(sr); CHECK(b.loadState(blob.data(), blob.size())); CHECK(b.hasCurve());
    const CurveSnapshot *sa = a.snapshotForUI(), *sb = b.snapshotForUI();
    CHECK(sa->size() == sb->size()); CHECK(sa->firstHop() == sb->firstHop());
    double worst = 0; for (size_t i = 0; i < sa->size(); ++i) worst = std::max(worst, (double)std::fabs(sa->gainDb()[i] - sb->gainDb()[i]));
    CHECK(worst < 0.006);
    CHECK(a.gridForUI().covered() == b.gridForUI().covered());
    // rebuilding from the restored analysis reproduces the curve (analysis survives the round trip)
    CHECK(b.rebuildCurve(natural())); worst = 0; for (size_t i = 0; i < sa->size(); ++i) worst = std::max(worst, (double)std::fabs(sa->gainDb()[i] - b.snapshotForUI()->gainDb()[i]));
    CHECK(worst < 0.15);
    // corruption: every single-byte flip and every truncation is rejected, never crashes
    std::mt19937 rng(3); int rejected = 0;
    for (int k = 0; k < 400; ++k) { auto c = blob; c[rng() % c.size()] ^= (uint8_t)(1 + rng() % 255); VoxLevelerCore z; z.prepare(sr); if (!z.loadState(c.data(), c.size())) ++rejected; }
    CHECK(rejected == 400);
    for (size_t cut : { (size_t)0, (size_t)10, (size_t)43, blob.size() / 2, blob.size() - 1 }) { VoxLevelerCore z; z.prepare(sr); CHECK(!z.loadState(blob.data(), cut)); CHECK(!z.hasCurve()); }
    // crafted header with valid CRC but absurd counts
    { auto c = blob; const uint32_t huge = 0x7fffffffu; std::memcpy(&c[24], &huge, 4); const uint32_t crc = crc32(c.data(), c.size() - 4); std::memcpy(&c[c.size() - 4], &crc, 4); VoxLevelerCore z; z.prepare(sr); CHECK(!z.loadState(c.data(), c.size())); }
    CHECK(!a.loadState(nullptr, 0));
}

static void test_sample_rate_change() {
    Take t48 = makeTake(48000), t44 = makeTake(44100);
    VoxLevelerCore a; a.prepare(48000); analyze(a, t48.x); CHECK(a.rebuildCurve(natural()));
    VoxLevelerCore b; b.prepare(44100); analyze(b, t44.x); CHECK(b.rebuildCurve(natural()));
    CHECK(a.lastReport().events == b.lastReport().events);
    // state captured at 48 kHz, restored in a 96 kHz project: same gain at the same time
    const auto blob = a.saveState(); VoxLevelerCore c; c.prepare(96000); CHECK(c.loadState(blob.data(), blob.size()));
    for (double sec : { 1.0, 2.0, 3.5, 5.0, 6.5 }) CHECK_NEAR(gainDbAt(*a.snapshotForUI(), 48000, (size_t)(sec * 48000)), gainDbAt(*c.snapshotForUI(), 96000, (size_t)(sec * 96000)), 0.006);
    // 44.1 vs 48 analysis of the same performance gives the same curve within a fraction of a dB
    double worst = 0; for (double sec = 0.6; sec < 6.0; sec += 0.37) worst = std::max(worst, std::fabs(gainDbAt(*a.snapshotForUI(), 48000, (size_t)(sec * 48000)) - gainDbAt(*b.snapshotForUI(), 44100, (size_t)(sec * 44100))));
    CHECK(worst < 0.8);
}

static void test_stale_monitor() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x); CHECK(core.rebuildCurve(natural()));
    run(core, t.x, 480, 0, true); CHECK(!core.isStale());
    std::vector<float> loud = t.x; for (auto& s : loud) s *= 3.0f;
    run(core, loud, 480, 0, true); CHECK(core.isStale());
    CHECK(core.rebuildCurve(natural())); CHECK(!core.isStale());   // rebuilding resets the monitor
}

static void test_concurrent_publish() {
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x); CHECK(core.rebuildCurve(natural()));
    std::atomic<bool> stop{false}; std::atomic<int> bad{0}; std::atomic<long> blocks{0};
    std::thread audio([&] {
        std::vector<float> b(256); int64_t pos = 0;
        while (!stop.load()) {
            std::fill(b.begin(), b.end(), 1.0f); float* ch[1] = { b.data() };
            core.processBlock(ch, 1, 256, TransportInfo{ true, true, pos }, true);
            for (float v : b) if (!(std::isfinite(v) && v >= 0.38f && v <= 2.6f)) bad.fetch_add(1);
            pos = (pos + 256) % (int64_t)(6.0 * sr); blocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    });
    const auto blob = core.saveState();
    for (int k = 0; k < 40; ++k) { core.rebuildCurve(k % 2 ? natural() : extreme()); if (k % 8 == 0) { const auto s = core.saveState(); CHECK(core.loadState(s.data(), s.size())); } }
    CHECK(core.startAnalysis()); CHECK(core.stopAnalysis()); CHECK(core.loadState(blob.data(), blob.size()));
    // Windows CI may round sleep_for(20us) up to the scheduler's ~1ms tick.
    // Wait for a bounded number of audio iterations instead of assuming 50ms
    // always permits >50 callbacks on every hosted runner.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (blocks.load(std::memory_order_relaxed) <= 50 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop = true; audio.join();
    CHECK(bad.load() == 0); CHECK(blocks.load() > 50);
}

static void test_range_knob_is_not_clamped_and_extreme_flattens() {
    // Regression: the RANGE knob used to be silently clamped to 5 dB (NATURAL) / 8 dB (EXTREME),
    // so moving it further did nothing and loud/quiet syllables stayed loud/quiet.
    const double sr = 48000; Take t = makeTake(sr); VoxLevelerCore core; core.prepare(sr); analyze(core, t.x);
    auto spread = [&](const std::vector<float>& sig) {
        double mn = 1e9, mx = -1e9;
        for (auto& sp : t.syl) { const double d = rmsDb(sig, sp.b + (size_t)(0.04 * sr), sp.e - (size_t)(0.06 * sr)); mn = std::min(mn, d); mx = std::max(mx, d); }
        return mx - mn;
    };
    const double before = spread(t.x);
    double prev = before + 1.0;
    for (float rg : { 2.0f, 4.0f, 8.0f, 12.0f }) {
        Settings st = extreme(); st.rangeDb = rg; CHECK(core.rebuildCurve(st));
        const double sp = spread(run(core, t.x, 480, 0, true));
        CHECK(sp < prev + 1e-6);          // a larger range never flattens less
        prev = sp;
    }
    { Settings st = extreme(); st.rangeDb = 12.0f; CHECK(core.rebuildCurve(st)); CHECK(spread(run(core, t.x, 480, 0, true)) < 1.0); }   // essentially flat
    {   // wide take (24 dB between loudest and quietest syllable): 18 dB of range must beat 8 dB
        Voice v(sr); std::vector<Span> sy; v.room(0.5);
        for (double db : { -16.0, -40.0, -22.0, -34.0, -18.0, -38.0, -28.0, -20.0 }) { sy.push_back(v.syl(0.18, db)); v.room(0.05); }
        v.room(0.5);
        auto sp2 = [&](const std::vector<float>& sig) { double mn = 1e9, mx = -1e9; for (auto& q : sy) { const double d = rmsDb(sig, q.b + (size_t)(0.04 * sr), q.e - (size_t)(0.06 * sr)); mn = std::min(mn, d); mx = std::max(mx, d); } return mx - mn; };
        VoxLevelerCore c2; c2.prepare(sr); analyze(c2, v.x);
        Settings a = extreme(); a.rangeDb = 8.0f; a.ceilingDb = 0.0f; Settings b = a; b.rangeDb = 18.0f;
        CHECK(c2.rebuildCurve(a)); const double s8 = sp2(run(c2, v.x, 480, 0, true));
        CHECK(c2.rebuildCurve(b)); const double s18 = sp2(run(c2, v.x, 480, 0, true));
        CHECK(s18 < s8 - 2.0); CHECK(s18 < 1.5);
    }
    { Settings a = natural(); a.rangeDb = 3.0f; Settings b = natural(); b.rangeDb = 8.0f; CHECK(core.rebuildCurve(a)); const double sa = spread(run(core, t.x, 480, 0, true)); CHECK(core.rebuildCurve(b)); CHECK(spread(run(core, t.x, 480, 0, true)) < sa); }
    { Settings st = extreme(); st.amount = 0.5f; st.rangeDb = 12.0f; CHECK(core.rebuildCurve(st)); const double half = spread(run(core, t.x, 480, 0, true)); CHECK(half > 1.0 && half < before); }   // AMOUNT scales the effect
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--negative-control") == 0) {   // proves the harness really fails when a check fails
        CHECK(1 + 1 == 3); std::printf("negative control: failures=%d (must be >0)\n", g_fail); return g_fail > 0 ? 1 : 0;
    }
    struct T { const char* name; void (*fn)(); };
    const T tests[] = {
        { "hop grid mapping", test_hop_grid },
        { "analysis: sample-rate & block-size independence", test_analysis_rate_and_blocksize_independence },
        { "analysis: seek / loop / partial hops", test_seek_loop_partial_hops },
        { "segmentation: events/groups/phrases (acoustic)", test_segmentation_and_classes },
        { "bright high-pitch voice is not rejected", test_bright_high_pitch_voice_is_not_rejected },
        { "leveling: natural vs extreme, no ducking tail, no intra-event riding", test_leveling_natural_vs_extreme_and_no_ducking_tail },
        { "fricative/breath/room-tone never boosted", test_fricative_breath_noise_not_boosted },
        { "slope limit, range and peak ceiling", test_slope_limit_and_ceiling },
        { "apply once / block sizes / seek / loop / stereo / passthrough", test_apply_once_blocksize_seek_loop_offline },
        { "real-time safety: zero allocations in processBlock", test_realtime_no_allocation },
        { "state serialization + corruption rejection", test_serialization },
        { "sample-rate change", test_sample_rate_change },
        { "stale-analysis monitor", test_stale_monitor },
        { "concurrent audio thread vs control thread", test_concurrent_publish },
        { "range knob not clamped; extreme flattens; amount scales", test_range_knob_is_not_clamped_and_extreme_flattens },
    };
    int failedTests = 0;
    for (const auto& t : tests) { const int before = g_fail; std::printf("[ RUN  ] %s\n", t.name); t.fn(); const bool ok = g_fail == before; if (!ok) ++failedTests; std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name); }
    std::printf("\n%d checks, %d failed, %d/%d tests passed\n", g_checks, g_fail, (int)(sizeof(tests) / sizeof(tests[0])) - failedTests, (int)(sizeof(tests) / sizeof(tests[0])));
    return g_fail == 0 ? 0 : 1;
}
