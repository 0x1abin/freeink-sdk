#pragma once

#include <Arduino.h>

namespace freeink {

// Board-supplied power glue, called from the LovyanGFX bus lifecycle. The board
// implements these (for example PCA9535 expander + TPS65185 PMIC on LilyGo T5 S3)
// and injects them in its LgfxEpdConfig. Any hook may be null.
struct LgfxEpdPowerHooks {
  bool (*prepare)();
  bool (*powerOn)();
  void (*powerOff)();
};

// LovyanGFX parallel-EPD wiring. Geometry is not here; it comes from the active
// BoardProfile, like all drivers. This carries only bus/panel specifics.
struct LgfxEpdConfig {
  int8_t dataPins[8];
  int8_t pinSph;
  int8_t pinSpv;
  int8_t pinOe;
  int8_t pinLe;
  int8_t pinCl;
  int8_t pinCkv;
  int8_t pinPwr;
  uint32_t busHz;
  uint8_t linePadding;
  uint8_t rotation;
  LgfxEpdPowerHooks power;
  const uint32_t* lutQuality = nullptr;
  size_t lutQualityStep = 0;
  const uint32_t* lutText = nullptr;
  size_t lutTextStep = 0;
  const uint32_t* lutFast = nullptr;
  size_t lutFastStep = 0;
  const uint32_t* lutFastest = nullptr;
  size_t lutFastestStep = 0;

  // --- appended: 16-bit data bus (Read Pico / E0470A01) ---------------------
  // These two are LAST on purpose. The struct is brace-initialized positionally
  // by every board (BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp,
  // BoardPaperS3/src/M5PaperS3LgfxConfig.cpp), so a new member may only be
  // appended with a default: an existing initializer that stops before it keeps
  // compiling and keeps its meaning. Widening dataPins[] to 16 in place would
  // have silently re-mapped those two boards' pins.
  //
  // High data lines D8..D15, driven only when busWidth == 16. -1 == unassigned
  // (BoardConfig.h's PIN_UNASSIGNED), which Bus_EPD's config_t already uses as
  // its own default and never touches past bus_width.
  int8_t dataPinsHigh[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  // 8 = the existing boards. 16 = a 16-bit i80 bus, whose low half is
  // dataPins[0..7] and whose high half is dataPinsHigh[0..7].
  uint8_t busWidth = 8;
};

}  // namespace freeink
