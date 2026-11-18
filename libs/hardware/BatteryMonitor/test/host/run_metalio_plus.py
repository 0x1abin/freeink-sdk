#!/usr/bin/env python3
"""Real battery monitor/native IO with recording IDF boundary, no hardware."""
from pathlib import Path
import os, subprocess, tempfile
HERE=Path(__file__).resolve().parent
SDK=HERE.parents[4]
IO=SDK/'libs/display/MetalioPlusEpd'
with tempfile.TemporaryDirectory(prefix='freeink-plus-battery-') as tmp:
 p=Path(tmp)
 (p/'native.h').write_text(r"""#pragma once
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
""")
 for n in ['metalio_i2c1.h','epd_board_metalio_eink4_plus.h']:
  (p/n).write_text('#include "native.h"\n')
 command=[os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
 '-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1',
 '-I'+str(p),'-I'+str(HERE/'stubs'),'-I'+str(HERE.parents[1]/'include'),
 '-I'+str(SDK/'libs/hardware/BoardConfig/include'),'-I'+str(SDK/'libs/hardware/XteinkDetect/test/host/stubs'),
 '-I'+str(IO/'include'),str(HERE/'test_metalio_plus.cpp'),str(IO/'src/MetalioPlusIo.cpp'),'-o',str(p/'test')]
 subprocess.run(command,check=True)
 for case in ['normal','current_negative','current_zero','current_minus_one','invalid_soc','invalid_voltage',
              'voltage_failure','soc_failure','lock_failure','lock_retry','cache','expiry','wrap','readonly',
              'soc_zero','soc_full','soc_over','voltage_zero','voltage_low','voltage_high','voltage_min','voltage_max']:
  subprocess.run([str(p/'test'),case],check=True)
