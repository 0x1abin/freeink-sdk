#!/usr/bin/env python3
"""Compile real Plus native IO and InputManager; external BSP/IDF calls are recorded."""
from pathlib import Path
import os, shutil, subprocess, tempfile
HERE=Path(__file__).resolve().parent
SDK=HERE.parents[4]
IO=SDK/'libs/display/MetalioPlusEpd'
with tempfile.TemporaryDirectory(prefix='freeink-plus-input-') as tmp:
    p=Path(tmp)
    shutil.copytree(HERE/'metalio_stubs',p/'stubs')
    a=p/'stubs/Arduino.h'
    a.write_text(a.read_text().replace('[49]','[64]'))
    (p/'stubs/native.h').write_text(r"""#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t=int;
constexpr int ESP_OK=0, I2C_NUM_1=1;
extern bool busOk, lockOk, ioReady, portOk;
extern unsigned lockCalls, unlockCalls, resetCalls, readCalls, writeCalls;
extern bool held;
extern uint8_t port1, report[11];
extern "C" {
bool metalio_i2c1_lock(int);
void metalio_i2c1_unlock();
esp_err_t metalio_i2c1_transmit_receive_locked(int,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,int);
esp_err_t metalio_i2c1_write_reg_locked(int,uint8_t,uint8_t,const uint8_t*,size_t,int);
bool epd_board_metalio_eink4_plus_ioexp_ok();
void epd_board_metalio_eink4_plus_tp_reset();
bool epd_board_metalio_eink4_plus_ioexp_read_port(int,uint8_t*);
}
""")
    for n in ['metalio_i2c1.h','epd_board_metalio_eink4_plus.h']:
        (p/'stubs'/n).write_text('#include "native.h"\n')
    command=[os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
             '-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1','-I'+str(p/'stubs'),
             '-I'+str(SDK/'libs/hardware/BoardConfig/include'),'-I'+str(HERE.parents[1]/'include'),
             '-I'+str(IO/'include'),str(HERE/'test_metalio_plus.cpp'),
             str(HERE.parents[1]/'src/InputManager.cpp'),str(IO/'src/MetalioPlusIo.cpp'),'-o',str(p/'test')]
    subprocess.run(command,check=True)
    subprocess.run([str(p/'test'),os.environ.get('PLUS_TEST_MODE','all')],check=True)

    disabled = command[:command.index(str(HERE/'test_metalio_plus.cpp'))] + ['-DFREEINK_CAP_TOUCH=0','-fsyntax-only',str(HERE.parents[1]/'src/InputManager.cpp')]
    subprocess.run(disabled,check=True)
