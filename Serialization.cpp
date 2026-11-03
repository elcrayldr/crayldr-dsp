#include "Serialization.h"
#include <cstring>

namespace crayldr {
namespace {
constexpr std::size_t kHeader = 40;
constexpr uint16_t kVersion = 1;

void put(std::vector<uint8_t>& o, uint64_t v, int bytes) { for (int i = 0; i < bytes; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i))); }
uint64_t get(const uint8_t* p, int bytes) { uint64_t v = 0; for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i); return v; }
int16_t q16(float db) { return static_cast<int16_t>(std::lround(std::max(-300.0f, std::min(300.0f, db)) * 100.0f)); }
uint8_t q8(float v) { return static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f)); }
}

uint32_t crc32(const uint8_t* d, std::size_t n) noexcept {
    struct Table { uint32_t t[256]; Table() { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } } };
    static const Table table;   // thread-safe initialisation
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = table.t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> serializeState(const AnalysisGrid& grid, const CurveData& curve, double sr) {
    const uint32_t aNum = grid.covered() > 0 ? static_cast<uint32_t>(grid.last() - grid.first() + 1) : 0;
    const int64_t aFirst = aNum ? grid.first() : 0;
    const uint32_t cNum = static_cast<uint32_t>(curve.gainDb.size());
    std::vector<uint8_t> o; o.reserve(kHeader + aNum * 6ull + cNum * 2ull + 4);
    o.push_back('C'); o.push_back('L'); o.push_back('V'); o.push_back('L');
    put(o, kVersion, 2); put(o, 200, 2);
    uint64_t bits; std::memcpy(&bits, &sr, 8); put(o, bits, 8);
    put(o, static_cast<uint64_t>(aFirst), 8); put(o, aNum, 4);
    put(o, static_cast<uint64_t>(curve.firstHop), 8); put(o, cNum, 4);
    for (uint32_t k = 0; k < aNum; ++k) {
        const HopFeatures& f = grid.at(aFirst + k);
        if (!f.covered()) { put(o, 0x8000, 2); put(o, 0, 2); put(o, 0, 2); continue; }
        put(o, static_cast<uint16_t>(q16(f.rmsDb)), 2); put(o, static_cast<uint16_t>(q16(f.peakDb)), 2);
        put(o, q8(f.hf), 1); put(o, q8(f.lf), 1);
    }
    for (float g : curve.gainDb) put(o, static_cast<uint16_t>(q16(g)), 2);
    put(o, crc32(o.data(), o.size()), 4);
    return o;
}

bool deserializeState(const uint8_t* d, std::size_t size, AnalysisGrid& grid, CurveData& curve, double& sr) {
    if (d == nullptr || size < kHeader + 4) return false;
    if (d[0] != 'C' || d[1] != 'L' || d[2] != 'V' || d[3] != 'L') return false;
    if (get(d + 4, 2) != kVersion || get(d + 6, 2) != 200) return false;
    const uint64_t bits = get(d + 8, 8); double rate; std::memcpy(&rate, &bits, 8);
    if (!(rate >= 8000.0 && rate <= 768000.0)) return false;
    const int64_t aFirst = static_cast<int64_t>(get(d + 16, 8));
    const uint64_t aNum = get(d + 24, 4);
    const int64_t cFirst = static_cast<int64_t>(get(d + 28, 8));
    const uint64_t cNum = get(d + 36, 4);
    if (aNum > static_cast<uint64_t>(kMaxHops) || cNum > static_cast<uint64_t>(kMaxHops) + 4096) return false;
    if (aNum > 0 && (aFirst < 0 || aFirst + static_cast<int64_t>(aNum) > kMaxHops)) return false;
    if (cNum > 0 && (cFirst < -4096 || cFirst > kMaxHops)) return false;
    if (size != kHeader + aNum * 6 + cNum * 2 + 4) return false;
    if (static_cast<uint32_t>(get(d + size - 4, 4)) != crc32(d, size - 4)) return false;
    if (aNum > 0 && !grid.allocated()) return false;

    sr = rate;
    const uint8_t* p = d + kHeader;
    for (uint64_t k = 0; k < aNum; ++k, p += 6) {
        const uint16_t r = static_cast<uint16_t>(get(p, 2));
        if (r == 0x8000) continue;
        HopFeatures f;
        f.rmsDb = static_cast<int16_t>(r) / 100.0f; f.peakDb = static_cast<int16_t>(get(p + 2, 2)) / 100.0f;
        f.hf = p[4] / 255.0f; f.lf = p[5] / 255.0f;
        grid.set(aFirst + static_cast<int64_t>(k), f);
    }
    curve.firstHop = cFirst; curve.gainDb.resize(static_cast<std::size_t>(cNum));
    for (uint64_t k = 0; k < cNum; ++k, p += 2) curve.gainDb[static_cast<std::size_t>(k)] = static_cast<int16_t>(get(p, 2)) / 100.0f;
    return true;
}

} // namespace crayldr
