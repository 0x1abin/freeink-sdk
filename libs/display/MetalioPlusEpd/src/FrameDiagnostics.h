#pragma once

#include <cstddef>
#include <cstdint>

namespace freeink {
struct FrameDiagnostics {
  uint32_t entire = 0;
  uint32_t center = 0;
};

// Diagnostic fingerprints, not integrity/security checks. The center excludes
// the outer eighth on every edge, so a footer can change independently of it.
// Reads the existing packed4 buffer once; no scratch or heap allocation.
inline FrameDiagnostics frameDiagnostics(const uint8_t* pixels, uint16_t width, uint16_t height) {
  if (!pixels || width == 0 || (width & 1) || height == 0) return {};
  constexpr uint32_t offset = 2166136261u;
  constexpr uint32_t prime = 16777619u;
  FrameDiagnostics result{offset, offset};
  const size_t rowBytes = width / 2;
  const size_t marginX = rowBytes / 8;
  const size_t marginY = height / 8;
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < rowBytes; ++x) {
      const uint8_t value = pixels[y * rowBytes + x];
      result.entire = (result.entire ^ value) * prime;
      if (y >= marginY && y < height - marginY && x >= marginX && x < rowBytes - marginX) {
        result.center = (result.center ^ value) * prime;
      }
    }
  }
  return result;
}
}  // namespace freeink
