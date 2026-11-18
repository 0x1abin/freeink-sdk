#!/usr/bin/env python3
"""Compile the real Plus power adapter against a fault-injecting native API."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="metalio-plus-power-") as temporary:
    root = Path(temporary)
    (root / "driver").mkdir()
    (root / "Arduino.h").write_text("#pragma once\nunsigned long millis(); void delay(unsigned long);\n")
    (root / "esp_log.h").write_text("#pragma once\ninline void testLog(const char*,...) {}\n#define ESP_LOGE(...) testLog(__VA_ARGS__)\n#define ESP_LOGI(...) testLog(__VA_ARGS__)\n")
    api = '''#pragma once
#include <cstdint>
extern "C" {
using esp_err_t=int; using gpio_num_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, GPIO_MODE_OUTPUT=1, GPIO_MODE_INPUT=2;
constexpr int GPIO_FLOATING=0,GPIO_PULLUP_ONLY=1,ESP_SLEEP_WAKEUP_ALL=0,ESP_SLEEP_WAKEUP_EXT1=1;
constexpr int ESP_EXT1_WAKEUP_ANY_LOW=0;
bool epd_board_metalio_eink4_plus_ioexp_ok();
esp_err_t epd_board_metalio_eink4_plus_pa_set(bool);
void epd_board_metalio_eink4_plus_motor_set(bool);
esp_err_t epd_board_metalio_eink4_plus_tps_enter_sleep();
void epd_board_metalio_eink4_plus_bus_hold_low();
void epd_board_metalio_eink4_plus_bus_latch_for_sleep();
esp_err_t epd_board_metalio_eink4_plus_ioexp_mask_for_sleep();
bool epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin();
esp_err_t epd_board_metalio_eink4_plus_ioexp_restore_keys();
esp_err_t epd_board_metalio_eink4_plus_sleep_cut_rails();
esp_err_t epd_board_metalio_eink4_plus_sleep_restore_rails();
esp_err_t gpio_hold_dis(gpio_num_t); esp_err_t gpio_hold_en(gpio_num_t);
esp_err_t gpio_reset_pin(gpio_num_t); esp_err_t gpio_set_direction(gpio_num_t,int);
esp_err_t gpio_set_pull_mode(gpio_num_t,int); esp_err_t gpio_set_level(gpio_num_t,int);
int gpio_get_level(gpio_num_t);
bool esp_sleep_is_valid_wakeup_gpio(gpio_num_t);
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t,int);
esp_err_t esp_sleep_disable_wakeup_source(int);
}
'''
    (root / "native_api.h").write_text(api)
    for name in ["epd_board_metalio_eink4_plus.h", "driver/gpio.h", "esp_sleep.h"]:
        (root / name).write_text('#include "native_api.h"\n')
    (root / "config.h").write_text("""#pragma once
#define IO_EXPANDER_INT_GPIO 2
#define FL_COOL_PIN 6
#define FL_WARM_PIN 7
#define SDMMC_CLK_PIN 24
#define SDMMC_CMD_PIN 25
#define SDMMC_D0_PIN 20
#define SDMMC_D1_PIN 21
#define SDMMC_D2_PIN 22
#define SDMMC_D3_PIN 23
""")
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
               "-I" + str(root), "-I" + str(LIB / "include"), str(HERE / "test_power.cpp"),
               str(LIB / "src/MetalioPlusPower.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    cases = ["success", "held_then_released", "held_forever", "button_read", "not_ready", "invalid_pad",
             "pa", "tps", "park", "irq_config", "mask", "power_input", "irq_low", "arm",
             "rails", "irq_after_cut", "restore_fail"]
    for case in cases:
        subprocess.run([str(root / "test"), case], check=True)
    print(f"Plus power: {len(cases)} production cases passed")
