#include <BoardConfig.h>
#include <Imu.h>
#include <Wire.h>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
void chip(uint8_t address) {
  Wire.devices[address] = true;
  Wire.regs[address][0x0F] = 0x11;
  Wire.regs[address][0x70] = 0x28;
  Wire.regs[address][0x27] = 0x08;
}

void xyz(uint8_t address, int16_t x, int16_t y, int16_t z) {
  uint8_t reg = 0x28;
  for (int16_t value : {x, y, z}) {
    Wire.regs[address][reg++] = static_cast<uint8_t>(value);
    Wire.regs[address][reg++] = static_cast<uint16_t>(value) >> 8;
  }
}

bool sampleIs(const Imu::Sample& sample, float x, float y, float z, const char* context) {
  if (std::fabs(sample.ax - x) < 0.00001f && std::fabs(sample.ay - y) < 0.00001f &&
      std::fabs(sample.az - z) < 0.00001f && sample.gx == 0 && sample.gy == 0 && sample.gz == 0) {
    return true;
  }
  std::fprintf(stderr, "%s: expected accel=(%.6f,%.6f,%.6f), got=(%.6f,%.6f,%.6f), gyro=(%g,%g,%g)\n", context, x, y, z,
               sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz);
  return false;
}

bool everyMountCombination(Imu& imu, uint8_t address) {
  const auto original = BoardConfig::ACTIVE.sensors;
  struct Mount {
    bool swap, flipX, flipY;
    float x, y;
  };
  // Literal results for raw (0.5, -1, 0.25) g, with swap before sign flips.
#if FREEINK_DEVICE_READPICO
  // Read Pico retains its existing low-power / 1 mg decoder (raw >> 4).
  constexpr float z = 0.256f;
  constexpr Mount mounts[] = {{false, false, false, 0.512f, -1.024f}, {false, false, true, 0.512f, 1.024f},
                              {false, true, false, -0.512f, -1.024f}, {false, true, true, -0.512f, 1.024f},
                              {true, false, false, -1.024f, 0.512f},  {true, false, true, -1.024f, -0.512f},
                              {true, true, false, 1.024f, 0.512f},    {true, true, true, 1.024f, -0.512f}};
#else
  constexpr float z = 0.25f;
  constexpr Mount mounts[] = {{false, false, false, 0.5f, -1}, {false, false, true, 0.5f, 1},
                              {false, true, false, -0.5f, -1}, {false, true, true, -0.5f, 1},
                              {true, false, false, -1, 0.5f},  {true, false, true, -1, -0.5f},
                              {true, true, false, 1, 0.5f},    {true, true, true, 1, -0.5f}};
#endif
  xyz(address, 8192, -16384, 4096);
  for (const auto& mount : mounts) {
    auto& sensors = BoardConfig::ACTIVE.sensors;
    sensors.imuSwapXY = mount.swap;
    sensors.imuFlipX = mount.flipX;
    sensors.imuFlipY = mount.flipY;
    Imu::Sample sample{};
    assert(imu.read(sample));
    char context[80];
    std::snprintf(context, sizeof(context), "SC7 mount swap=%d flipX=%d flipY=%d", mount.swap, mount.flipX,
                  mount.flipY);
    if (!sampleIs(sample, mount.x, mount.y, z, context)) return false;
  }
  BoardConfig::ACTIVE.sensors = original;
  return true;
}

bool profilePoses(Imu& imu, uint8_t address) {
  struct Pose {
    int16_t rawX, rawY;
    float x, y;
  };
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  // Historical V1.4 calibration: (-rawY, -rawX). This is a pending EPDiy candidate.
  constexpr Pose poses[] = {{16384, 0, 0, -1}, {0, -16384, 1, 0}, {-16384, 0, 0, 1}, {0, 16384, -1, 0}};
#elif FREEINK_DEVICE_READPICO
  constexpr Pose poses[] = {{16000, 0, 1, 0}, {0, -16000, 0, -1}, {-16000, 0, -1, 0}, {0, 16000, 0, 1}};
#else
  constexpr Pose poses[] = {{16384, 0, 1, 0}, {0, -16384, 0, -1}, {-16384, 0, -1, 0}, {0, 16384, 0, 1}};
#endif
  for (const auto& pose : poses) {
    xyz(address, pose.rawX, pose.rawY, 0);
    Imu::Sample sample{};
    assert(imu.read(sample));
    if (!sampleIs(sample, pose.x, pose.y, 0, "real board profile pose")) return false;
  }
  return true;
}
}  // namespace

int main() {
  if (FREEINK_CAP_IMU != FREEINK_TEST_EXPECT_IMU) {
    std::fprintf(stderr, "%s: expected IMU=%d from the real board defaults, got IMU=%d\n", BoardConfig::ACTIVE.name,
                 FREEINK_TEST_EXPECT_IMU, FREEINK_CAP_IMU);
    return 1;
  }
  Imu imu;
  Imu::Sample sample{42, 42, 42, 42, 42, 42};
#if !FREEINK_CAP_IMU
  assert(!imu.begin() && !imu.present() && !imu.hasGyroscope());
  assert(!imu.read(sample) && !imu.sleep() && !imu.wake());
  assert(sample.ax == 42 && sample.ay == 42 && sample.az == 42 && sample.gx == 42 && sample.gy == 42 &&
         sample.gz == 42);
#else
  assert(BoardConfig::ACTIVE.sensors.imuType == BoardConfig::ImuType::Sc7a20h);
  const uint8_t address = BoardConfig::ACTIVE.sensors.imuAddr;
  chip(address);
  assert(imu.begin() && imu.present() && !imu.hasGyroscope());
  if (!everyMountCombination(imu, address) || !profilePoses(imu, address)) return 1;

  Wire.shortRead = true;
  assert(!imu.read(sample) && sample.ax == 42 && sample.ay == 42 && sample.az == 42 && sample.gx == 42 &&
         sample.gy == 42 && sample.gz == 42);
  Wire.shortRead = false;
  Wire.failReadReg = 0x28;
  assert(!imu.read(sample) && sample.ax == 42 && sample.ay == 42 && sample.az == 42);
  Wire.failReadReg = -1;
  Wire.failWriteReg = 0x20;
  assert(!imu.sleep());
  Wire.failWriteReg = -1;
  assert(imu.sleep());
#if FREEINK_DEVICE_READPICO
  assert(Wire.regs[address][0x20] == 0);
#else
  assert(Wire.regs[address][0x20] == 7 && !imu.read(sample));
#endif
  Wire.failWriteReg = 0x20;
  assert(!imu.wake());
  Wire.failWriteReg = -1;
  assert(imu.wake());
  if (!profilePoses(imu, address)) return 1;
#if !FREEINK_DEVICE_READPICO
  assert(Wire.regs[address][0x20] == 0x57);
  Wire.regs[address][0x27] = 0;
  sample = {42, 42, 42, 42, 42, 42};
  assert(!imu.read(sample) && sample.ax == 42 && sample.ay == 42 && sample.az == 42);
  Wire.regs[address][0x27] = 8;
  Wire.failReadReg = 0x27;
  assert(!imu.read(sample) && sample.ax == 42 && sample.ay == 42 && sample.az == 42);
  Wire.failReadReg = -1;
  const uint8_t alternate = address == 0x19 ? 0x18 : 0x19;
  Wire.devices[address] = false;
  chip(alternate);
  assert(imu.begin());
  if (!profilePoses(imu, alternate)) return 1;
  assert(imu.sleep() && imu.wake());
  if (!profilePoses(imu, alternate)) return 1;
#else
  assert(Wire.regs[address][0x20] == 0x2F);
#endif
#endif
  std::printf("%s: real profile, SC7 mount combinations/scaling and lifecycle passed (IMU=%d)\n",
              BoardConfig::ACTIVE.name, FREEINK_CAP_IMU);
}
