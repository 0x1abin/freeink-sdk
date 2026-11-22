#include <BoardConfig.h>
#include <I2cBusLock.h>
#include <Rtc.h>
#include <Wire.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::array<uint8_t, 16> regs{};
std::array<uint8_t, 16> original{};
struct Write {
  uint8_t reg;
  std::vector<uint8_t> bytes;
};
std::vector<Write> writes;
std::vector<unsigned> owners;
unsigned operations = 0, failAt = 0;
uint8_t lastRead = 0;
size_t lastReadSize = 0;

bool fail() {
  assert(hostLockDepth > 0);
  owners.push_back(hostRootLockSerial);
  return ++operations == failAt;
}

bool same(const Rtc::DateTime& a, const Rtc::DateTime& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour && a.minute == b.minute &&
         a.second == b.second && a.weekday == b.weekday;
}

void checkBus() {
  assert(hostLockDepth == 0 && hostLockTakes == hostLockGives);
  assert(Wire.sda == 0 && Wire.scl == 1 && Wire.hz == 400000);
  assert(Wire.beginCalls == 1 && Wire.endCalls == 0 && Wire.uninitializedTransactions == 0);
  assert(Wire1.beginCalls == 0 && Wire1.transmissions == 0 && Wire1.requests == 0);
}

void checkUnrelated() {
  assert(regs[1] == original[1]);
  for (size_t i = 9; i < regs.size(); ++i) assert(regs[i] == original[i]);
}
}  // namespace

bool hostI2cWrite(uint8_t addr, const std::vector<uint8_t>& bytes) {
  assert(addr == 0x51 && !bytes.empty());
  const uint8_t reg = bytes[0];
  writes.push_back({reg, {bytes.begin() + 1, bytes.end()}});
  if (fail()) {
    // A calendar transfer can partly reach the chip before the driver reports
    // failure.
    if (reg == 2) std::memcpy(regs.data() + 2, bytes.data() + 1, 3);
    return false;
  }
  assert(reg + bytes.size() - 1 <= regs.size());
  std::memcpy(regs.data() + reg, bytes.data() + 1, bytes.size() - 1);
  return true;
}

bool hostI2cRead(uint8_t addr, uint8_t reg, uint8_t* out, size_t length) {
  assert(addr == 0x51 && reg + length <= regs.size());
  lastRead = reg;
  lastReadSize = length;
  if (fail()) return false;
  std::memcpy(out, regs.data() + reg, length);
  return true;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  BoardConfig::ACTIVE.sensors = {0, 1, 400000, 0x51, 0, BoardConfig::RtcType::Pcf8563};
  freeink::enableI2cBusLock();
  for (size_t i = 0; i < regs.size(); ++i) regs[i] = 0xa0 + i;
  regs[0] = 0;
  const uint8_t calendar[] = {0x56, 0x34, 0x12, 0x10, 0x02, 0x11, 0x26};
  std::memcpy(regs.data() + 2, calendar, sizeof(calendar));
  Rtc rtc;
  assert(rtc.begin() && rtc.present());
  // begin() retains the existing CLKOUT policy; now()/set() must not alter it.
  original = regs;
  writes.clear();
  owners.clear();
  operations = 0;
  Rtc::DateTime out{2025, 6, 7, 8, 9, 10, 6};
  const auto previous = out;
  if (scenario.rfind("set", 0) == 0) {
    Rtc::DateTime target{2028, 2, 29, 23, 59, 58, 2};
    if (scenario == "set_stopped") regs[0] = 0x20;
    if (scenario == "set_reserved") regs[0] = 0x40;
    if (scenario == "set_test1" || scenario == "set_test_both") regs[0] = scenario == "set_test1" ? 0x80 : 0x88;
    if (scenario == "set_por_override" || scenario.rfind("set_por_failure", 0) == 0) {
      regs[0] = 0x08;
      regs[2] |= 0x80;
    }
    if (scenario == "set_century") target = {1999, 12, 31, 23, 59, 58, 5};
    if (scenario == "set_invalid_leap") target.year = 2027;
    if (scenario == "set_invalid_1900_leap") target.year = 1900;
    if (scenario == "set_month") target.month = 13;
    if (scenario == "set_zero_month") target.month = 0;
    if (scenario == "set_zero_day") target.day = 0;
    if (scenario == "set_hour") target.hour = 24;
    if (scenario == "set_minute") target.minute = 60;
    if (scenario == "set_second") target.second = 60;
    if (scenario == "set_weekday") target.weekday = 7;
    if (scenario == "set_year_low") target.year = 1899;
    if (scenario == "set_year_high") target.year = 2100;
    const bool invalid = scenario == "set_invalid_leap" || scenario == "set_invalid_1900_leap" ||
                         scenario == "set_month" || scenario == "set_zero_month" || scenario == "set_zero_day" ||
                         scenario == "set_hour" || scenario == "set_minute" || scenario == "set_second" ||
                         scenario == "set_weekday" || scenario == "set_year_low" || scenario == "set_year_high";
    if (scenario.rfind("set_failure", 0) == 0 || scenario.rfind("set_por_failure", 0) == 0)
      failAt = scenario.back() - '0';
    if (scenario == "set_pointer_failure") Wire.pointerNacks = 1;
    const bool ok = rtc.set(target);
    const bool testClock = scenario == "set_test1" || scenario == "set_test_both";
    if (invalid || testClock || failAt || scenario == "set_pointer_failure") {
      assert(!ok);
      if (invalid) assert(operations == 0 && writes.empty());
      if (testClock || scenario == "set_pointer_failure" || failAt == 1) assert(writes.empty());
      if (failAt == 2) assert(writes.size() == 1 && writes[0].reg == 0);
      if (failAt >= 3) {
        assert(regs[0] == 0x20);
        assert(!rtc.now(out) && same(out, previous));
      }
    } else {
      assert(ok && regs[0] == 0 && writes.size() == 3);
      assert(writes[0].reg == 0 && writes[0].bytes == std::vector<uint8_t>{0x20});
      const std::vector<uint8_t> expected = scenario == "set_century"
                                                ? std::vector<uint8_t>{0x58, 0x59, 0x23, 0x31, 0x05, 0x92, 0x99}
                                                : std::vector<uint8_t>{0x58, 0x59, 0x23, 0x29, 0x02, 0x02, 0x28};
      assert(writes[1].reg == 2 && writes[1].bytes == expected);
      assert(writes[2].reg == 0 && writes[2].bytes == std::vector<uint8_t>{0});
      // The complete control/read/STOP/calendar/RUN sequence shares one outer
      // guard.
      assert(owners.size() == 4);
      for (const auto owner : owners) assert(owner == owners[0]);
      assert(rtc.now(out) && same(out, target));
    }
  } else {
    const bool valid = scenario == "normal" || scenario == "snapshot" || scenario == "century" || scenario == "leap";
    if (scenario == "stopped") regs[0] = 0x20;
    if (scenario == "test1") regs[0] = 0x80;
    if (scenario == "testc") regs[0] = 0x08;
    if (scenario == "voltage_low") regs[2] |= 0x80;
    if (scenario == "bcd") regs[3] = 0x1a;  // Naive decode produces plausible minute 20.
    if (scenario == "year_bcd") regs[8] = 0x1a;
    if (scenario == "zero_month") regs[7] = 0;
    if (scenario == "zero_day") regs[5] = 0;
    if (scenario == "day_31_april") {
      regs[5] = 0x31;
      regs[7] = 0x04;
    }
    if (scenario == "bad_leap" || scenario == "leap" || scenario == "bad_1900_leap") {
      regs[5] = 0x29;
      regs[7] = scenario == "bad_1900_leap" ? 0x82 : 0x02;
      regs[8] = scenario == "leap" ? 0x28 : scenario == "bad_leap" ? 0x27 : 0;
    }
    if (scenario == "century") regs[7] |= 0x80;
    if (scenario == "weekday") regs[6] = 7;
    if (scenario == "hour") regs[4] = 0x24;
    if (scenario == "minute") regs[3] = 0x60;
    if (scenario == "second") regs[2] = 0x60;
    if (scenario == "read_failure") failAt = 1;
    if (scenario == "pointer_failure") Wire.pointerNacks = 1;
    if (scenario == "short_read") Wire.shortReads = 1;
    assert(rtc.now(out) == valid);
    if (!valid) assert(same(out, previous));
    if (scenario == "snapshot") assert(lastRead == 0 && lastReadSize == 9 && operations == 1);
    if (scenario == "normal" || scenario == "snapshot") assert(same(out, {2026, 11, 10, 12, 34, 56, 2}));
    if (scenario == "century") assert(out.year == 1926 && out.month == 11);
    if (scenario == "leap") assert(out.year == 2028 && out.month == 2 && out.day == 29);
    assert(writes.empty());
  }
  checkUnrelated();
  checkBus();
  std::printf("PCF8563 %s: passed\n", scenario.c_str());
}
