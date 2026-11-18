// Catches missing native routing, stale/invalid telemetry and accidental charger writes.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../src/BatteryMonitor.cpp"
#include "native.h"

namespace {
bool lockOk = true, held = false, voltsOk = true, socOk = true;
unsigned locks = 0, unlocks = 0, transfers = 0, writes = 0;
uint16_t mv = 4012, soc = 73, rawCurrent = 125;
void put(uint8_t* b, uint16_t v) {
  b[0] = v & 255;
  b[1] = v >> 8;
}
void checkNativeOnly() {
  assert(!held && locks == unlocks);
  assert(Wire.beginCalls == 0 && Wire.transmissions == 0 && Wire.requests == 0);
  assert(writes == 0);
}
}  // namespace
extern "C" bool metalio_i2c1_lock(int ms) {
  assert(ms == 50 && !held);
  if (!lockOk) return false;
  ++locks;
  held = true;
  return true;
}
extern "C" void metalio_i2c1_unlock() {
  assert(held);
  held = false;
  ++unlocks;
}
extern "C" int metalio_i2c1_transmit_receive_locked(int bus, uint8_t addr, const uint8_t* tx, size_t nt, uint8_t* rx,
                                                    size_t nr, int ms) {
  assert(held && bus == 1 && addr == 0x55 && nt == 1 && ms == 50);
  ++transfers;
  if (tx[0] == 0x08) {
    assert(nr == 6);
    if (!voltsOk) return -1;
    put(rx, mv);
    rx[2] = 0x80;
    rx[3] = 0x11;
    put(rx + 4, rawCurrent);
    return 0;
  }
  assert(tx[0] == 0x2c && nr == 2);
  if (!socOk) return -1;
  put(rx, soc);
  return 0;
}
extern "C" int metalio_i2c1_write_reg_locked(int, uint8_t, uint8_t, const uint8_t*, size_t, int) {
  ++writes;
  return -1;
}
extern "C" bool epd_board_metalio_eink4_plus_ioexp_ok() { return false; }
extern "C" void epd_board_metalio_eink4_plus_tp_reset() { assert(false); }
extern "C" bool epd_board_metalio_eink4_plus_ioexp_read_port(int, uint8_t*) { return false; }
bool hostI2cWrite(uint8_t, const std::vector<uint8_t>&) {
  ++writes;
  return false;
}
bool hostI2cRead(uint8_t, uint8_t, uint8_t*, size_t) { return false; }

int main(int argc, char** argv) {
  assert(argc == 2);
  const char* c = argv[1];
  if (!std::strcmp(c, "current_negative")) rawCurrent = 0xff83;  // -125
  if (!std::strcmp(c, "current_zero")) rawCurrent = 0;
  if (!std::strcmp(c, "current_minus_one")) rawCurrent = 0xffff;
  if (!std::strcmp(c, "invalid_soc")) soc = 65535;
  if (!std::strcmp(c, "invalid_voltage")) mv = 65535;
  if (!std::strcmp(c, "voltage_failure")) voltsOk = false;
  if (!std::strcmp(c, "soc_failure")) socOk = false;
  if (!std::strcmp(c, "lock_failure")) lockOk = false;
  if (!std::strcmp(c, "soc_zero")) soc = 0;
  if (!std::strcmp(c, "soc_full")) soc = 100;
  if (!std::strcmp(c, "soc_over")) soc = 101;
  if (!std::strcmp(c, "voltage_zero")) mv = 0;
  if (!std::strcmp(c, "voltage_low")) mv = 2499;
  if (!std::strcmp(c, "voltage_high")) mv = 5001;
  if (!std::strcmp(c, "voltage_min")) mv = 2500;
  if (!std::strcmp(c, "voltage_max")) mv = 5000;
  if (!std::strcmp(c, "lock_retry")) lockOk = false;
  if (!std::strcmp(c, "wrap")) hostMillis = std::numeric_limits<unsigned long>::max() - 500;
  BatteryMonitor b;
  const auto s = b.readStatus();
  assert(s.supported);  // RED on the original NO_GAUGE Plus profile.
  assert(s.percentageKnown == (lockOk && socOk && soc <= 100));
  assert(s.millivoltsKnown == (lockOk && voltsOk && mv >= 2500 && mv <= 5000));
  assert(s.chargingKnown == (lockOk && voltsOk));
  if (s.percentageKnown) assert(s.percentage == soc);
  if (s.millivoltsKnown) assert(s.millivolts == mv);
  if (s.chargingKnown) assert(s.charging == (rawCurrent == 125));
  assert(!s.externalPowerKnown);
  if (lockOk)
    assert(transfers == 2);
  else
    assert(transfers == 0);
  uint16_t out = 0xcafe;
  if (!s.percentageKnown) assert(!b.readPercentageChecked(out) && out == 0xcafe);
  if (!s.millivoltsKnown) assert(!readReg16(0x55, 0x08, out) && out == 0xcafe);
  if (!std::strcmp(c, "lock_retry")) {
    lockOk = true;
    assert(b.readPercentageChecked(out) && out == 73 && transfers == 2);
  }
  if (!std::strcmp(c, "cache")) {
    soc = 51;
    for (unsigned i = 0; i < 20; ++i) {
      assert(b.readPercentage() == 73);
      assert(b.isCharging());
    }
    assert(transfers == 2);
    hostMillis = 999;
    assert(b.readPercentage() == 73 && transfers == 2);
    hostMillis = 1000;
    assert(b.readPercentage() == 51 && transfers == 4);
  }
  if (!std::strcmp(c, "expiry")) {
    hostMillis = 1000;
    voltsOk = false;
    socOk = false;
    const auto bad = b.readStatus();
    assert(!bad.percentageKnown && !bad.millivoltsKnown && !bad.chargingKnown);
    assert(transfers == 4);
    voltsOk = true;
    socOk = true;
    assert(!b.readPercentageChecked(out) && out == 0xcafe && transfers == 4);
    hostMillis = 2000;
    assert(b.readPercentageChecked(out) && out == 73 && transfers == 6);
  }
  if (!std::strcmp(c, "wrap")) {
    hostMillis += 999;
    assert(b.readPercentage() == 73 && transfers == 2);
    ++hostMillis;
    soc = 52;
    assert(b.readPercentage() == 52 && transfers == 4);
  }
  if (!std::strcmp(c, "readonly")) {
    assert(!BatteryMonitor::loadDesignCapacity());
    assert(!BatteryMonitor::configureCharger());
    assert(!BatteryMonitor::enterShipMode());
    const auto before = transfers;
    assert(!readReg16(0x6b, 0x08, out));
    assert(!readReg16(0x55, 0x00, out));
    assert(!writeReg8(0x55, 0, 1) && !writeReg16(0x55, 0, 1));
    uint8_t byte = 0xa5;
    assert(!readReg8(0x6b, 0, byte) && byte == 0xa5);
    assert(transfers == before);
  }
  checkNativeOnly();
  std::printf("Plus battery %s passed\n", c);
  return 0;
}
