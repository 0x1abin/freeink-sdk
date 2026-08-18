#include "Uc8279cA4Driver.h"

#include <Arduino.h>

#include <string.h>

#include <BoardConfig.h>

namespace freeink {
namespace {
constexpr uint8_t CMD_PANEL_SETTING = 0x00;    // PSR
constexpr uint8_t CMD_POWER_OFF = 0x02;        // POF
constexpr uint8_t CMD_PFS = 0x03;              // PFS
constexpr uint8_t CMD_POWER_ON = 0x04;         // PON
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;       // DSLP (0xA5)
constexpr uint8_t CMD_DTM1 = 0x10;             // OLD plane
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;  // DRF
constexpr uint8_t CMD_DTM2 = 0x13;             // NEW plane
constexpr uint8_t CMD_PLL = 0x30;
constexpr uint8_t CMD_RESOLUTION = 0x61;         // TRES
constexpr uint8_t CMD_GATE_SOURCE_START = 0x65;  // GSST
constexpr uint8_t CMD_PARTIAL_IN = 0x91;         // PTIN
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;        // PTOUT
constexpr uint8_t CMD_CCSET = 0xE0;
constexpr uint8_t CMD_GATE_SCAN = 0xE1;
constexpr uint8_t CMD_TSSET = 0xE5;
}  // namespace

const Uc8279cA4Config& uc8279cA4DefaultConfig() {
  // Built-in OTP defaults carried from the UC8279 X4 sibling; border/PLL/temp
  // bytes may need tuning on the bench (docs/eego-a4-support.md).
  static const Uc8279cA4Config cfg = {
      0x17, 0x4D, 0x20, 0x0E, 0x02, 0x02, 0x1E, 0x5A, 600, 4,
  };
  return cfg;
}

Uc8279cA4Driver::Uc8279cA4Driver(const Uc8279cA4Config& cfg)
    : _cfg(cfg),
      _w(BoardConfig::ACTIVE.displayWidth),
      _h(BoardConfig::ACTIVE.displayHeight),
      _wb(BoardConfig::ACTIVE.displayWidth / 8),
      _tresH(cfg.tresHeight),
      _bufferSize(static_cast<uint32_t>(BoardConfig::ACTIVE.displayWidth / 8) * BoardConfig::ACTIVE.displayHeight) {}

uint32_t Uc8279cA4Driver::spiHz() const {
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : 20000000;
}

PanelGeometry Uc8279cA4Driver::geometry() const { return {_w, _h, _wb, _bufferSize}; }

void Uc8279cA4Driver::initController(EpdBus& bus) {
  bus.cmd(CMD_PANEL_SETTING);
  bus.data(_cfg.psr0);
  bus.data(_cfg.psr1);

  bus.cmd(CMD_RESOLUTION);
  bus.data(static_cast<uint8_t>((_w >> 8) & 0xFF));
  bus.data(static_cast<uint8_t>(_w & 0xFF));
  bus.data(static_cast<uint8_t>((_tresH >> 8) & 0xFF));
  bus.data(static_cast<uint8_t>(_tresH & 0xFF));

  bus.cmd(CMD_GATE_SOURCE_START);
  bus.data(0x00);
  bus.data(0x00);
  bus.data(0x00);
  bus.data(0x00);

  bus.cmd(CMD_PFS);
  bus.data(_cfg.pfs);
  bus.cmd(CMD_PLL);
  bus.data(_cfg.pll);
  bus.cmd(CMD_GATE_SCAN);
  bus.data(_cfg.gateScan);

  _isScreenOn = false;
}

void Uc8279cA4Driver::begin(EpdBus& bus) {
  bus.reset(50);
  initController(bus);
}

// Visible rows bottom-to-top, then 48 white rows to fill the 768x600 RAM.
void Uc8279cA4Driver::streamPlane(EpdBus& bus, uint8_t ramCmd, const uint8_t* fb) {
  uint8_t row[128];
  const uint16_t wb = _wb <= sizeof(row) ? _wb : sizeof(row);
  bus.cmd(ramCmd);
  for (uint16_t y = _h; y-- > 0;) {
    const uint8_t* src = fb + static_cast<uint32_t>(y) * _wb;
    bus.data(src, wb);
  }
  memset(row, 0xFF, wb);
  for (uint16_t y = _h; y < _tresH; y++) bus.data(row, wb);
}

void Uc8279cA4Driver::fillPlaneWhite(EpdBus& bus, uint8_t ramCmd) {
  uint8_t row[128];
  const uint16_t wb = _wb <= sizeof(row) ? _wb : sizeof(row);
  memset(row, 0xFF, wb);
  bus.cmd(ramCmd);
  for (uint16_t y = 0; y < _tresH; y++) bus.data(row, wb);
}

void Uc8279cA4Driver::powerOnIfNeeded(EpdBus& bus, const char* tag) {
  if (_isScreenOn) return;
  bus.cmd(CMD_POWER_ON);
  bus.waitBusy(tag);
  _isScreenOn = true;
}

void Uc8279cA4Driver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)prev;
  bool fast = (mode != RefreshMode::Full) && !_needFullClear && _oldPlaneValid;
  if (fast && _fastRefreshesSinceFull >= _cfg.maxFastBeforeFull) fast = false;

  streamPlane(bus, CMD_DTM2, fb);
  if (!fast) fillPlaneWhite(bus, CMD_DTM1);

  bus.cmd(CMD_CCSET);
  bus.data(_cfg.ccset);
  bus.cmd(CMD_TSSET);
  bus.data(fast ? _cfg.tssetFast : _cfg.tsset);
  bus.cmd(CMD_PANEL_SETTING);
  bus.data(_cfg.psr0);
  bus.data(_cfg.psr1);
  if (fast) {
    bus.cmd(CMD_PFS);
    bus.data(_cfg.pfs);
    bus.cmd(CMD_GATE_SCAN);
    bus.data(_cfg.gateScan);
  }

  powerOnIfNeeded(bus, " 8279c_PON");

  if (fast) bus.cmd(CMD_PARTIAL_IN);
  bus.cmd(CMD_DISPLAY_REFRESH);
  bus.waitBusy(" 8279c_DRF");
  if (fast) bus.cmd(CMD_PARTIAL_OUT);

  streamPlane(bus, CMD_DTM1, fb);
  _oldPlaneValid = true;
  _needFullClear = false;
  _fastRefreshesSinceFull = fast ? static_cast<uint8_t>(_fastRefreshesSinceFull + 1) : 0;

  if (turnOff) {
    bus.cmd(CMD_POWER_OFF);
    bus.waitBusy(" 8279c_POF");
    _isScreenOn = false;
  }
}

void Uc8279cA4Driver::requestResync(uint8_t settlePasses) {
  (void)settlePasses;
  _needFullClear = true;
}

void Uc8279cA4Driver::skipInitialResync() { _needFullClear = false; }

void Uc8279cA4Driver::deepSleep(EpdBus& bus) {
  if (_isScreenOn) {
    bus.cmd(CMD_POWER_OFF);
    bus.waitBusy(" 8279c power-down");
    _isScreenOn = false;
  }
  bus.cmd(CMD_DEEP_SLEEP);
  bus.data(0xA5);
}

#ifdef FREEINK_UC8279C_CONFIG
const Uc8279cA4Config& FREEINK_UC8279C_CONFIG();
static const Uc8279cA4Config& uc8279cA4ActiveConfig() { return FREEINK_UC8279C_CONFIG(); }
#else
static const Uc8279cA4Config& uc8279cA4ActiveConfig() { return uc8279cA4DefaultConfig(); }
#endif

PanelDriver& uc8279cA4Driver() {
  static Uc8279cA4Driver instance(uc8279cA4ActiveConfig());
  return instance;
}

}  // namespace freeink
