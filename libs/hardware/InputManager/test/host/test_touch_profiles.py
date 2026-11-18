#!/usr/bin/env python3
"""Compile real board profiles across the fork/upstream touch-driver union."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
BOARD = HERE.parents[2] / "BoardConfig/include"
SOURCE = r"""
#include <Arduino.h>
using HardwareSerial = SerialStub;
inline HardwareSerial Serial0;
#include <BoardConfig.h>
using namespace BoardConfig;
static_assert(static_cast<unsigned>(TouchController::Gslx680) == 4);
static_assert(static_cast<unsigned>(TouchController::Ft6336u) == 5);
static_assert(static_cast<unsigned>(TouchController::Cst836u) == 7);
static_assert(static_cast<unsigned>(TouchController::Tma525c) == 8);
static_assert(static_cast<unsigned>(GaugeType::Cw32L010Pmu) == 3);
static_assert(static_cast<unsigned>(GaugeType::Cw2015) == 4);
static_assert(static_cast<unsigned>(Board::Silkscreen) == 8);
static_assert(Board::DeLink == Board::Silkscreen);
static_assert(static_cast<unsigned>(Board::WaveshareEpaper397) == 15);
static_assert(static_cast<unsigned>(Board::OnePage) == 16);
static_assert(static_cast<unsigned>(Board::MetalioEink4) == 17);
static_assert(static_cast<unsigned>(Board::ReadPico) == 18);
static_assert(static_cast<unsigned>(Board::WsEpaper397) == 19);
static_assert(static_cast<unsigned>(Board::MetalioEInk4) == 20);
static_assert(static_cast<unsigned>(Board::Picco) == 21);
static_assert(FREEINK_FB_PSRAM == EXPECT_FB_PSRAM);
static_assert(MAX_FRAMEBUFFER_BYTES >= panelBytes(DEFAULT_DEVICE));
#if FREEINK_MCU_C3
static_assert(!FREEINK_FB_PSRAM && MAX_FRAMEBUFFER_BYTES <= 52272);
#endif
#if FREEINK_DEVICE_SILKSCREEN
static_assert(FREEINK_DEVICE_DELINK && FREEINK_MCU_S3);
static_assert(DEFAULT_DEVICE.board == Board::Silkscreen);
static_assert(DE_LINK.displayWidth == SILKSCREEN.displayWidth);
#endif
static_assert(EEGO_A4.statusBarOffsetY == 6 && EEGO_A4.bottomCornerRadius == 40);
static_assert(EEGO_A4.display.powerEnable == 6);
static_assert(EEGO_A4.sd.powerEnable == PIN_UNASSIGNED);
static_assert(EEGO_A4.grayscaleLevels == 4 && EEGO_A4.headerOffsetY == 6);
static_assert(EEGO_A4.cornerRadius == 24 && EEGO_A4.tabBarOffsetY == 8);
static_assert(EEGO_A4.tabBarHeightPct == 80 && EEGO_A4.wifiMaxTxPower == 60);
static_assert(M5PAPER_S3.grayscaleLevels == 4 && M5PAPER_S3.headerOffsetY == 0);
static_assert(!M5PAPER_S3.usbDetectActiveHigh);
#if FREEINK_DEVICE_READPICO
static_assert(FREEINK_CAP_TOUCH && FREEINK_TOUCH_CST836U);
static_assert(touchDriverCompiled(READ_PICO.touch.controller));
#endif
#if FREEINK_DEVICE_MURPHY_M4
static_assert(FREEINK_TOUCH_FT6336U);
static_assert(touchDriverCompiled(MURPHY_M4.touch.controller));
#endif
#if FREEINK_DEVICE_X3
static_assert(!FREEINK_CAP_TOUCH && !FREEINK_TOUCH_GSLX680 && !FREEINK_TOUCH_CST836U);
#endif
"""

with tempfile.TemporaryDirectory(prefix="freeink-touch-profiles-") as directory:
    source = Path(directory) / "profiles.cpp"
    source.write_text(SOURCE)
    compiler = [os.environ.get("CXX", "c++"), "-std=c++17", "-fsyntax-only", "-Wno-deprecated-declarations",
                "-I" + str(HERE / "metalio_stubs"), "-I" + str(BOARD), str(source)]
    profiles = [
        (["FREEINK_DEVICE_X3"], 0),
        (["FREEINK_DEVICE_X4"], 0),
        (["FREEINK_DEVICE_SILKSCREEN"], 0),
        (["FREEINK_DEVICE_DELINK"], 0),
        (["FREEINK_DEVICE_READPICO"], 1),
        (["FREEINK_DEVICE_MURPHY_M4"], 0),
        (["FREEINK_DEVICE_PICCO"], 1),
        (["FREEINK_DEVICE_STICKY"], 0),
        (["FREEINK_DEVICE_STICKY", "BOARD_HAS_PSRAM"], 1),
        (["FREEINK_DEVICE_READPICO", "FREEINK_DEVICE_MURPHY_M4", "FREEINK_DEVICE_EEGO_A4",
          "FREEINK_DEVICE_METALIO_EINK4", "FREEINK_DEVICE_PICCO", "FREEINK_DEVICE_STICKY"], 1),
    ]
    for selected, psram in profiles:
        subprocess.run(compiler + ["-D" + name + "=1" for name in selected] +
                       ["-DEXPECT_FB_PSRAM=" + str(psram)], check=True)
    subprocess.run(compiler + ["-DFREEINK_DEVICE_STICKY=1", "-DBOARD_HAS_PSRAM=1",
                               "-DFREEINK_FB_PSRAM=0", "-DEXPECT_FB_PSRAM=0"], check=True)
    rejected = subprocess.run(compiler + ["-DFREEINK_DEVICE_READPICO=1", "-DFREEINK_TOUCH_CST836U=0",
                                          "-DEXPECT_FB_PSRAM=1"], text=True, capture_output=True)
    assert rejected.returncode != 0 and "has no InputManager driver" in rejected.stderr
print("Board aliases/IDs, PSRAM policies, profile union and missing-driver rejection passed")
