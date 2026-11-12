#include <BoardConfig.h>
#include <Rtc.h>
#include <Wire.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "native.h"

namespace {
std::array<uint8_t, 16> regs;
std::array<uint8_t, 16> original;
bool held = false, lockOk = true, busReady = true;
int locks = 0, unlocks = 0, operations = 0, writes = 0, failAt = 0;
bool fail() { return !busReady || ++operations == failAt; }
void checkBus() {
  assert(!held && locks == unlocks);
  assert(Wire.beginCalls == 0 && Wire.transmissions == 0 && Wire.requests == 0);
  assert(Wire1.beginCalls == 0 && Wire1.transmissions == 0 && Wire1.requests == 0);
}
void checkUnrelated() {
  assert(regs[1] == original[1]);
  for (size_t i = 9; i < regs.size(); ++i) assert(regs[i] == original[i]);
}
bool same(const Rtc::DateTime& a, const Rtc::DateTime& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour && a.minute == b.minute &&
         a.second == b.second && a.weekday == b.weekday;
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
  assert(held && bus == 1 && addr == 0x51 && nt == 1 && ms == 50);
  assert(tx[0] == 0 && (nr == 1 || nr == 9));
  if (fail()) return -1;
  std::memcpy(rx, regs.data(), nr);
  return 0;
}
extern "C" int metalio_i2c1_write_reg_locked(int bus, uint8_t addr, uint8_t reg, const uint8_t* data, size_t len,
                                             int ms) {
  assert(held && bus == 1 && addr == 0x51 && ms == 50);
  assert((reg == 0 && len == 1 && (data[0] == 0 || data[0] == 0x20)) || (reg == 2 && len == 7));
  ++writes;
  if (fail()) {
    // Model a partially applied calendar write, not only a harmless NACK.
    if (reg == 2) std::memcpy(regs.data() + reg, data, 3);
    return -1;
  }
  std::memcpy(regs.data() + reg, data, len);
  return 0;
}
extern "C" bool epd_board_metalio_eink4_plus_ioexp_ok() { return false; }
extern "C" void epd_board_metalio_eink4_plus_tp_reset() { assert(false); }
extern "C" bool epd_board_metalio_eink4_plus_ioexp_read_port(int, uint8_t*) { return false; }
bool hostI2cWrite(uint8_t, const std::vector<uint8_t>&) { return false; }
bool hostI2cRead(uint8_t, uint8_t, uint8_t*, size_t) { return false; }

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  for (size_t i = 0; i < regs.size(); ++i) regs[i] = 0xa0 + i;
  regs[0] = 0;
  const uint8_t time[] = {0x56, 0x34, 0x12, 0x10, 0x02, 0x11, 0x26};
  std::memcpy(regs.data() + 2, time, sizeof(time));
  original = regs;
  if (scenario == "not_ready") busReady = false;
  if (scenario == "lock") lockOk = false;
  Rtc rtc;
  const bool found = rtc.begin();
  checkBus();  // RED on a competing Wire transport, before profile enablement.
  if (!busReady || !lockOk) {
    assert(!found && !rtc.present() && writes == 0);
    busReady = lockOk = true;
    assert(rtc.begin() && rtc.present());
  } else {
    assert(found && rtc.present());
  }
  assert(writes == 0 && regs == original);
  const Rtc::DateTime expected{2026, 11, 10, 12, 34, 56, 2};
  Rtc::DateTime out;
  assert(rtc.now(out) && same(out, expected));
  if (scenario.rfind("set", 0) == 0) {
    Rtc::DateTime target{2028, 2, 29, 23, 59, 58, 2};
    if (scenario == "set_stopped") regs[0] = 0x20;
    if (scenario == "set_reserved") regs[0] = 0x40;  // Reserved bits must be written as zero.
    if (scenario == "set_test") regs[0] = 0x80;
    if (scenario == "set_test_both") regs[0] = 0x88;
    if (scenario == "set_por_override" || scenario.rfind("set_por_failure", 0) == 0) {
      regs[0] = 0x08;
      regs[2] |= 0x80;  // Actual unit: TESTC + VL; never restore this calendar.
      const auto previous = out;
      assert(!rtc.now(out) && same(out, previous));
    }
    if (scenario == "set_invalid_leap") target.year = 2027;
    if (scenario == "set_month") target.month = 13;
    if (scenario == "set_hour") target.hour = 24;
    if (scenario == "set_weekday") target.weekday = 7;
    if (scenario == "set_year_low") target.year = 1999;
    if (scenario == "set_year_high") target.year = 2100;
    if (scenario == "set_lock") lockOk = false;
    const bool invalid = scenario == "set_invalid_leap" || scenario == "set_month" || scenario == "set_hour" ||
                         scenario == "set_weekday" || scenario == "set_year_low" || scenario == "set_year_high";
    operations = 0;
    if (scenario.rfind("set_failure", 0) == 0 || scenario.rfind("set_por_failure", 0) == 0)
      failAt = scenario.back() - '0';
    const bool ok = rtc.set(target);
    if (invalid || scenario == "set_test" || scenario == "set_test_both" || !lockOk || failAt) {
      assert(!ok);
      if (invalid) assert(operations == 0 && writes == 0);
      if (scenario == "set_test" || scenario == "set_test_both" || !lockOk) assert(writes == 0);
      if (failAt >= 3) assert(regs[0] == 0x20);
      failAt = 0;
      lockOk = true;
      if (regs[0] & 0xa8) assert(!rtc.now(out));
    } else {
      assert(ok && regs[0] == 0 && writes == 3);
      assert(rtc.now(out) && same(out, target));
    }
  } else if (scenario == "no_timer") {
    bool fired = true;
    assert(!rtc.armWakeTimer(60) && !rtc.cancelWakeTimer() && !rtc.readWakeTimer(fired) && !fired);
  } else {
    const bool invalid =
        scenario != "normal" && scenario != "read_only_begin" && scenario != "not_ready" && scenario != "lock";
    if (scenario == "read_failure") failAt = operations + 1;
    if (scenario == "stopped") regs[0] = 0x20;
    if (scenario == "test1") regs[0] = 0x80;
    if (scenario == "testc") regs[0] = 0x08;
    if (scenario == "voltage_low") regs[2] |= 0x80;
    if (scenario == "bcd") regs[3] = 0x1a;
    if (scenario == "zero_month") regs[7] = 0;
    if (scenario == "zero_day") regs[5] = 0;
    if (scenario == "bad_leap") {
      regs[5] = 0x29;
      regs[7] = 0x02;
      regs[8] = 0x27;
    }
    if (scenario == "century") regs[7] |= 0x80;
    if (scenario == "weekday") regs[6] = 7;
    const auto previous = out;
    assert(rtc.now(out) == !invalid);
    if (invalid) assert(same(previous, out));
    assert(writes == 0);
  }
  checkUnrelated();
  checkBus();
  std::printf("Plus RTC %s: passed\n", scenario.c_str());
}
