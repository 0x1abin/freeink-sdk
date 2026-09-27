#include "Imu.h"

#include <BoardConfig.h>

#if FREEINK_CAP_IMU

#include <Wire.h>
#include <soc/soc_caps.h>

namespace freeink {
namespace {

// LSM6DS3TR-C register map (datasheet).
constexpr uint8_t REG_WHO_AM_I = 0x0F;
constexpr uint8_t WHO_AM_I_VALUE = 0x6A;  // shared by LSM6DS3 / LSM6DS3TR-C
constexpr uint8_t REG_CTRL1_XL = 0x10;    // accel: ODR + full scale
constexpr uint8_t REG_CTRL2_G = 0x11;     // gyro: ODR + full scale
constexpr uint8_t REG_CTRL3_C = 0x12;     // BDU / auto-increment
constexpr uint8_t REG_OUTX_L_G = 0x22;    // gyro X..Z (6 bytes, LE)
constexpr uint8_t REG_OUTX_L_XL = 0x28;   // accel X..Z (6 bytes, LE)

// 104 Hz (ODR = 0100b in bits [7:4]); accel FS = ±2 g, gyro FS = ±245 dps (00b).
constexpr uint8_t CTRL1_XL_104HZ_2G = 0x40;
constexpr uint8_t CTRL2_G_104HZ_245DPS = 0x40;
constexpr uint8_t CTRL3_C_BDU_IF_INC = 0x44;  // BDU=1, IF_INC=1 (block update + auto-increment)

// Sensitivities for the scales above (datasheet "mechanical characteristics").
constexpr float ACCEL_G_PER_LSB = 0.061f / 1000.0f;  // 0.061 mg/LSB at ±2 g
constexpr float GYRO_DPS_PER_LSB = 8.75f / 1000.0f;  // 8.75 mdps/LSB at ±245 dps

// QMI8658 register map.
constexpr uint8_t QMI8658_REG_WHO_AM_I = 0x00;
constexpr uint8_t QMI8658_WHO_AM_I_VALUE = 0x05;
constexpr uint8_t QMI8658_REG_CTRL1 = 0x02;
constexpr uint8_t QMI8658_REG_CTRL2 = 0x03;
constexpr uint8_t QMI8658_REG_CTRL3 = 0x04;
constexpr uint8_t QMI8658_REG_CTRL7 = 0x08;
constexpr uint8_t QMI8658_REG_AX_L = 0x35;
constexpr uint8_t QMI8658_REG_GX_L = 0x3B;
constexpr uint8_t QMI8658_ADDR_6A = 0x6A;
constexpr uint8_t QMI8658_ADDR_6B = 0x6B;
constexpr uint8_t QMI8658_CTRL1_BIG_ENDIAN = 1U << 5;
constexpr uint8_t QMI8658_CTRL1_AUTO_INC = 1U << 6;
constexpr uint8_t QMI8658_CTRL1_SENSOR_DISABLE = 1U << 0;
constexpr uint8_t QMI8658_CTRL1_BASE = QMI8658_CTRL1_AUTO_INC | QMI8658_CTRL1_BIG_ENDIAN;
constexpr uint8_t QMI8658_CTRL2_FS_2G = 0U << 4;
constexpr uint8_t QMI8658_CTRL2_ODR_28HZ = 0x08;
constexpr uint8_t QMI8658_CTRL3_FS_512DPS = 0b101U << 4;
constexpr uint8_t QMI8658_CTRL3_ODR_28HZ = 0x08;
constexpr uint8_t QMI8658_CTRL7_ACC_GYRO_ENABLE = 0x03;
constexpr uint8_t QMI8658_CTRL7_DISABLE_ALL = 0x00;
// LSM6DS3: ODR bits [7:4] = 0000b powers the sensor down; full-scale bits are
// retained, so restoring the configured CTRL value resumes sampling.
constexpr uint8_t CTRL_ODR_POWER_DOWN = 0x00;
constexpr float QMI8658_ACCEL_G_PER_LSB = 1.0f / 16384.0f;  // ±2 g
constexpr float QMI8658_GYRO_DPS_PER_LSB = 1.0f / 64.0f;    // ±512 dps

// SC7A20H register map (Silan; LIS2DH-compatible layout with CTRL0 at 0x1F).
// Values quoted from read_pico_firmware components/sc7a20h/sc7a20h.c and
// include/sc7a20h.h.
constexpr uint8_t SC7A20H_REG_WHO_AM_I = 0x0F;
constexpr uint8_t SC7A20H_REG_CTRL0 = 0x1F;
constexpr uint8_t SC7A20H_REG_CTRL1 = 0x20;
constexpr uint8_t SC7A20H_REG_CTRL2 = 0x21;
constexpr uint8_t SC7A20H_REG_CTRL4 = 0x23;
constexpr uint8_t SC7A20H_REG_CTRL5 = 0x24;
constexpr uint8_t SC7A20H_REG_OUT_X_L = 0x28;
constexpr uint8_t SC7A20H_REG_SOFT_RESET = 0x68;
constexpr uint8_t SC7A20H_REG_VERSION = 0x70;
constexpr uint8_t SC7A20H_WHO_AM_I_VALUE = 0x11;  // SC7A20H_WHO_AM_I_VAL
// 0x68 = 0xA5 resets the whole analog/digital block (Silan 12.34, sc7a20h.c).
constexpr uint8_t SC7A20H_SOFT_RESET_CMD = 0xA5;
// sc7a20h.c bring_up(): after the soft reset, WHO_AM_I/VERSION, then CTRL5.BOOT
// (0x80) reboots the memory content, then the sensor config is applied.
constexpr uint8_t SC7A20H_CTRL5_BOOT = 0x80;
// sc7a20h.c read_regs(): a multi-byte read MUST set the sub-address MSB, which is
// the SC7A20H's auto-increment / address-latch bit for the output registers.
constexpr uint8_t SC7A20H_REG_AUTO_INCREMENT = 0x80;
// SC7A20H_CONFIG_IDLE (sc7a20h.h) expanded by sc7a20h_apply_config():
//   CTRL0 = OSR off, DLPF off, high-resolution off    -> 0x00
//   CTRL2 = high-pass filter off                      -> 0x00
//   CTRL4 = BDU set, full scale ±2 g                  -> 0x80
//   CTRL1 = ODR 12.5 Hz (0010b), LP mode (LPen), XYZ  -> 0x2F
constexpr uint8_t SC7A20H_CTRL0_IDLE = 0x00;
constexpr uint8_t SC7A20H_CTRL2_IDLE = 0x00;
constexpr uint8_t SC7A20H_CTRL4_BDU_2G = 0x80;
constexpr uint8_t SC7A20H_CTRL1_12_5HZ_LP_XYZ = 0x2F;
// CTRL1 ODR = 0000 stops the sampling engine (sc7a20h_power_down), ≈0.5 µA; every
// other config register is retained for the wake path.
constexpr uint8_t SC7A20H_CTRL1_ODR_POWER_DOWN = 0x00;
// The output registers are 16-bit left-justified. sc7a20h.c's fill_sample() drops
// the low 4 bits, and its 1 mg/LSB at ±2 g (mg_lsb = 1 << fs) is the scale for
// THAT normalized value — i.e. the LP mode 8-bit reading is left-justified, not
// an 8-bit value to be scaled by 16 mg.
constexpr uint8_t SC7A20H_OUT_SHIFT = 4;
constexpr float SC7A20H_MG_PER_LSB = 1.0f;  // ±2 g (SC7A20H_FS_2G)

bool g_wireReady[2] = {false, false};
TwoWire& sensorWire() {
  const auto& s = BoardConfig::ACTIVE.sensors;
#if SOC_I2C_NUM > 1
  return s.i2cBus == 1 ? Wire1 : Wire;
#else
  return Wire;
#endif
}

void ensureWire() {
  const auto& s = BoardConfig::ACTIVE.sensors;
  const uint8_t bus =
#if SOC_I2C_NUM > 1
      s.i2cBus == 1 ? 1 : 0;
#else
      0;
#endif
  if (g_wireReady[bus]) return;
  if (s.i2cSda < 0 || s.i2cScl < 0) return;  // no sensor bus on this board
  auto& wire = sensorWire();
  wire.begin(s.i2cSda, s.i2cScl, s.i2cHz);
  g_wireReady[bus] = true;
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  ensureWire();
  auto& wire = sensorWire();
  wire.beginTransmission(addr);
  wire.write(reg);
  wire.write(value);
  return wire.endTransmission() == 0;
}

bool readRegs(uint8_t addr, uint8_t reg, uint8_t* dst, uint8_t len) {
  ensureWire();
  auto& wire = sensorWire();
  wire.beginTransmission(addr);
  wire.write(reg);
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom(addr, len, static_cast<uint8_t>(true)) < len) return false;
  for (uint8_t i = 0; i < len; ++i) dst[i] = wire.read();
  return true;
}

bool qmi8658PresentAt(uint8_t addr) {
  uint8_t who = 0;
  return readRegs(addr, QMI8658_REG_WHO_AM_I, &who, 1) && who == QMI8658_WHO_AM_I_VALUE;
}

bool powerDownQmi8658(uint8_t addr) {
  // Do not short-circuit these writes: even if disabling the sensor engines
  // fails, still try to stop the internal oscillator. This is also used as the
  // cleanup path after a partially failed begin().
  const bool sensorsDisabled = writeReg(addr, QMI8658_REG_CTRL7, QMI8658_CTRL7_DISABLE_ALL);
  const bool oscillatorDisabled = writeReg(addr, QMI8658_REG_CTRL1, QMI8658_CTRL1_BASE | QMI8658_CTRL1_SENSOR_DISABLE);
  return sensorsDisabled && oscillatorDisabled;
}

}  // namespace

bool Imu::begin() {
  begun_ = false;
  addr_ = 0;

  const auto& s = BoardConfig::ACTIVE.sensors;
  const uint8_t configuredAddr = s.imuAddr;
  if (configuredAddr == 0) return false;
  if (s.i2cSda < 0 || s.i2cScl < 0 || s.i2cHz == 0) return false;
  ensureWire();
  uint8_t who = 0;
  switch (s.imuType) {
    case BoardConfig::ImuType::Lsm6ds3:
      if (!readRegs(configuredAddr, REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VALUE) return false;
      if (!writeReg(configuredAddr, REG_CTRL3_C, CTRL3_C_BDU_IF_INC)) return false;
      if (!writeReg(configuredAddr, REG_CTRL1_XL, CTRL1_XL_104HZ_2G)) return false;
      if (!writeReg(configuredAddr, REG_CTRL2_G, CTRL2_G_104HZ_245DPS)) return false;
      addr_ = configuredAddr;
      break;
    case BoardConfig::ImuType::Qmi8658: {
      // SA0 selects between 0x6A and 0x6B. X3 production revisions have used
      // both, so treat the profile address as a preference rather than a
      // guarantee. This restores the fallback used by the pre-SDK X3 driver.
      const uint8_t alternateAddr = configuredAddr == QMI8658_ADDR_6A ? QMI8658_ADDR_6B : QMI8658_ADDR_6A;
      if (qmi8658PresentAt(configuredAddr)) {
        addr_ = configuredAddr;
      } else if (qmi8658PresentAt(alternateAddr)) {
        addr_ = alternateAddr;
      } else {
        return false;
      }

      const bool configured = writeReg(addr_, QMI8658_REG_CTRL7, QMI8658_CTRL7_DISABLE_ALL) &&
                              writeReg(addr_, QMI8658_REG_CTRL1, QMI8658_CTRL1_BASE) &&
                              writeReg(addr_, QMI8658_REG_CTRL2, QMI8658_CTRL2_FS_2G | QMI8658_CTRL2_ODR_28HZ) &&
                              writeReg(addr_, QMI8658_REG_CTRL3, QMI8658_CTRL3_FS_512DPS | QMI8658_CTRL3_ODR_28HZ) &&
                              writeReg(addr_, QMI8658_REG_CTRL7, QMI8658_CTRL7_ACC_GYRO_ENABLE);
      if (!configured) {
        // A failed setup must not strand a previously running sensor in its
        // multi-milliamp active mode. The digital interface remains available
        // in QMI8658 power-down, so this cleanup is safe to attempt here.
        powerDownQmi8658(addr_);
        return false;
      }
      break;
    }
    case BoardConfig::ImuType::Sc7a20h: {
      // Bring-up order from sc7a20h.c bring_up(): soft reset, then identity, then
      // CTRL5.BOOT, then the sensor config. The reset is what makes the following
      // identity read meaningful after a warm boot (the board's BoardReadPico
      // probe may already have run it; it is idempotent).
      if (!writeReg(configuredAddr, SC7A20H_REG_SOFT_RESET, SC7A20H_SOFT_RESET_CMD)) return false;
      delay(20);
      if (!readRegs(configuredAddr, SC7A20H_REG_WHO_AM_I, &who, 1) || who != SC7A20H_WHO_AM_I_VALUE) return false;
      // VERSION (~0x28 on the bench part) is read to prove the register file
      // answers: sc7a20h.c only logs the value and deliberately does not gate
      // bring-up on it, so a silicon revision cannot be rejected here.
      uint8_t version = 0;
      if (!readRegs(configuredAddr, SC7A20H_REG_VERSION, &version, 1)) return false;
      if (!writeReg(configuredAddr, SC7A20H_REG_CTRL5, SC7A20H_CTRL5_BOOT)) return false;
      delay(10);
      if (!writeReg(configuredAddr, SC7A20H_REG_CTRL0, SC7A20H_CTRL0_IDLE)) return false;
      if (!writeReg(configuredAddr, SC7A20H_REG_CTRL2, SC7A20H_CTRL2_IDLE)) return false;
      if (!writeReg(configuredAddr, SC7A20H_REG_CTRL4, SC7A20H_CTRL4_BDU_2G)) return false;
      if (!writeReg(configuredAddr, SC7A20H_REG_CTRL1, SC7A20H_CTRL1_12_5HZ_LP_XYZ)) return false;
      delay(20);  // sc7a20h_apply_config() settle before the first sample
      addr_ = configuredAddr;
      break;
    }
    case BoardConfig::ImuType::None:
      return false;
  }
  begun_ = true;
  return true;
}

bool Imu::read(Sample& out) {
  const uint8_t addr = addr_;
  if (!begun_ || addr == 0) return false;
  const auto& s = BoardConfig::ACTIVE.sensors;
  uint8_t g[6] = {};
  uint8_t a[6] = {};
  float accelScale = ACCEL_G_PER_LSB;
  float gyroScale = GYRO_DPS_PER_LSB;
  // The SC7A20H is an accelerometer with no gyroscope at all (sc7a20h.h: "3 axes
  // ... no gyroscope"), so it reads the accel block only and reports gx = gy =
  // gz = 0. See the Imu.h note about what that means for rate-based consumers.
  uint8_t accelShift = 0;
  if (s.imuType == BoardConfig::ImuType::Lsm6ds3) {
    if (!readRegs(addr, REG_OUTX_L_G, g, sizeof(g))) return false;
    if (!readRegs(addr, REG_OUTX_L_XL, a, sizeof(a))) return false;
  } else if (s.imuType == BoardConfig::ImuType::Qmi8658) {
    if (!readRegs(addr, QMI8658_REG_GX_L, g, sizeof(g))) return false;
    if (!readRegs(addr, QMI8658_REG_AX_L, a, sizeof(a))) return false;
    accelScale = QMI8658_ACCEL_G_PER_LSB;
    gyroScale = QMI8658_GYRO_DPS_PER_LSB;
  } else if (s.imuType == BoardConfig::ImuType::Sc7a20h) {
    if (!readRegs(addr, static_cast<uint8_t>(SC7A20H_REG_OUT_X_L | SC7A20H_REG_AUTO_INCREMENT), a, sizeof(a))) {
      return false;
    }
    accelScale = SC7A20H_MG_PER_LSB / 1000.0f;
    gyroScale = 0.0f;
    accelShift = SC7A20H_OUT_SHIFT;
  } else {
    return false;
  }

  const int16_t gx = static_cast<int16_t>(g[0] | g[1] << 8);
  const int16_t gy = static_cast<int16_t>(g[2] | g[3] << 8);
  const int16_t gz = static_cast<int16_t>(g[4] | g[5] << 8);
  const int16_t ax = static_cast<int16_t>(static_cast<int16_t>(a[0] | a[1] << 8) >> accelShift);
  const int16_t ay = static_cast<int16_t>(static_cast<int16_t>(a[2] | a[3] << 8) >> accelShift);
  const int16_t az = static_cast<int16_t>(static_cast<int16_t>(a[4] | a[5] << 8) >> accelShift);

  out.ax = ax * accelScale;
  out.ay = ay * accelScale;
  out.az = az * accelScale;
  out.gx = gx * gyroScale;
  out.gy = gy * gyroScale;
  out.gz = gz * gyroScale;
  return true;
}

bool Imu::sleep() {
  const uint8_t addr = addr_;
  if (!begun_ || addr == 0) return false;
  switch (BoardConfig::ACTIVE.sensors.imuType) {
    case BoardConfig::ImuType::Lsm6ds3:
      return writeReg(addr, REG_CTRL1_XL, CTRL_ODR_POWER_DOWN) && writeReg(addr, REG_CTRL2_G, CTRL_ODR_POWER_DOWN);
    case BoardConfig::ImuType::Qmi8658:
      // CTRL7 only disables sampling; the internal oscillator keeps running.
      // SensorDisable is required for the QMI8658's full power-down mode.
      return powerDownQmi8658(addr);
    case BoardConfig::ImuType::Sc7a20h:
      // CTRL1 ODR = 0000 shuts the sampling engine down (≈0.5 µA) and retains
      // every other config register, which is what wake() re-applies
      // (sc7a20h_power_down in sc7a20h.c).
      return writeReg(addr, SC7A20H_REG_CTRL1, SC7A20H_CTRL1_ODR_POWER_DOWN);
    case BoardConfig::ImuType::None:
      return false;
  }
  return false;
}

bool Imu::wake() {
  const uint8_t addr = addr_;
  if (!begun_ || addr == 0) return false;
  switch (BoardConfig::ACTIVE.sensors.imuType) {
    case BoardConfig::ImuType::Lsm6ds3:
      return writeReg(addr, REG_CTRL1_XL, CTRL1_XL_104HZ_2G) && writeReg(addr, REG_CTRL2_G, CTRL2_G_104HZ_245DPS);
    case BoardConfig::ImuType::Qmi8658:
      // Re-enable the internal oscillator before restarting the sensors.
      return writeReg(addr, QMI8658_REG_CTRL1, QMI8658_CTRL1_BASE) &&
             writeReg(addr, QMI8658_REG_CTRL7, QMI8658_CTRL7_ACC_GYRO_ENABLE);
    case BoardConfig::ImuType::Sc7a20h:
      // Same config sequence as begin() (sc7a20h_power_up -> apply_config), minus
      // the soft reset: only CTRL1's ODR field was cleared, so the retained
      // CTRL0/CTRL2/CTRL4 values are rewritten for a defined state. The 20 ms
      // settle is sc7a20h_apply_config()'s.
      if (!writeReg(addr, SC7A20H_REG_CTRL0, SC7A20H_CTRL0_IDLE)) return false;
      if (!writeReg(addr, SC7A20H_REG_CTRL2, SC7A20H_CTRL2_IDLE)) return false;
      if (!writeReg(addr, SC7A20H_REG_CTRL4, SC7A20H_CTRL4_BDU_2G)) return false;
      if (!writeReg(addr, SC7A20H_REG_CTRL1, SC7A20H_CTRL1_12_5HZ_LP_XYZ)) return false;
      delay(20);
      return true;
    case BoardConfig::ImuType::None:
      return false;
  }
  return false;
}

}  // namespace freeink

#else  // FREEINK_CAP_IMU — IMU absent.

namespace freeink {
bool Imu::begin() { return false; }
bool Imu::read(Sample&) { return false; }
bool Imu::sleep() { return false; }
bool Imu::wake() { return false; }
}  // namespace freeink

#endif  // FREEINK_CAP_IMU
