#pragma once

// UC8279C panel driver — EEGO Reader A4 (ESP32-S3, 768x552). UC81xx KW family,
// sibling of Uc8279X4Driver. Addresses a 768x600 RAM with 552 visible gates at
// the top, streamed bottom-to-top then padded 48 white rows. BUSY_N idle-high,
// 20 MHz. Four fast (DU) refreshes then a forced full (GC).
//
// B/W uses the family's built-in OTP waveforms. Grayscale + waveform tuning need
// a hardware capture. PENDING HARDWARE VALIDATION. See docs/eego-a4-support.md.

#include "PanelDriver.h"

namespace freeink {

struct Uc8279cA4Config {
  uint8_t psr0;
  uint8_t psr1;
  uint8_t pfs;
  uint8_t pll;
  uint8_t gateScan;
  uint8_t ccset;
  uint8_t tsset;      // full (GC)
  uint8_t tssetFast;  // fast (DU)
  uint16_t tresHeight;
  uint8_t maxFastBeforeFull;
};

const Uc8279cA4Config& uc8279cA4DefaultConfig();

class Uc8279cA4Driver : public PanelDriver {
 public:
  explicit Uc8279cA4Driver(const Uc8279cA4Config& cfg = uc8279cA4DefaultConfig());

  uint32_t spiHz() const override;
  BusyPolarity busyPolarity() const override { return BusyPolarity::UcIdleHigh; }
  PanelGeometry geometry() const override;

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;

  void requestResync(uint8_t settlePasses) override;
  void skipInitialResync() override;

 private:
  void initController(EpdBus& bus);
  void streamPlane(EpdBus& bus, uint8_t ramCmd, const uint8_t* fb);
  void fillPlaneWhite(EpdBus& bus, uint8_t ramCmd);
  void powerOnIfNeeded(EpdBus& bus, const char* tag);

  const Uc8279cA4Config& _cfg;
  uint16_t _w;
  uint16_t _h;
  uint16_t _wb;
  uint16_t _tresH;
  uint32_t _bufferSize;

  bool _isScreenOn = false;
  bool _needFullClear = true;
  bool _oldPlaneValid = false;
  uint8_t _fastRefreshesSinceFull = 0;
};

PanelDriver& uc8279cA4Driver();

}  // namespace freeink
