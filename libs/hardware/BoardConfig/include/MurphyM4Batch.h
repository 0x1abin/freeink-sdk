#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace freeink {

enum class MurphyM4Batch : uint8_t { First, Second };
enum class MurphyM4BatchProbe : uint8_t { Inconclusive, First, Second };

constexpr MurphyM4Batch defaultMurphyM4Batch() {
#if defined(FREEINK_MURPHY_M4_BATCH1) && FREEINK_MURPHY_M4_BATCH1
  return MurphyM4Batch::First;
#else
  return MurphyM4Batch::Second;
#endif
}

constexpr std::size_t MURPHY_M4_BATCH_SAMPLE_COUNT = 7;
constexpr uint32_t MURPHY_M4_BATCH2_RISE_MIN_US = 2000;
constexpr uint32_t MURPHY_M4_BATCH2_RISE_MAX_US = 4500;
constexpr uint32_t MURPHY_M4_BATCH1_RISE_MIN_US = 5200;
constexpr uint32_t MURPHY_M4_BATCH1_RISE_MAX_US = 10000;

constexpr uint32_t medianMurphyM4RiseTime(std::array<uint32_t, MURPHY_M4_BATCH_SAMPLE_COUNT>& samples) {
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const uint32_t value = samples[i];
    std::size_t j = i;
    while (j > 0 && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = value;
  }
  return samples[samples.size() / 2];
}

constexpr MurphyM4BatchProbe classifyMurphyM4RiseTime(const uint32_t riseTimeUs) {
  if (riseTimeUs >= MURPHY_M4_BATCH2_RISE_MIN_US && riseTimeUs <= MURPHY_M4_BATCH2_RISE_MAX_US) {
    return MurphyM4BatchProbe::Second;
  }
  if (riseTimeUs >= MURPHY_M4_BATCH1_RISE_MIN_US && riseTimeUs <= MURPHY_M4_BATCH1_RISE_MAX_US) {
    return MurphyM4BatchProbe::First;
  }
  return MurphyM4BatchProbe::Inconclusive;
}

struct MurphyM4TouchRange {
  int16_t min;
  int16_t max;
};

constexpr MurphyM4TouchRange murphyM4TouchRange(const MurphyM4Batch batch) {
  switch (batch) {
    case MurphyM4Batch::First:
      return {-52, 553};
    case MurphyM4Batch::Second:
      return {-47, 514};
  }
  return {-47, 514};
}

constexpr uint16_t mapMurphyM4TouchShortAxis(const uint16_t raw, const MurphyM4Batch batch, const uint16_t outMax) {
  const MurphyM4TouchRange range = murphyM4TouchRange(batch);
  const int32_t mapped =
      (static_cast<int32_t>(raw) - range.min) * outMax / (static_cast<int32_t>(range.max) - range.min);
  if (mapped <= 0) return 0;
  if (mapped >= outMax) return outMax;
  return static_cast<uint16_t>(mapped);
}

}  // namespace freeink
