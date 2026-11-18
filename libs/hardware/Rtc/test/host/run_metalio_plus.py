#!/usr/bin/env python3
"""Compile actual RTC/native IO; exercise coherent reads and bounded writes."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SDK = HERE.parents[4]
IO = SDK / "libs/display/MetalioPlusEpd"
RTC = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="freeink-plus-rtc-") as temporary:
    root = Path(temporary)
    (root / "soc").mkdir()
    (root / "soc/soc_caps.h").write_text("#pragma once\n#define SOC_I2C_NUM 2\n")
    wire = SDK / "libs/hardware/BatteryMonitor/test/host/stubs/Wire.h"
    (root / "Wire.h").write_text(f'#pragma once\n#include "{wire}"\ninline TwoWire Wire1;\n')
    profile = (HERE / "stubs/BoardConfig.h").read_text()
    profile = profile.replace("47, i2cScl = 48", "0, i2cScl = 1").replace("rtcAddr = 0x32, i2cBus = 0", "rtcAddr = 0x51, i2cBus = 1")
    profile = profile.replace("rtcType = RtcType::Rx8130", "rtcType = RtcType::Pcf8563")
    (root / "BoardConfig.h").write_text(profile)
    (root / "native.h").write_text(r'''#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t=int;
constexpr int ESP_OK=0, I2C_NUM_1=1;
extern "C" {
bool metalio_i2c1_lock(int);
void metalio_i2c1_unlock();
esp_err_t metalio_i2c1_transmit_receive_locked(int,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,int);
esp_err_t metalio_i2c1_write_reg_locked(int,uint8_t,uint8_t,const uint8_t*,size_t,int);
bool epd_board_metalio_eink4_plus_ioexp_ok();
void epd_board_metalio_eink4_plus_tp_reset();
bool epd_board_metalio_eink4_plus_ioexp_read_port(int,uint8_t*);
}
''')
    for name in ("metalio_i2c1.h", "epd_board_metalio_eink4_plus.h"):
        (root / name).write_text('#include "native.h"\n')
    args = [os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
            "-Wno-error=unused-const-variable",  # Existing other-board register constants under Apple clang.
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1", "-DFREEINK_CAP_RTC=1"]
    for include in (root, SDK / "libs/hardware/BatteryMonitor/test/host/stubs", RTC / "include",
                    SDK / "libs/hardware/BoardConfig/include", IO / "include"):
        args += ["-I", str(include)]
    executable = root / "test"
    subprocess.run([*args, str(RTC / "src/Rtc.cpp"), str(IO / "src/MetalioPlusIo.cpp"),
                    str(HERE / "test_metalio_plus.cpp"), "-o", str(executable)], check=True)
    cases = ["normal", "read_only_begin", "not_ready", "lock", "read_failure", "stopped", "test1", "testc",
             "voltage_low", "bcd", "zero_month", "zero_day", "bad_leap", "century", "weekday",
             "set", "set_stopped", "set_reserved", "set_test", "set_invalid_leap", "set_month",
             "set_hour", "set_weekday", "set_year_low", "set_year_high", "set_lock", "no_timer",
             "set_failure1", "set_failure2", "set_failure3", "set_failure4",
             "set_por_override", "set_test_both",
             "set_por_failure1", "set_por_failure2", "set_por_failure3", "set_por_failure4"]
    for case in cases:
        subprocess.run([str(executable), case], check=True)
    print(f"Plus RTC: {len(cases)} production cases passed with ASan/UBSan")
