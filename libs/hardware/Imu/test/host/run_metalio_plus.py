#!/usr/bin/env python3
"""Compile production Plus IMU/native I/O against a recording native bus."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SDK = HERE.parents[4]
IO = SDK / "libs/display/MetalioPlusEpd"
with tempfile.TemporaryDirectory(prefix="freeink-plus-imu-") as temporary:
    root = Path(temporary)
    (root / "soc").mkdir()
    (root / "soc/soc_caps.h").write_text("#pragma once\n#define SOC_I2C_NUM 2\n")
    wire = (HERE / "stubs/Wire.h").read_text()
    wire = wire.replace("struct TwoWire {", "struct TwoWire {\n unsigned beginCalls=0, transmissions=0, requests=0;")
    wire = wire.replace("return sda == 41", "++beginCalls; return sda == 41")
    wire = wire.replace("address = a; tx.clear();", "++transmissions; address = a; tx.clear();")
    (root / "Wire.h").write_text(wire + "\ninline TwoWire Wire1;\n")
    (root / "BoardConfig.h").write_text('''#pragma once
#include <cstdint>
namespace BoardConfig {
enum class ImuType {None,Lsm6ds3,Qmi8658,Sc7a20h,Bmi270};
struct Sensors {
 int i2cSda=0,i2cScl=1; uint32_t i2cHz=100000;
 uint8_t imuAddr=0x19,i2cBus=1; ImuType imuType=ImuType::Sc7a20h;
 bool imuSwapXY=false,imuFlipX=false,imuFlipY=false;
};
struct Profile { Sensors sensors; }; inline Profile ACTIVE;
}
''')
    board_stub = root / "BoardConfig.h"
    board_stub.write_text('#if defined(PLUS_IMU_REAL_PROFILE)\n#include "' +
                          str(SDK / "libs/hardware/BoardConfig/include/BoardConfig.h") +
                          '"\n#else\n' + board_stub.read_text() + '\n#endif\n')
    native = '''#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t=int; constexpr int ESP_OK=0,I2C_NUM_1=1;
extern "C" {
bool metalio_i2c1_lock(int); void metalio_i2c1_unlock();
int metalio_i2c1_transmit_receive_locked(int,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,int);
int metalio_i2c1_write_reg_locked(int,uint8_t,uint8_t,const uint8_t*,size_t,int);
bool epd_board_metalio_eink4_plus_ioexp_ok();
void epd_board_metalio_eink4_plus_tp_reset();
bool epd_board_metalio_eink4_plus_ioexp_read_port(int,uint8_t*);
}
'''
    (root / "native.h").write_text(native)
    for name in ("metalio_i2c1.h", "epd_board_metalio_eink4_plus.h"):
        (root / name).write_text('#include "native.h"\n')
    args = [os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-Wno-unused-const-variable", "-Wno-unused-private-field",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1", "-DFREEINK_CAP_IMU=1"]
    for include in (root, SDK / "libs/hardware/BatteryMonitor/test/host/stubs",
                    SDK / "libs/hardware/BoardConfig/include", HERE.parents[1] / "include", IO / "include"):
        args += ["-I", str(include)]
    exe = root / "test"
    subprocess.run([*args, str(HERE.parents[1] / "src/Imu.cpp"), str(IO / "src/MetalioPlusIo.cpp"),
                    str(HERE / "test_metalio_plus.cpp"), "-o", str(exe)], check=True)
    cases = ["normal", "id", "version", "irq1", "irq2", "filter", "fifo", "fifo_ctrl", "bus", "lock",
             "no_data", "read_status", "read_partial", "mount", "sleep_fail", "wake_route"]
    cases += [f"begin_fail_{n}" for n in range(1, 9)]
    cases += [f"wake_fail_{n}" for n in range(1, 10)]
    for case in cases:
        subprocess.run([str(exe), case], check=True)
    print(f"Plus IMU: {len(cases)} production cases passed with ASan/UBSan")

    # Exercise Imu::read() with the actual Plus board profile, not the generic seam.
    real_exe = root / "test-v14-mount"
    subprocess.run([*args, "-I", str(SDK / "libs/hardware/XteinkDetect/test/host/stubs"),
                    "-DPLUS_IMU_REAL_PROFILE=1", str(HERE.parents[1] / "src/Imu.cpp"),
                    str(IO / "src/MetalioPlusIo.cpp"), str(HERE / "test_metalio_plus.cpp"),
                    "-o", str(real_exe)], check=True)
    subprocess.run([str(real_exe), "v14_pose_frame"], check=True)
