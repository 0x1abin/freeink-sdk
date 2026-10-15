#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

struct WireStub {
  std::vector<std::vector<uint8_t>> writes;
  std::vector<uint8_t> tx;
  uint8_t reg = 0;
  unsigned remaining = 0;
  int failAt = -1;
  bool magicValid = true;
  bool ended = false;
  bool homeDown = false;
  bool screenDown = false;
  unsigned readAt = 0;

  bool begin(int, int, uint32_t) {
    ended = false;
    return true;
  }
  void end() { ended = true; }
  void setTimeOut(int) {}
  void beginTransmission(uint8_t address) {
    assert(address == 0x40);
    tx.clear();
  }
  size_t write(uint8_t value) {
    tx.push_back(value);
    return 1;
  }
  size_t write(const uint8_t *data, size_t length) {
    tx.insert(tx.end(), data, data + length);
    return length;
  }
  int endTransmission(bool = true) {
    const auto index = static_cast<int>(writes.size());
    writes.push_back(tx);
    reg = tx.at(0);
    return index == failAt ? 1 : 0;
  }
  uint8_t requestFrom(uint8_t, uint8_t length, uint8_t) {
    remaining = length;
    readAt = 0;
    return length;
  }
  unsigned available() const { return remaining; }
  uint8_t read() {
    --remaining;
    const unsigned index = readAt++;
    if (reg == 0x80 && homeDown) {
      constexpr uint8_t homeFrame[] = {1, 0, 0, 0, 0x20, 0x10, 0xA0, 0x03};
      return index < sizeof(homeFrame) ? homeFrame[index] : 0;
    }
    if (reg == 0x80 && screenDown) {
      constexpr uint8_t screenFrame[] = {1, 0, 0, 0, 0x54, 0x01, 0x80, 0x02};
      return index < sizeof(screenFrame) ? screenFrame[index] : 0;
    }
    return reg == 0xB0 && magicValid ? 0x5A : 0;
  }
};

inline WireStub Wire;
