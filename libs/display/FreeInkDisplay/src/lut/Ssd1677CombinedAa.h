#pragma once

#include <BoardConfig.h>

#include "Ssd1677Luts.h"
#include "StickyCombinedAa.h"

namespace freeink::combinedAa {
// Per-panel calibration in nominal 5 ms frames. Keep independent entries so
// tuning an unvalidated panel never changes Sticky or Paper Mono.
struct Calibration {
  uint8_t kick, gray, black, frameRate;
  const uint8_t* voltages;
  uint8_t border, powerOff;
  bool powerUpFirst;
  uint8_t sleepKey = 0x03;
};
inline const Calibration sticky{stickyCombinedAa::KICK_FRAMES,
                                stickyCombinedAa::GRAY_FRAMES,
                                stickyCombinedAa::BLACK_FRAMES,
                                0x08,
                                stickyCombinedAa::VOLTAGES,
                                0x80,
                                0x03,
                                true};
inline const Calibration x4pro{16, 24, 32, 0x08, lut_grayscale + 105, 0xC0, 0x03, false};
inline const Calibration x4classic{16, 24, 32, 0x08, lut_grayscale + 105, 0xC0, 0x03, false};
inline const Calibration murphy{16, 24, 32, 0x08, lut_grayscale + 105, 0xC0, 0x03, false};
inline const Calibration waveshare{16, 24, 32, 0x08, lut_grayscale_waveshare + 105, 0x80, 0x03, true, 0x01};
inline const Calibration metalio{16, 24, 32, 0x08, lut_grayscale + 105, 0x80, 0x83, true};
inline const Calibration& calibration() {
#if FREEINK_DEVICE_X4PRO
  return x4pro;
#elif FREEINK_DEVICE_X4CLASSIC
  return x4classic;
#elif FREEINK_DEVICE_MURPHY_M4
  return murphy;
#elif FREEINK_DEVICE_WAVESHARE_EPAPER_397
  return waveshare;
#elif FREEINK_DEVICE_METALIO_EINK4
  return metalio;
#else
  return sticky;
#endif
}
}  // namespace freeink::combinedAa
