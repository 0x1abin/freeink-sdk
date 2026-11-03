#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// The test supplies the device behind these two calls.
bool hostI2cWrite(uint8_t addr, const std::vector<uint8_t>& bytes);
bool hostI2cRead(uint8_t addr, uint8_t reg, uint8_t* out, size_t length);

class TwoWire {
  uint8_t addr_ = 0;
  std::vector<uint8_t> tx_;
  uint8_t rx_[32] = {};
  size_t rxLength_ = 0;
  size_t rxPos_ = 0;

 public:
  bool beginResult = true;
  bool started = false;
  unsigned beginCalls = 0;
  unsigned endCalls = 0;
  unsigned transmissions = 0;
  unsigned requests = 0;
  unsigned reads = 0;
  unsigned uninitializedTransactions = 0;
  unsigned pointerNacks = 0;
  unsigned shortReads = 0;
  uint8_t shortReadLength = 1;
  int sda = -1;
  int scl = -1;
  uint32_t hz = 0;

  bool begin(int nextSda, int nextScl, uint32_t nextHz) {
    ++beginCalls;
    sda = nextSda;
    scl = nextScl;
    hz = nextHz;
    if (beginResult) started = true;
    return beginResult;
  }
  void end() {
    ++endCalls;
    started = false;
    rxLength_ = rxPos_ = 0;
  }
  void setTimeOut(int) {}
  void beginTransmission(uint8_t addr) {
    ++transmissions;
    if (!started) ++uninitializedTransactions;
    addr_ = addr;
    tx_.clear();
  }
  size_t write(uint8_t byte) {
    tx_.push_back(byte);
    return 1;
  }
  uint8_t endTransmission(bool stop = true) {
    if (!started) return 2;
    if (!stop && tx_.size() == 1) {
      if (pointerNacks == 0) return 0;
      --pointerNacks;
      return 2;
    }
    return hostI2cWrite(addr_, tx_) ? 0 : 2;
  }
  uint8_t requestFrom(uint8_t addr, uint8_t length, uint8_t = 1) {
    ++requests;
    rxPos_ = 0;
    rxLength_ = started && !tx_.empty() && hostI2cRead(addr, tx_[0], rx_, length) ? length : 0;
    if (shortReads > 0 && rxLength_ > shortReadLength) {
      --shortReads;
      rxLength_ = shortReadLength;
    }
    return static_cast<uint8_t>(rxLength_);
  }
  int available() const { return static_cast<int>(rxLength_ - rxPos_); }
  int read() {
    ++reads;
    return rxPos_ < rxLength_ ? rx_[rxPos_++] : -1;
  }
};
inline TwoWire Wire;
