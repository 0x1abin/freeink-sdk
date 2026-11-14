#include <MetalioPlusPower.h>
#include <native_api.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {
std::string fault;
std::array<uint8_t, 8> regs;
std::vector<int> writes;
bool locked = false;
unsigned long elapsed = 0;
int readyAddress = 0x20;
int failureSeen = 0;
bool fails(const std::string& operation) {
  if (fault != operation) return false;
  ++failureSeen;
  return true;
}
}  // namespace

void delay(unsigned long ms) {
  assert(!locked && ms == 80);
  if (elapsed == 0) {
    assert((regs[3] & 0x10) && !(regs[7] & 0x10));
    if (fault != "cold") assert(!(regs[2] & 0x20));
  } else {
    assert((regs[2] & 0x20) && !(regs[6] & 0x20));
  }
  elapsed += ms;
}

esp_err_t gpio_hold_dis(int pin) {
  assert(pin == 0 || pin == 1);
  return fails(pin == 0 ? "sda" : "scl") ? ESP_FAIL : ESP_OK;
}
esp_err_t metalio_i2c1_init(int sda, int scl) {
  assert(sda == 0 && scl == 1 && !locked);
  return fails("init") ? ESP_FAIL : ESP_OK;
}
bool metalio_i2c1_lock(int ms) {
  assert(ms > 0 && !locked);
  if (fails("lock")) return false;
  locked = true;
  return true;
}
void metalio_i2c1_unlock() {
  assert(locked);
  locked = false;
}
esp_err_t metalio_i2c1_probe_locked(int port, uint8_t address, int timeout) {
  assert(locked && port == 1 && timeout > 0);
  return fault == "missing" || address != readyAddress ? ESP_FAIL : ESP_OK;
}
esp_err_t metalio_i2c1_read_reg_stop_locked(int port, uint8_t address, uint8_t reg, uint8_t* value, size_t size,
                                            int timeout) {
  assert(locked && port == 1 && address == readyAddress && size == 1 && timeout > 0);
  assert(failureSeen == 0);
  if (fails("read" + std::to_string(reg))) return ESP_FAIL;
  *value = regs.at(reg);
  return ESP_OK;
}
esp_err_t metalio_i2c1_write_reg_locked(int port, uint8_t address, uint8_t reg, const uint8_t* value, size_t size,
                                        int timeout) {
  assert(locked && port == 1 && address == readyAddress && size == 1 && timeout > 0);
  assert(failureSeen == 0);
  if (fails("write" + std::to_string(reg))) return ESP_FAIL;
  regs.at(reg) = *value;
  writes.push_back(reg);
  return ESP_OK;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  fault = argv[1];
  regs = {0, 0, 0x09, 0x87, 0, 0, 0x00, 0x80};
  if (fault == "cold") regs.fill(0xff);
  if (fault == "alternate") readyAddress = 0x24;
  const auto before = regs;
  const bool success = fault == "retained" || fault == "cold" || fault == "alternate";
  assert(freeink::metalioPlusRestoreBootRails() == success);
  assert(!locked);
  if (success) {
    assert(elapsed == 160);
    assert(regs[2] == (before[2] | 0x20) && regs[3] == (before[3] | 0x10));
    assert(regs[6] == (before[6] & 0xdf) && regs[7] == (before[7] & 0xef));
    for (int reg : {0, 1, 4, 5}) assert(regs[reg] == before[reg]);
    assert((writes == std::vector<int>{3, 7, 2, 6}));
  } else {
    assert(elapsed <= 80);
    if (fault == "sda" || fault == "scl" || fault == "init" || fault == "lock" || fault == "missing")
      assert(writes.empty());
  }
  std::printf("Plus boot %s passed\n", argv[1]);
}
