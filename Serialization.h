#pragma once
#include "Curve.h"
#include "Grid.h"
#include <cstddef>

namespace crayldr {

uint32_t crc32(const uint8_t* data, std::size_t size) noexcept;

// Compact versioned binary state (8 bytes/hop: analysis 6 + curve 2), CRC-protected.
std::vector<uint8_t> serializeState(const AnalysisGrid& grid, const CurveData& curve, double sampleRate);

// Validates everything (size, bounds, CRC) BEFORE touching `grid`. The caller
// passes an allocated, cleared grid. Returns false on any inconsistency.
bool deserializeState(const uint8_t* data, std::size_t size, AnalysisGrid& grid,
                      CurveData& curve, double& sampleRate);

} // namespace crayldr
