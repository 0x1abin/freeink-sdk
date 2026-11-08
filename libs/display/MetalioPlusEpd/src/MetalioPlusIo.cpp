#include <Arduino.h>
#include <MetalioPlusIo.h>

extern "C" {
#include <epd_board_metalio_eink4_plus.h>
#include <metalio_i2c1.h>
}

namespace freeink {
namespace {
constexpr int kTimeoutMs = 50;
constexpr uint8_t kTouchAddress = 0x38;
bool attempted = false;
bool ready = false;

// All gauge cache access is protected by the existing native bus mutex.
struct GaugeSnapshot {
  bool attempted = false;
  bool voltageCurrentOk = false;
  bool socOk = false;
  unsigned long at = 0;
  uint16_t voltage = 0;
  uint16_t current = 0;
  uint16_t soc = 0;
};
GaugeSnapshot gauge;

uint16_t littleEndianWord(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

void refreshGauge() {
  const unsigned long now = millis();
  if (gauge.attempted && now - gauge.at < 1000) return;
  gauge.attempted = true;
  gauge.at = now;  // Failed snapshots are throttled too; no stale success.
  uint8_t data[6] = {};
  uint8_t reg = 0x08;
  gauge.voltageCurrentOk =
      metalio_i2c1_transmit_receive_locked(I2C_NUM_1, 0x55, &reg, 1, data, sizeof(data), kTimeoutMs) == ESP_OK;
  if (gauge.voltageCurrentOk) {
    gauge.voltage = littleEndianWord(data);
    gauge.current = littleEndianWord(data + 4);
  }
  delayMicroseconds(66);  // TI SLUSCB7A 7.3.1.3: gauge bus-free interval.
  reg = 0x2c;
  gauge.socOk = metalio_i2c1_transmit_receive_locked(I2C_NUM_1, 0x55, &reg, 1, data, 2, kTimeoutMs) == ESP_OK;
  if (gauge.socOk) gauge.soc = littleEndianWord(data);
}

bool readRegister(uint8_t reg, uint8_t* data, size_t size) {
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  const auto err = metalio_i2c1_transmit_receive_locked(I2C_NUM_1, kTouchAddress, &reg, 1, data, size, kTimeoutMs);
  metalio_i2c1_unlock();
  return err == ESP_OK;
}
}  // namespace

bool metalioPlusReadGauge16(uint8_t reg, uint16_t& value) {
  if (reg != 0x08 && reg != 0x0c && reg != 0x2c) return false;
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  refreshGauge();
  bool ok = false;
  switch (reg) {
    case 0x08:
      ok = gauge.voltageCurrentOk && gauge.voltage >= 2500 && gauge.voltage <= 5000;
      if (ok) value = gauge.voltage;
      break;
    case 0x0c:
      ok = gauge.voltageCurrentOk;
      if (ok) value = gauge.current;
      break;
    case 0x2c:
      ok = gauge.socOk && gauge.soc <= 100;
      if (ok) value = gauge.soc;
      break;
  }
  metalio_i2c1_unlock();
  return ok;
}

bool metalioPlusTouchBegin() {
  if (attempted) return ready;
  // A caller before display/board init must be able to retry after it, without
  // touching reset or initializing the vendor's non-idempotent display stack.
  if (!epd_board_metalio_eink4_plus_ioexp_ok()) return false;
  attempted = true;
  epd_board_metalio_eink4_plus_tp_reset();
  uint8_t chip = 0;
  if (!readRegister(0xa3, &chip, 1) || !metalio_i2c1_lock(kTimeoutMs)) return false;
  const uint8_t mode = 0;
  const auto err = metalio_i2c1_write_reg_locked(I2C_NUM_1, kTouchAddress, 0xa4, &mode, 1, kTimeoutMs);
  metalio_i2c1_unlock();
  ready = err == ESP_OK;
  return ready;
}

bool metalioPlusTouchRead(uint8_t* frame, size_t size) {
  if (!ready || !frame || size != 11) return false;
  return readRegister(0x02, frame, size);
}

bool metalioPlusReadButtons(uint8_t& mask) {
  mask = 0;
  uint8_t port = 0xff;
  // The vendor helper owns its I2C lock. Never take it recursively here.
  if (!epd_board_metalio_eink4_plus_ioexp_read_port(1, &port)) return false;
  if (!(port & 1u)) mask |= 1u << 4;
  if (!(port & 2u)) mask |= 1u << 5;
  if (!(port & 0x80u)) mask |= 1u << 6;
  return true;
}
}  // namespace freeink
