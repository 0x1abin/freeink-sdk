#include <Arduino.h>
#include <MetalioPlusIo.h>
#include <MetalioPlusPower.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>

extern "C" {
#include <config.h>
#include <epd_board_metalio_eink4_plus.h>
}

namespace freeink {
namespace {
bool parkLow(const gpio_num_t pin) {
  return gpio_hold_dis(pin) == ESP_OK && gpio_reset_pin(pin) == ESP_OK &&
         gpio_set_direction(pin, GPIO_MODE_OUTPUT) == ESP_OK && gpio_set_pull_mode(pin, GPIO_FLOATING) == ESP_OK &&
         gpio_set_level(pin, 0) == ESP_OK && gpio_hold_en(pin) == ESP_OK;
}

bool waitForReleasedPower() {
  const auto started = millis();
  do {
    uint8_t buttons = 0;
    if (!metalioPlusReadButtons(buttons)) return false;
    if (!(buttons & (1u << 6))) return true;
    delay(20);
  } while (millis() - started < 5000);
  return false;
}

bool waitForIdleIrq() {
  const auto started = millis();
  do {
    if (gpio_get_level(IO_EXPANDER_INT_GPIO) != 0) return true;
    delay(20);
  } while (millis() - started < 600);
  return false;
}
}  // namespace

bool metalioPlusPrepareDeepSleep() {
  bool maskAttempted = false;
  bool railsAttempted = false;
  const auto fail = [&](const char* stage) {
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    ESP_LOGE("PlusPower", "Sleep preparation failed: %s; reboot required", stage);
    if (railsAttempted && epd_board_metalio_eink4_plus_sleep_restore_rails() != ESP_OK)
      ESP_LOGE("PlusPower", "Rail restoration failed before reboot");
    if (maskAttempted && epd_board_metalio_eink4_plus_ioexp_restore_keys() != ESP_OK)
      ESP_LOGE("PlusPower", "Key restoration failed before reboot");
    // Do not reallocate/restart LCD here. The caller reboots; normal boot and
    // SD begin release the parked/held pads before recreating their drivers.
    return false;
  };
  if (!epd_board_metalio_eink4_plus_ioexp_ok()) return fail("native board not ready");
  if (!esp_sleep_is_valid_wakeup_gpio(IO_EXPANDER_INT_GPIO)) return fail("invalid wake pad");
  if (!waitForReleasedPower()) return fail("power held or unreadable");

  // Storage must already be flushed and its SDMMC host stopped by the caller.
  // These stack-only loops neither create a bus nor allocate a second buffer.
  const gpio_num_t parked[] = {FL_COOL_PIN,  FL_WARM_PIN,  SDMMC_CLK_PIN, SDMMC_CMD_PIN,
                               SDMMC_D0_PIN, SDMMC_D1_PIN, SDMMC_D2_PIN,  SDMMC_D3_PIN};
  for (const auto pin : parked) {
    if (!parkLow(pin)) return fail("pad park");
  }
  epd_board_metalio_eink4_plus_motor_set(false);
  if (epd_board_metalio_eink4_plus_pa_set(false) != ESP_OK) return fail("PA off");
  if (epd_board_metalio_eink4_plus_tps_enter_sleep() != ESP_OK) return fail("TPS sleep");
  epd_board_metalio_eink4_plus_bus_hold_low();

  if (gpio_set_direction(IO_EXPANDER_INT_GPIO, GPIO_MODE_INPUT) != ESP_OK ||
      gpio_set_pull_mode(IO_EXPANDER_INT_GPIO, GPIO_PULLUP_ONLY) != ESP_OK)
    return fail("IRQ configuration");
  // A failed transfer can still have changed the expander or one of the rails.
  maskAttempted = true;
  if (epd_board_metalio_eink4_plus_ioexp_mask_for_sleep() != ESP_OK) return fail("TCA mask");
  if (!epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin()) return fail("P17 input");
  if (!waitForIdleIrq()) return fail("shared IRQ low");

  (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (esp_sleep_enable_ext1_wakeup_io(1ULL << IO_EXPANDER_INT_GPIO, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK)
    return fail("EXT1 arm");
  railsAttempted = true;
  if (epd_board_metalio_eink4_plus_sleep_cut_rails() != ESP_OK) return fail("rail cut");
  if (!waitForIdleIrq()) return fail("IRQ low after rail cut");
  epd_board_metalio_eink4_plus_bus_latch_for_sleep();
  ESP_LOGI("PlusPower", "Deep sleep ready: TCA P17 -> GPIO%d EXT1 low", IO_EXPANDER_INT_GPIO);
  return true;
}
}  // namespace freeink
