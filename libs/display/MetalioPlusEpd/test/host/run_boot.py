#!/usr/bin/env python3
"""Compile the real early rail restoration against a register-level TCA fake."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="metalio-plus-boot-") as temporary:
    root = Path(temporary)
    (root / "driver").mkdir()
    (root / "Arduino.h").write_text("#pragma once\nvoid delay(unsigned long);\n")
    (root / "esp_log.h").write_text("#pragma once\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n")
    (root / "native_api.h").write_text('''#pragma once
#include <cstdint>
#include <cstddef>
extern "C" {
using esp_err_t=int; using gpio_num_t=int; using i2c_port_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, I2C_NUM_1=1;
esp_err_t gpio_hold_dis(gpio_num_t);
esp_err_t metalio_i2c1_init(gpio_num_t,gpio_num_t);
bool metalio_i2c1_lock(int); void metalio_i2c1_unlock();
esp_err_t metalio_i2c1_probe_locked(i2c_port_t,uint8_t,int);
esp_err_t metalio_i2c1_read_reg_stop_locked(i2c_port_t,uint8_t,uint8_t,uint8_t*,size_t,int);
esp_err_t metalio_i2c1_write_reg_locked(i2c_port_t,uint8_t,uint8_t,const uint8_t*,size_t,int);
}
''')
    for name in ("driver/gpio.h", "metalio_i2c1.h"):
        (root / name).write_text('#include "native_api.h"\n')
    (root / "config.h").write_text('''#pragma once
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define EPD_IO_CAM_SCR_EN 5
#define EPD_IO_BT_PA_PWR 12
#define POWER_RAIL_SETTLE_MS 80
''')
    sources = [str(HERE / "test_boot.cpp"), str(LIB / "src/MetalioPlusBoot.cpp")]
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root), "-I" + str(LIB / "include"), *sources,
                    "-o", str(root / "test")], check=True)
    cases = ["retained", "cold", "alternate", "sda", "scl", "init", "lock", "missing",
             "read3", "read7", "write3", "write7", "read2", "read6", "write2", "write6"]
    for case in cases:
        subprocess.run([str(root / "test"), case], check=True)
    print(f"Plus early rails: {len(cases)} production cases passed")
