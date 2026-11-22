#!/usr/bin/env python3
"""Exercise production Rtc.cpp and I2cBusLock with a recording Wire device."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
RTC = HERE.parents[1]
HARDWARE = RTC.parent
CASES = [
    "normal", "snapshot", "stopped", "test1", "testc", "voltage_low", "bcd", "year_bcd",
    "zero_month", "zero_day", "day_31_april", "bad_leap", "bad_1900_leap", "leap", "century",
    "weekday", "hour", "minute", "second", "read_failure", "pointer_failure", "short_read",
    "set", "set_stopped", "set_reserved", "set_test1", "set_test_both", "set_por_override", "set_century",
    "set_invalid_leap", "set_invalid_1900_leap", "set_month", "set_zero_month", "set_zero_day", "set_hour",
    "set_minute", "set_second", "set_weekday", "set_year_low", "set_year_high", "set_pointer_failure",
    *[f"set_failure{i}" for i in range(1, 5)],
    *[f"set_por_failure{i}" for i in range(1, 5)],
]

with tempfile.TemporaryDirectory(prefix="freeink-pcf8563-") as temporary:
    root = Path(temporary)
    (root / "soc").mkdir()
    (root / "soc/soc_caps.h").write_text("#pragma once\n#define SOC_I2C_NUM 2\n")
    (root / "freertos").mkdir()
    (root / "freertos/FreeRTOS.h").write_text("#pragma once\nconstexpr int portMAX_DELAY = -1;\n")
    (root / "freertos/semphr.h").write_text(r'''#pragma once
#include <cassert>
struct StaticSemaphore_t { unsigned depth = 0; };
using SemaphoreHandle_t = StaticSemaphore_t*;
inline unsigned hostLockDepth = 0, hostLockTakes = 0, hostLockGives = 0, hostRootLockSerial = 0;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* s) { return s; }
inline bool xSemaphoreTakeRecursive(SemaphoreHandle_t s, int) {
  if (hostLockDepth == 0) ++hostRootLockSerial;
  ++s->depth; ++hostLockDepth; ++hostLockTakes; return true;
}
inline void xSemaphoreGiveRecursive(SemaphoreHandle_t s) {
  assert(s->depth && hostLockDepth); --s->depth; --hostLockDepth; ++hostLockGives;
}
''')
    shared = HARDWARE / "BatteryMonitor/test/host/stubs"
    (root / "Wire.h").write_text(f'#pragma once\n#include "{shared / "Wire.h"}"\ninline TwoWire Wire1;\n')
    args = [os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-const-variable",  # Unselected RX8010 constants in the existing driver.
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DESP_PLATFORM=1",
            "-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1", "-DFREEINK_CAP_RTC=1"]
    for include in (root, HERE / "stubs", shared, RTC / "include", HARDWARE / "BoardConfig/include"):
        args += ["-I", str(include)]
    binary = root / "test"
    subprocess.run([*args, str(RTC / "src/Rtc.cpp"), str(HERE / "test_pcf8563.cpp"), "-o", str(binary)], check=True)
    failures = []
    for case in CASES:
        result = subprocess.run([str(binary), case], text=True, capture_output=True)
        print(result.stdout, end="")
        if result.returncode:
            failures.append(case)
            print(f"PCF8563 {case}: FAILED ({result.returncode})\n{result.stderr}", end="")
    if failures:
        raise SystemExit(f"PCF8563 failed {len(failures)}/{len(CASES)}: {', '.join(failures)}")
    print(f"PCF8563: {len(CASES)} production cases passed with ASan/UBSan")
