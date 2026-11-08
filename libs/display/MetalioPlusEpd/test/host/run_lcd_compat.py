"""Compile the production LCD clock shim against both known HAL signatures."""

from pathlib import Path
import subprocess
import tempfile

LIB = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="metalio-plus-lcd-") as tmp:
    root = Path(tmp)
    (root / "hal").mkdir()
    (root / "sdkconfig.h").write_text("#define CONFIG_SPIRAM_SPEED 200\n")
    (root / "hal/lcd_ll.h").write_text('''#pragma once
#include <stdbool.h>
typedef struct { int unused; } lcd_cam_dev_t;
typedef enum { LCD_SRC_TEST = 1 } lcd_clock_source_t;
static int selected, divided;
static lcd_cam_dev_t* selected_dev;
static lcd_cam_dev_t* divided_dev;
#if TEST_NATIVE_HAL
static inline void lcd_ll_select_clk_src(lcd_cam_dev_t* dev, lcd_clock_source_t src) {
 selected_dev=dev; selected=src;
}
static inline void lcd_ll_set_group_clock_coeff(lcd_cam_dev_t* dev, int n, int a, int b) {
 divided_dev=dev; divided=n+a+b;
}
#else
static inline void lcd_ll_select_clk_src(int group, lcd_clock_source_t src) {
 selected=src+group;
}
static inline void lcd_ll_set_group_clock_coeff(int group, int n, int a, int b) {
 divided=n+a+b+group;
}
#endif
''')
    source = root / "test.c"
    source.write_text('''#include "LcdClockCompat.h"
#include <assert.h>
int main(void) {
 lcd_cam_dev_t dev={0};
 lcd_ll_select_clk_src(&dev,LCD_SRC_TEST);
 lcd_ll_set_group_clock_coeff(&dev,3,4,5);
 assert(selected==1 && divided==12);
#if TEST_NATIVE_HAL
 assert(selected_dev==&dev && divided_dev==&dev);
#endif
}
''')
    for native in (0, 1):
        exe = root / f"test-{native}"
        command = ["cc", "-std=gnu17", f"-DFREEINK_METALIO_PLUS_NATIVE_IDF={native}",
                   f"-DTEST_NATIVE_HAL={native}", "-I" + str(root), "-I" + str(LIB / "src"),
                   str(source), "-o", str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
    bad = subprocess.run(["cc", "-std=gnu17", "-DFREEINK_METALIO_PLUS_NATIVE_IDF=1",
                          "-DTEST_NATIVE_HAL=0", "-I" + str(root), "-I" + str(LIB / "src"),
                          "-c", str(source), "-o", str(root / "bad.o")], capture_output=True, text=True)
    assert bad.returncode != 0 and "static assertion" in bad.stderr, bad.stderr
    (root / "sdkconfig.h").write_text("#define CONFIG_SPIRAM_SPEED 250\n")
    stale = subprocess.run(["cc", "-std=gnu17", "-DFREEINK_METALIO_PLUS_NATIVE_IDF=1",
                            "-DTEST_NATIVE_HAL=1", "-I" + str(root), "-I" + str(LIB / "src"),
                            "-c", str(source), "-o", str(root / "stale.o")], capture_output=True, text=True)
    assert stale.returncode != 0 and "static assertion" in stale.stderr, stale.stderr

print("LCD clock contract: both HALs pass; incompatible signature and stale 250MHz core rejected")
