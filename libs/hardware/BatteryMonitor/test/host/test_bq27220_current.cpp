// X3 current reads own their bus initialization, including after the boot probe
// releases Wire and before RTC/IMU start using the same pins.
#include <cstdio>
#include <vector>

#include "../../src/BatteryMonitor.cpp"

namespace {
int checksRun = 0;
int checksFailed = 0;
#define CHECK(condition)                                               \
  do {                                                                 \
    ++checksRun;                                                       \
    if (!(condition)) {                                                \
      ++checksFailed;                                                  \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition); \
    }                                                                  \
  } while (0)

int16_t current = 125;
unsigned currentReads = 0;

void reset(const BoardConfig::BoardProfile& board = BoardConfig::XTEINK_X3) {
  BoardConfig::ACTIVE = board;
  Wire = TwoWire{};
  g_wireReady = false;
  hostMillis = 0;
  current = 125;
  currentReads = 0;
}

void beginSharedBus() {
  const auto& sensors = BoardConfig::ACTIVE.sensors;
  CHECK(Wire.begin(sensors.i2cSda, sensors.i2cScl, sensors.i2cHz));
}

void testBootAndSharedBusOrder() {
  CHECK(BoardConfig::XTEINK_X4.usbDetect == 20);
  for (const auto& board : {BoardConfig::XTEINK_X3, BoardConfig::XTEINK_X3_UC8279}) {
    CHECK(board.usbDetect == BoardConfig::PIN_UNASSIGNED);
    for (const bool probe : {false, true}) {
      for (const bool sensorsFirst : {false, true}) {
        reset(board);
        if (probe) {
          beginSharedBus();
          Wire.end();
          CHECK(!Wire.started);
        }
        if (sensorsFirst) {
          beginSharedBus();  // RTC
          beginSharedBus();  // IMU
        }
        const unsigned before = Wire.beginCalls;
        BatteryMonitor monitor;
        CHECK(monitor.isCharging());
        CHECK(Wire.beginCalls == before + 1);
        CHECK(g_wireReady && Wire.started);
        CHECK(Wire.sda == board.batteryGauge.i2cSda);
        CHECK(Wire.scl == board.batteryGauge.i2cScl);
        CHECK(Wire.hz == board.batteryGauge.i2cHz);
        CHECK(Wire.uninitializedTransactions == 0);
        if (!sensorsFirst) {
          beginSharedBus();  // RTC/IMU may initialize after the boot USB query.
          beginSharedBus();
        }
        const unsigned readyCalls = Wire.beginCalls;
        CHECK(monitor.isCharging());
        CHECK(Wire.beginCalls == readyCalls);
        bool known = true;
        CHECK(!monitor.isExternalPowerPresent(&known) && !known);
      }
    }
  }
}

void testRegisterHelpersRetryFailedInitialization() {
  enum class RegisterHelper { Read16, Read8, ReadMany, Acknowledge, Write8, Write16 };
  for (const auto helper : {RegisterHelper::Read16, RegisterHelper::Read8, RegisterHelper::ReadMany,
                            RegisterHelper::Acknowledge, RegisterHelper::Write8, RegisterHelper::Write16}) {
    reset();
    Wire.beginResult = false;
    uint16_t word = 0xCAFE;
    uint8_t byte = 0xA5;
    uint8_t bytes[2] = {0xA5, 0x5A};
    const auto operation = [&] {
      switch (helper) {
        case RegisterHelper::Read16:
          return readReg16(0x55, 0x0C, word);
        case RegisterHelper::Read8:
          return readReg8(0x55, 0x0C, byte);
        case RegisterHelper::ReadMany:
          return readRegs(0x55, 0x0C, bytes, sizeof(bytes));
        case RegisterHelper::Acknowledge:
          return i2cAcks(0x55);
        case RegisterHelper::Write8:
          return writeReg8(0x55, 0x00, 0x01);
        case RegisterHelper::Write16:
          return writeReg16(0x55, 0x00, 0x0001);
      }
      return false;
    };
    CHECK(!operation());
    CHECK(!g_wireReady);
    CHECK(Wire.beginCalls == 1);
    CHECK(Wire.transmissions == 0 && Wire.requests == 0);
    CHECK(Wire.uninitializedTransactions == 0);
    CHECK(word == 0xCAFE && byte == 0xA5 && bytes[0] == 0xA5 && bytes[1] == 0x5A);
    Wire.beginResult = true;
    CHECK(operation());
    CHECK(g_wireReady && Wire.beginCalls == 2);
    CHECK(operation());
    CHECK(Wire.beginCalls == 2);
  }

  reset();
  Wire.beginResult = false;
  bool known = true;
  CHECK(!readGaugeCharging(known) && !known);
  CHECK(Wire.beginCalls == 2 && Wire.transmissions == 0);
  Wire.beginResult = true;
  CHECK(readGaugeCharging(known) && known);
  CHECK(Wire.beginCalls == 3);
}

void testSignedCurrentAndBoundedRetry() {
  for (const int16_t value : {int16_t{125}, int16_t{-125}, int16_t{0}, int16_t{32767}, int16_t{-32768}}) {
    reset();
    current = value;
    bool known = false;
    CHECK(readGaugeCharging(known) == (value > 0));
    CHECK(known && currentReads == 1);
    CHECK(Wire.transmissions == 1 && Wire.requests == 1);
    CHECK(hostMillis == 0);
  }

  for (const unsigned nacks : {1u, 2u, 3u}) {
    reset();
    Wire.pointerNacks = nacks;
    bool known = true;
    CHECK(readGaugeCharging(known) == (nacks == 1));
    CHECK(known == (nacks == 1));
    CHECK(Wire.transmissions == 2);
    CHECK(Wire.requests == (nacks == 1 ? 1u : 0u));
    CHECK(Wire.beginCalls == 1 && hostMillis == 2);
  }
}

void testShortReadsAreDrainedAndLeaveOutputUntouched() {
  for (const uint8_t length : {uint8_t{0}, uint8_t{1}}) {
    reset();
    Wire.shortReads = 1;
    Wire.shortReadLength = length;
    uint16_t word = 0xCAFE;
    CHECK(!readReg16(0x55, 0x0C, word));
    CHECK(word == 0xCAFE);
    CHECK(Wire.available() == 0 && Wire.reads == length);
    CHECK(readReg16(0x55, 0x0C, word));
    CHECK(word == static_cast<uint16_t>(current));
    CHECK(Wire.beginCalls == 1);
    reset();
    Wire.shortReads = 1;
    Wire.shortReadLength = length;
    uint8_t bytes[2] = {0xA5, 0x5A};
    CHECK(!readRegs(0x55, 0x0C, bytes, sizeof(bytes)));
    CHECK(bytes[0] == 0xA5 && bytes[1] == 0x5A);
    CHECK(Wire.available() == 0 && Wire.reads == length);
    CHECK(readRegs(0x55, 0x0C, bytes, sizeof(bytes)));
    CHECK(bytes[0] == static_cast<uint8_t>(current) && bytes[1] == 0);
  }
  for (const unsigned shortReads : {1u, 2u}) {
    reset();
    Wire.shortReads = shortReads;
    bool known = true;
    CHECK(readGaugeCharging(known) == (shortReads == 1));
    CHECK(known == (shortReads == 1));
    CHECK(Wire.transmissions == 2 && Wire.requests == 2);
    CHECK(Wire.available() == 0);
    CHECK(Wire.reads == (shortReads == 1 ? 3u : 2u));
    CHECK(hostMillis == 2);
  }
}
}  // namespace

bool hostI2cWrite(const uint8_t addr, const std::vector<uint8_t>& bytes) {
  return addr == 0x55 && (bytes.empty() || bytes.size() == 2 || bytes.size() == 3);
}

bool hostI2cRead(const uint8_t addr, const uint8_t reg, uint8_t* out, const size_t length) {
  if (addr != 0x55 || reg != 0x0C || length == 0 || length > 2) return false;
  ++currentReads;
  const uint16_t raw = static_cast<uint16_t>(current);
  out[0] = static_cast<uint8_t>(raw);
  if (length == 2) out[1] = static_cast<uint8_t>(raw >> 8);
  return true;
}

int main() {
  testBootAndSharedBusOrder();
  testRegisterHelpersRetryFailedInitialization();
  testSignedCurrentAndBoundedRetry();
  testShortReadsAreDrainedAndLeaveOutputUntouched();
  std::printf("%d checks, %d failures\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
