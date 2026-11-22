#!/usr/bin/env python3
"""Compile production Imu and real board profiles against a recording Wire boundary."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HOST = Path(__file__).resolve().parent
ROOT = HOST.parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--profile", choices=("plus", "metalio", "readpico", "plus-disabled", "c3-disabled"))
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
profiles = {
    "plus": ["FREEINK_DEVICE_METALIO_EINK4_PLUS=1", "FREEINK_TEST_EXPECT_IMU=1"],
    "metalio": ["FREEINK_DEVICE_METALIO_EINK4=1", "FREEINK_TEST_EXPECT_IMU=1"],
    "readpico": ["FREEINK_DEVICE_READPICO=1", "FREEINK_TEST_EXPECT_IMU=1"],
    "plus-disabled": ["FREEINK_DEVICE_METALIO_EINK4_PLUS=1", "FREEINK_CAP_IMU=0", "FREEINK_TEST_EXPECT_IMU=0"],
    "c3-disabled": ["FREEINK_DEVICE_X4=1", "FREEINK_TEST_EXPECT_IMU=0"],
}
with tempfile.TemporaryDirectory(prefix="freeink-imu-mount-") as temporary:
    for name in ([args.profile] if args.profile else profiles):
        executable = Path(temporary) / name
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-parameter", "-Wno-unused-private-field", "-Wno-unused-function",
                   "-Wno-unused-const-variable"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += ["-D" + define for define in profiles[name]]
        command += ["-I" + str(HOST / "stubs"),
                    "-I" + str(ROOT / "libs/hardware/InputManager/test/host/metalio_stubs"),
                    "-I" + str(ROOT / "libs/hardware/BoardConfig/include"),
                    "-I" + str(ROOT / "libs/hardware/Imu/include"),
                    str(HOST / "test_sc7a20h_mount.cpp"),
                    str(ROOT / "libs/hardware/Imu/src/Imu.cpp"), "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
