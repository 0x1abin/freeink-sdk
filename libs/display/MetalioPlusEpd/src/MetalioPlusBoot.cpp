#include <Arduino.h>
#include <MetalioPlusPower.h>
#include <driver/gpio.h>
#include <esp_log.h>

extern "C" {
#include <config.h>
#include <metalio_i2c1.h>
}

namespace freeink {
namespace {
constexpr int kTimeoutMs = 50;

bool restoreRail(const uint8_t address, const unsigned pin) {
  const uint8_t port = pin >> 3;
  const uint8_t mask = 1u << (pin & 7u);
  uint8_t output = 0;
  uint8_t direction = 0;
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  // Read the hardware latch, not the input register: P17 and other output
  // latches must survive this early phase. Native display init owns them later.
  auto err = metalio_i2c1_read_reg_stop_locked(I2C_NUM_1, address, 2 + port, &output, 1, kTimeoutMs);
  if (err == ESP_OK) err = metalio_i2c1_read_reg_stop_locked(I2C_NUM_1, address, 6 + port, &direction, 1, kTimeoutMs);
  if (err == ESP_OK) {
    output |= mask;
    err = metalio_i2c1_write_reg_locked(I2C_NUM_1, address, 2 + port, &output, 1, kTimeoutMs);
  }
  if (err == ESP_OK) {
    direction &= static_cast<uint8_t>(~mask);
    err = metalio_i2c1_write_reg_locked(I2C_NUM_1, address, 6 + port, &direction, 1, kTimeoutMs);
  }
  metalio_i2c1_unlock();
  if (err != ESP_OK) return false;
  delay(POWER_RAIL_SETTLE_MS);
  return true;
}
}  // namespace

bool metalioPlusRestoreBootRails() {
  // Reuse the native bus owner. Its idempotent init is called again by display
  // bring-up; this moves the same bus allocation earlier, without a second bus.
  if (gpio_hold_dis(I2C_SDA_PIN) != ESP_OK || gpio_hold_dis(I2C_SCL_PIN) != ESP_OK ||
      metalio_i2c1_init(I2C_SDA_PIN, I2C_SCL_PIN) != ESP_OK)
    return false;
  if (!metalio_i2c1_lock(kTimeoutMs)) return false;
  // Match the frozen native board's expander probe preference.
  constexpr uint8_t candidates[] = {0x20, 0x24, 0x21, 0x22, 0x23, 0x25, 0x26, 0x27};
  uint8_t address = 0;
  for (const auto candidate : candidates) {
    if (metalio_i2c1_probe_locked(I2C_NUM_1, candidate, kTimeoutMs) == ESP_OK) {
      address = candidate;
      break;
    }
  }
  metalio_i2c1_unlock();
  if (!address || !restoreRail(address, EPD_IO_BT_PA_PWR) || !restoreRail(address, EPD_IO_CAM_SCR_EN)) {
    ESP_LOGE("PlusPower", "Boot rail restoration failed before SD init");
    return false;
  }
  ESP_LOGI("PlusPower", "Boot restored P14 then P5 before SD init");
  return true;
}
}  // namespace freeink
