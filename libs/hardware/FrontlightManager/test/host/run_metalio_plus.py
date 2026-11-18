#!/usr/bin/env python3
"""Exercise real PWM/profile code, recording Arduino4 LEDC and GPIO effects."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="plus-frontlight-") as temporary:
    root = Path(temporary)
    (root / "driver").mkdir()
    (root / "Arduino.h").write_text('''#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
constexpr int HIGH=1,LOW=0,OUTPUT=3;
inline unsigned long fakeNow=0;
unsigned long millis(); void delay(unsigned long);
bool ledcAttach(uint8_t,uint32_t,uint8_t); bool ledcWrite(uint8_t,uint32_t); bool ledcDetach(uint8_t);
void pinMode(int,int); void digitalWrite(int,int);
inline int Serial=0;
''')
    (root / "driver/gpio.h").write_text('''#pragma once
using gpio_num_t=int;
void gpio_hold_dis(int); void gpio_hold_en(int);
''')
    for name, flags in [('Plus', ['FREEINK_DEVICE_METALIO_EINK4_PLUS=1']),
                        ('X4Pro regression', ['FREEINK_DEVICE_X4PRO=1'])]:
        executable = root / name.replace(' ', '_')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-DARDUINO=10819', '-DESP_ARDUINO_VERSION_MAJOR=4',
                        *['-D'+flag for flag in flags], '-I'+str(root), '-I'+str(HERE/'stubs'),
                        '-I'+str(LIB/'include'), '-I'+str(LIB.parent/'BoardConfig/include'),
                        str(HERE/'test_metalio_plus.cpp'), str(LIB/'src/FrontlightManager.cpp'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print(name + ' real PWM driver passed')
