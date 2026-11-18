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
bool imuReady = false;
bool imuAwake = false;

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

// Caller holds the native mutex; reject unowned IRQ/FIFO/filter configuration.
bool imuReadByte(uint8_t reg, uint8_t& value) {
  return metalio_i2c1_transmit_receive_locked(I2C_NUM_1, 0x19, &reg, 1, &value, 1, kTimeoutMs) == ESP_OK;
}

bool imuWriteByte(uint8_t reg, uint8_t value) {
  return metalio_i2c1_write_reg_locked(I2C_NUM_1, 0x19, reg, &value, 1, kTimeoutMs) == ESP_OK;
}

bool imuPollingConfigSafe() {
  constexpr uint8_t preserved[] = {0x21, 0x22, 0x24, 0x25, 0x2e};
  for (const auto reg : preserved) {
    uint8_t value = 0;
    if (!imuReadByte(reg, value) || value != 0) return false;
  }
  return true;
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

bool metalioPlusRtcRead(uint8_t reg, uint8_t* data, size_t size) {
  const bool time = reg == 0x02 && size == 7;
  if (!data || (!time && !(reg == 0 && size == 1))) return false;
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  const uint8_t start = 0;
  uint8_t raw[9] = {};
  const auto err = metalio_i2c1_transmit_receive_locked(I2C_NUM_1, 0x51, &start, 1, raw, time ? 9 : 1, kTimeoutMs);
  metalio_i2c1_unlock();
  // STOP/test modes cannot provide advancing wall time. Presence still works,
  // so an explicit trusted set can initialize a stopped or voltage-low clock.
  if (err != ESP_OK || (time && (raw[0] & 0xa8))) return false;
  for (size_t i = 0; i < size; ++i) data[i] = raw[i + (time ? 2 : 0)];
  return true;
}

bool metalioPlusRtcWriteTime(const uint8_t* bcd) {
  if (!bcd || !metalio_i2c1_lock(kTimeoutMs)) return false;
  const uint8_t reg = 0;
  uint8_t control = 0;
  auto err = metalio_i2c1_transmit_receive_locked(I2C_NUM_1, 0x51, &reg, 1, &control, 1, kTimeoutMs);
  if (err == ESP_OK && (control & 0x80)) {
    metalio_i2c1_unlock();
    return false;  // TEST1 selects an external test clock; never repurpose it.
  }
  // Explicit trusted set also clears TESTC POR override (NXP 8.11.1) to
  // restore normal operation. Read-only begin/now never change that state.
  // PCF8563 control0's other bits are reserved and must be written as zero.
  // Keep STOP asserted on a failed calendar write: partial bytes cannot then
  // masquerade as valid, advancing time after reboot. No alarm/timer writes.
  const uint8_t stop = 0x20, run = 0;
  if (err == ESP_OK) err = metalio_i2c1_write_reg_locked(I2C_NUM_1, 0x51, 0, &stop, 1, kTimeoutMs);
  if (err == ESP_OK) err = metalio_i2c1_write_reg_locked(I2C_NUM_1, 0x51, 0x02, bcd, 7, kTimeoutMs);
  if (err == ESP_OK) err = metalio_i2c1_write_reg_locked(I2C_NUM_1, 0x51, 0, &run, 1, kTimeoutMs);
  metalio_i2c1_unlock();
  return err == ESP_OK;
}

bool metalioPlusImuBegin() {
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  imuReady = imuAwake = false;
  uint8_t who = 0, version = 0;
  const bool ok = imuReadByte(0x0f, who) && who == 0x11 && imuReadByte(0x70, version) && version == 0x28 &&
                  imuPollingConfigSafe() && imuWriteByte(0x20, 0x07);
  imuReady = ok;
  metalio_i2c1_unlock();
  return ok;
}

bool metalioPlusImuWake() {
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  if (!imuReady) {
    metalio_i2c1_unlock();
    return false;
  }
  imuAwake = false;
  // SC7A20H high-resolution +/-2g, BDU; do not touch the shared GPIO2 IRQ routes.
  const bool ok = imuPollingConfigSafe() && imuWriteByte(0x20, 0x07) && imuWriteByte(0x1f, 0x01) &&
                  imuWriteByte(0x23, 0x80) && imuWriteByte(0x20, 0x57);
  if (!ok) imuWriteByte(0x20, 0x07);  // Bounded standby attempt after a partial/ambiguous write.
  imuAwake = ok;
  metalio_i2c1_unlock();
  if (ok) delay(10);
  return ok;
}

bool metalioPlusImuSleep() {
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  const bool ok = imuReady && imuWriteByte(0x20, 0x07);
  imuAwake = false;  // An uncertain hardware state must never expose a fresh sample.
  metalio_i2c1_unlock();
  return ok;
}

bool metalioPlusImuRead(int16_t* axes) {
  if (!axes || !metalio_i2c1_lock(kTimeoutMs)) return false;
  uint8_t status = 0, raw[6] = {};
  const uint8_t reg = 0xa8;
  const bool ok =
      imuReady && imuAwake && imuReadByte(0x27, status) && (status & 0x08) &&
      metalio_i2c1_transmit_receive_locked(I2C_NUM_1, 0x19, &reg, 1, raw, sizeof(raw), kTimeoutMs) == ESP_OK;
  metalio_i2c1_unlock();
  if (!ok) return false;
  for (size_t i = 0; i < 3; ++i)
    axes[i] = static_cast<int16_t>((raw[i * 2] & 0xf0) | (static_cast<uint16_t>(raw[i * 2 + 1]) << 8));
  return true;
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
