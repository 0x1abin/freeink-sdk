#include <BoardConfig.h>
#include <Imu.h>
#include <Wire.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "native.h"

namespace {
std::array<uint8_t, 256> regs{};
bool held = false, busReady = true, lockOk = true;
int operations = 0, failAt = 0, locks = 0, unlocks = 0, writes = 0;
bool fail() { return !busReady || ++operations == failAt; }
void checkBus() {
  assert(!held && locks == unlocks);
  assert(Wire.beginCalls == 0 && Wire.transmissions == 0);
  assert(Wire1.beginCalls == 0 && Wire1.transmissions == 0);
}
void sampleBytes() {
  // Raw left-aligned values: +1g, -0.5g, +0.25g, with discarded low bits set.
  const uint8_t raw[] = {0x0f, 0x40, 0x07, 0xe0, 0x03, 0x10};
  std::memcpy(regs.data() + 0x28, raw, sizeof(raw));
  regs[0x27] = 0x08;
}
void unchanged(const Imu::Sample& value) {
  assert(value.ax == 7 && value.ay == 8 && value.az == 9 && value.gx == 10 && value.gy == 11 && value.gz == 12);
}
}  // namespace

extern "C" bool metalio_i2c1_lock(int ms) {
  assert(ms == 50 && !held);
  if (!lockOk) return false;
  held = true;
  ++locks;
  return true;
}
extern "C" void metalio_i2c1_unlock() {
  assert(held);
  held = false;
  ++unlocks;
}
extern "C" int metalio_i2c1_transmit_receive_locked(int bus, uint8_t addr, const uint8_t* tx, size_t nt, uint8_t* rx,
                                                    size_t nr, int ms) {
  assert(held && bus == 1 && addr == 0x19 && nt == 1 && ms == 50);
  assert(nr == 1 || (tx[0] == 0xa8 && nr == 6));
  if (fail()) {
    if (nr == 6) std::memcpy(rx, regs.data() + 0x28, 3);
    return -1;
  }
  std::memcpy(rx, regs.data() + (tx[0] & 0x7f), nr);
  if (nr == 6) regs[0x27] = 0;  // Real data-ready is consumed by the burst.
  return 0;
}
extern "C" int metalio_i2c1_write_reg_locked(int bus, uint8_t addr, uint8_t reg, const uint8_t* data, size_t len,
                                             int ms) {
  assert(held && bus == 1 && addr == 0x19 && len == 1 && ms == 50);
  assert(reg == 0x20 || reg == 0x1f || reg == 0x23);  // IRQ/reset/FIFO clients must be preserved.
  ++writes;
  regs[reg] = data[0];  // Even a failed transfer may have changed the byte.
  return fail() ? -1 : 0;
}
extern "C" bool epd_board_metalio_eink4_plus_ioexp_ok() { return false; }
extern "C" void epd_board_metalio_eink4_plus_tp_reset() { assert(false); }
extern "C" bool epd_board_metalio_eink4_plus_ioexp_read_port(int, uint8_t*) { return false; }

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  regs[0x0f] = 0x11;
  regs[0x70] = 0x28;
  regs[0x20] = 0x57;
  if (scenario == "id") regs[0x0f] = 0x33;
  if (scenario == "version") regs[0x70] = 0x27;
  if (scenario == "irq1") regs[0x22] = 0x10;
  if (scenario == "irq2") regs[0x25] = 0x02;
  if (scenario == "filter") regs[0x21] = 0x08;
  if (scenario == "fifo") regs[0x2e] = 0x80;
  if (scenario == "fifo_ctrl") regs[0x24] = 0x40;
  if (scenario == "bus") busReady = false;
  if (scenario == "lock") lockOk = false;
  if (scenario.rfind("begin_fail_", 0) == 0) failAt = std::stoi(scenario.substr(11));
  Imu imu;
  const bool found = imu.begin();
  checkBus();  // RED: the old production begin uses a competing Wire owner.
  const bool rejected = scenario == "id" || scenario == "version" || scenario == "irq1" || scenario == "irq2" ||
                        scenario == "filter" || scenario == "fifo" || scenario == "fifo_ctrl";
  if (rejected || !busReady || !lockOk || failAt) {
    assert(!found && !imu.present());
    if (rejected) assert(writes == 0);
    if (rejected) return 0;
    busReady = lockOk = true;
    failAt = 0;
    assert(imu.begin());
  } else {
    assert(found && imu.present() && !imu.hasGyroscope());
  }
  assert(regs[0x20] == 0x07);
  Imu::Sample out{7, 8, 9, 10, 11, 12};
  assert(!imu.read(out));
  unchanged(out);
  operations = 0;
  if (scenario == "wake_route") regs[0x22] = 0x10;
  if (scenario.rfind("wake_fail_", 0) == 0) failAt = std::stoi(scenario.substr(10));
  const bool woke = imu.wake();
  if (failAt || scenario == "wake_route") {
    assert(!woke && regs[0x20] == 0x07 && !imu.read(out));
    unchanged(out);
    checkBus();
    return 0;
  }
  assert(woke && regs[0x20] == 0x57 && regs[0x1f] == 1 && regs[0x23] == 0x80);
#if defined(PLUS_IMU_REAL_PROFILE)
  if (scenario == "v14_pose_frame") {
    struct Pose { int16_t x, y; float correctedX, correctedY; };
    // Two observed posture classifications (2026-11-17), plus their opposites.
    constexpr Pose poses[] = {{16384, 0, 0, -1}, {0, 16384, -1, 0},
                              {-16384, 0, 0, 1}, {0, -16384, 1, 0}};
    for (const auto& pose : poses) {
      const int16_t raw[] = {pose.x, pose.y, 4096};
      for (size_t axis = 0; axis < 3; ++axis) {
        const uint16_t bits = static_cast<uint16_t>(raw[axis]);
        regs[0x28 + axis * 2] = bits & 0xff;
        regs[0x29 + axis * 2] = bits >> 8;
      }
      regs[0x27] = 0x08;
      assert(imu.read(out));
      assert(out.ax == pose.correctedX && out.ay == pose.correctedY && out.az == 0.25f);
      assert(out.gx == 0 && out.gy == 0 && out.gz == 0);
    }
    checkBus();
    std::puts("Plus V1.4 actual profile: portrait/left/inverted/right frame passed");
    return 0;
  }
#endif
  sampleBytes();
  operations = 0;
  if (scenario == "no_data") regs[0x27] = 0;
  if (scenario == "read_status") failAt = 1;
  if (scenario == "read_partial") failAt = 2;
  if (scenario == "mount") {
    BoardConfig::ACTIVE.sensors.imuSwapXY = true;
    BoardConfig::ACTIVE.sensors.imuFlipX = true;
  }
  const bool got = imu.read(out);
  if (scenario == "no_data" || failAt) {
    assert(!got);
    unchanged(out);
  } else {
    assert(got && out.ax == (scenario == "mount" ? 0.5f : 1.0f) && out.ay == (scenario == "mount" ? 1.0f : -0.5f) &&
           out.az == 0.25f);
    assert(out.gx == 0 && out.gy == 0 && out.gz == 0);
    assert(!imu.read(out));
  }
  failAt = 0;
  operations = 0;
  if (scenario == "sleep_fail") failAt = 1;
  assert(imu.sleep() == (failAt == 0));
  assert(regs[0x20] == 0x07);
  checkBus();
  std::printf("Plus IMU %s passed\n", scenario.c_str());
}
