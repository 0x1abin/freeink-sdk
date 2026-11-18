#include <MetalioPlusIo.h>
#include <MetalioPlusPower.h>
#include <native_api.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
std::string fault;
std::vector<std::string> events;
std::map<int, int> levels;
std::map<int, bool> held;
unsigned long now = 0;
bool armed = false, cut = false;
int keyRestores = 0, railRestores = 0;
int step(const char* name) {
  events.emplace_back(name);
  return fault == name ? ESP_FAIL : ESP_OK;
}
int at(const char* name) {
  const auto found = std::find(events.begin(), events.end(), name);
  assert(found != events.end());
  return static_cast<int>(found - events.begin());
}
}  // namespace

unsigned long millis() { return now; }
void delay(unsigned long ms) { now += ms; }
namespace freeink {
bool metalioPlusReadButtons(uint8_t& mask) {
  mask = (fault == "held_forever" || (fault == "held_then_released" && now < 60)) ? 0x40 : 0;
  return fault != "button_read";
}
}  // namespace freeink

bool epd_board_metalio_eink4_plus_ioexp_ok() { return fault != "not_ready"; }
esp_err_t epd_board_metalio_eink4_plus_pa_set(bool on) {
  assert(!on);
  return step("pa");
}
void epd_board_metalio_eink4_plus_motor_set(bool on) {
  assert(!on);
  step("motor");
}
esp_err_t epd_board_metalio_eink4_plus_tps_enter_sleep() { return step("tps"); }
void epd_board_metalio_eink4_plus_bus_hold_low() { step("bus"); }
void epd_board_metalio_eink4_plus_bus_latch_for_sleep() {
  assert(armed && cut);
  step("latch");
}
esp_err_t epd_board_metalio_eink4_plus_ioexp_mask_for_sleep() { return step("mask"); }
bool epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin() { return step("power_input") == ESP_OK; }
esp_err_t epd_board_metalio_eink4_plus_ioexp_restore_keys() {
  ++keyRestores;
  return fault == "restore_fail" ? ESP_FAIL : step("restore_keys");
}
esp_err_t epd_board_metalio_eink4_plus_sleep_cut_rails() {
  assert(armed);
  // A failed transaction may still have changed a rail.
  cut = true;
  return step("rails");
}
esp_err_t epd_board_metalio_eink4_plus_sleep_restore_rails() {
  ++railRestores;
  cut = false;
  return fault == "restore_fail" ? ESP_FAIL : step("restore_rails");
}
esp_err_t gpio_hold_dis(int pin) {
  held[pin] = false;
  return ESP_OK;
}
esp_err_t gpio_hold_en(int pin) {
  held[pin] = true;
  return ESP_OK;
}
esp_err_t gpio_reset_pin(int) { return fault == "park" ? ESP_FAIL : ESP_OK; }
esp_err_t gpio_set_direction(int pin, int mode) {
  assert((pin == 2 && mode == GPIO_MODE_INPUT) || (pin != 2 && mode == GPIO_MODE_OUTPUT));
  return fault == "irq_config" && pin == 2 ? ESP_FAIL : ESP_OK;
}
esp_err_t gpio_set_pull_mode(int, int) { return ESP_OK; }
esp_err_t gpio_set_level(int pin, int level) {
  assert(level == 0);
  levels[pin] = level;
  return ESP_OK;
}
int gpio_get_level(int pin) {
  assert(pin == 2);
  return fault == "irq_low" || ((fault == "irq_after_cut" || fault == "restore_fail") && cut) ? 0 : 1;
}
bool esp_sleep_is_valid_wakeup_gpio(int pin) {
  assert(pin == 2);
  return fault != "invalid_pad";
}
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t mask, int mode) {
  assert(mask == (1ULL << 2) && mode == ESP_EXT1_WAKEUP_ANY_LOW);
  const auto error = step("arm");
  armed = error == ESP_OK;
  return error;
}
esp_err_t esp_sleep_disable_wakeup_source(int source) {
  assert(source == ESP_SLEEP_WAKEUP_ALL || source == ESP_SLEEP_WAKEUP_EXT1);
  armed = false;
  return ESP_OK;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  fault = argv[1];
  const bool success = fault == "success" || fault == "held_then_released";
  assert(freeink::metalioPlusPrepareDeepSleep() == success);
  if (success) {
    assert(armed && cut && keyRestores == 0 && railRestores == 0);
    assert(at("tps") < at("bus") && at("bus") < at("mask"));
    assert(at("mask") < at("power_input") && at("power_input") < at("arm"));
    assert(at("arm") < at("rails") && at("rails") < at("latch"));
    for (const int pin : {6, 7, 20, 21, 22, 23, 24, 25}) assert(held[pin] && levels[pin] == 0);
    if (fault == "held_then_released") assert(now >= 60);
  } else {
    assert(!armed);
    if (fault == "rails" || fault == "irq_after_cut" || fault == "restore_fail") assert(railRestores == 1);
    if (fault == "mask" || fault == "power_input" || fault == "irq_low" || fault == "arm" || fault == "rails" ||
        fault == "irq_after_cut" || fault == "restore_fail")
      assert(keyRestores == 1);
    if (fault == "held_forever") assert(now >= 5000 && now <= 5020);
  }
  std::printf("Plus power %s passed\n", argv[1]);
}
