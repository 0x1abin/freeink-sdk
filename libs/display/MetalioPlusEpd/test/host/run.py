#!/usr/bin/env python3
"""Compile the production Plus adapter and PanelDriver against a recording vendor API."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
DISPLAY = LIB.parent / "FreeInkDisplay"
required = [LIB / "src/MetalioPlusEpd.cpp", LIB / "include/MetalioPlusEpd.h",
            DISPLAY / "src/driver/MetalioPlusDriver.cpp", DISPLAY / "src/driver/MetalioPlusDriver.h"]
assert all(p.is_file() for p in required), "Plus production adapter and PanelDriver are not implemented"
with tempfile.TemporaryDirectory(prefix="freeink-metalio-plus-") as tmp:
    root = Path(tmp)
    for folder in ["src/driver", "src/bus", "include"]:
        (root / folder).mkdir(parents=True)
    for name in ["MetalioPlusDriver.h", "MetalioPlusDriver.cpp", "PanelDriver.h"]:
        shutil.copy2(DISPLAY / "src/driver" / name, root / "src/driver" / name)
    for name in ["GrayscaleCapabilities.h", "RefreshContext.h"]:
        shutil.copy2(DISPLAY / "include" / name, root / "include" / name)
    for name in ["Arduino.h", "EpdBus.h"]:
        destination = root / ("src/bus/EpdBus.h" if name == "EpdBus.h" else name)
        shutil.copy2(DISPLAY / "test/host/pro_stubs" / name, destination)
    (root / "BoardConfig.h").write_text("#pragma once\n#define FREEINK_DRIVER_METALIO_PLUS 1\n")
    (root / "esp_log.h").write_text("#pragma once\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) hostLog(__VA_ARGS__)\ntemplate <typename... Args> void hostLog(Args...) {}\n")
    stub = '''#pragma once
#include <cstdint>
#define MODE_DU 1
#define MODE_GC16 2
#define MODE_GL16 5
enum EpdDrawError { EPD_DRAW_SUCCESS=0, EPD_DRAW_FAILED=1 };
struct EpdiyHighlevelState { uint8_t *front_fb; };
extern uint8_t nativeFrame[1216*684/2];
extern EpdiyHighlevelState highlevel;
extern bool initOk,powerOk,offOk,clearOk,drawOk,portReadOk,geometryOk;
extern int initCalls,powerCalls,offCalls,clearCalls,drawCalls;
bool MetalioEpd_Init();
EpdiyHighlevelState* MetalioEpd_Hl();
int epd_width(); int epd_height();
void epd_poweron(); void epd_poweroff();
bool epd_board_metalio_eink4_plus_tps_ok();
bool epd_board_metalio_eink4_plus_ioexp_read_port(int,uint8_t*);
uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState*);
EpdDrawError epd_clear_checked();
EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*,int,int);
EpdDrawError epd_hl_update_screen(EpdiyHighlevelState*,int,int);
'''
    (root / "vendor_api.h").write_text(stub)
    for name in ["metalio_epd.h", "epdiy.h", "epd_board_metalio_eink4_plus.h"]:
        (root / name).write_text('#include "vendor_api.h"\n')
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
               "-Wno-unused-parameter", "-Werror", "-DFREEINK_DRIVER_METALIO_PLUS=1",
               "-I" + str(root), "-I" + str(root / "include"), "-I" + str(LIB / "include"),
               str(HERE / "test.cpp"), str(LIB / "src/MetalioPlusEpd.cpp"),
               str(root / "src/driver/MetalioPlusDriver.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    for case in ["modes", "resync", "sleep_recovery", "diff_failure", "diff_power_failure", "diff_off_failure", "success", "init_failure", "geometry_failure", "power_failure", "clear_failure",
                 "draw_failure", "off_failure", "port_failure"]:
        subprocess.run([str(root / "test"), case], check=True)
