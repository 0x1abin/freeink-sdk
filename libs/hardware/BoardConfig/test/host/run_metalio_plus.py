#!/usr/bin/env python3
"""Compile real profiles for the new S31 family and the existing C3/S3 families."""
from pathlib import Path
import os
import subprocess
import tempfile

LIB = Path(__file__).resolve().parents[2]
STUBS = LIB.parent / "XteinkDetect/test/host/stubs"
with tempfile.TemporaryDirectory(prefix="freeink-plus-profile-") as tmp:
    source = Path(tmp) / "profile.cpp"
    source.write_text('''#include <BoardConfig.h>
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
static_assert(FREEINK_MCU_S31 && !FREEINK_MCU_S3 && !FREEINK_MCU_C3);
static_assert(FREEINK_DRIVER_METALIO_PLUS && FREEINK_FB_PSRAM);
static_assert(BoardConfig::DEFAULT_DEVICE.board==BoardConfig::Board::MetalioEink4Plus);
static_assert(BoardConfig::DEFAULT_DEVICE.displayWidth==1216 && BoardConfig::DEFAULT_DEVICE.displayHeight==684);
static_assert(BoardConfig::MAX_FRAMEBUFFER_BYTES==103968);
static_assert(BoardConfig::DEFAULT_DEVICE.display.sclk==BoardConfig::PIN_UNASSIGNED);
static_assert(BoardConfig::DEFAULT_DEVICE.grayscaleLevels==2);
static_assert(FREEINK_CAP_TOUCH && FREEINK_TOUCH_FT6336U && FREEINK_SD_SDMMC && !FREEINK_CAP_BLE_HID_HOST);
static_assert(BoardConfig::DEFAULT_DEVICE.touch.reset==BoardConfig::PIN_UNASSIGNED);
static_assert(BoardConfig::DEFAULT_DEVICE.touch.sda==0 && BoardConfig::DEFAULT_DEVICE.touch.scl==1);
static_assert(BoardConfig::DEFAULT_DEVICE.sdmmc.busWidth==4 && BoardConfig::DEFAULT_DEVICE.sdmmc.clk==24);
static_assert(BoardConfig::DEFAULT_DEVICE.input.back==61);
static_assert(FREEINK_LOG_TRANSPORT==FREEINK_LOG_TRANSPORT_ROM_PRINTF);
#elif FREEINK_DEVICE_READPICO
static_assert(FREEINK_MCU_S3 && !FREEINK_MCU_S31);
static_assert(BoardConfig::DEFAULT_DEVICE.board==BoardConfig::Board::ReadPico);
static_assert(BoardConfig::MAX_FRAMEBUFFER_BYTES==103968 && FREEINK_DRIVER_EPDIY_LCD);
static_assert(FREEINK_LOG_TRANSPORT==FREEINK_LOG_TRANSPORT_SERIAL);
#else
static_assert(FREEINK_MCU_C3 && !FREEINK_MCU_S31);
static_assert(BoardConfig::MAX_FRAMEBUFFER_BYTES==52272);
static_assert(BoardConfig::DEFAULT_DEVICE.board==BoardConfig::Board::XteinkX4);
static_assert(FREEINK_LOG_TRANSPORT==FREEINK_LOG_TRANSPORT_SERIAL);
#endif
''')
    for name, flags in [
        ("Plus V1.4", ["FREEINK_DEVICE_METALIO_EINK4_PLUS=1"]),
        ("shared X3/X4", ["FREEINK_DEVICE_X3=1", "FREEINK_DEVICE_X4=1"]),
        ("ReadPico S3", ["FREEINK_DEVICE_READPICO=1"]),
    ]:
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsyntax-only", "-I" + str(STUBS), "-I" + str(LIB / "include"),
                        *["-D"+flag for flag in flags], str(source)], check=True)
        print(name + " profile: passed")

    mixed = subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-fsyntax-only",
                            "-I" + str(STUBS), "-I" + str(LIB / "include"),
                            "-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1", "-DFREEINK_DEVICE_READPICO=1",
                            str(source)], capture_output=True, text=True)
    assert mixed.returncode != 0 and "one MCU family" in mixed.stderr
    print("Mixed S31/S3 image: correctly rejected")
