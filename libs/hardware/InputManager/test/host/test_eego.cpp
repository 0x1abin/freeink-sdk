#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <Wire.h>
#include <driver/gpio.h>

#include <cassert>
#include <cstdio>

#define private public
#include <InputManager.h>
#undef private
#include "../../src/gsl/EegoA4GslFirmware.h"

int main() {
  InputManager input;
  assert(input.gslUploadFirmware());
  assert(Wire.writes.size() == 417);
  size_t entryIndex = 0;
  for (const auto &tx : Wire.writes) {
    assert(tx.size() >= 2 && tx.size() <= 65);
    assert(entryIndex < freeink::EEGO_A4_GSL_FIRMWARE_LEN);
    if (tx[0] == 0xF0) {
      const auto &entry = freeink::EEGO_A4_GSL_FIRMWARE[entryIndex++];
      assert(tx.size() == 2 && entry.reg == 0xF0 && tx[1] == entry.value);
      continue;
    }
    assert((tx.size() - 1) % 4 == 0);
    for (size_t offset = 1; offset < tx.size(); offset += 4) {
      assert(entryIndex < freeink::EEGO_A4_GSL_FIRMWARE_LEN);
      const auto &entry = freeink::EEGO_A4_GSL_FIRMWARE[entryIndex++];
      const uint32_t value =
          uint32_t(tx[offset]) | (uint32_t(tx[offset + 1]) << 8) |
          (uint32_t(tx[offset + 2]) << 16) | (uint32_t(tx[offset + 3]) << 24);
      assert(entry.reg != 0xF0 && entry.reg == tx[0] + offset - 1 &&
             value == entry.value);
    }
  }
  assert(entryIndex == freeink::EEGO_A4_GSL_FIRMWARE_LEN);

  for (const int failAt : {0, 1, 16, 416}) {
    Wire = {};
    Wire.failAt = failAt;
    assert(!input.gslUploadFirmware());
    assert(Wire.writes.size() == static_cast<size_t>(failAt + 1));
  }

  Wire = {};
  input.beginGslx680();
  assert(input.hasTouch());
  // A successful ACK cannot hide a later failed upload or invalid firmware
  // magic.
  Wire = {};
  Wire.failAt = 25;
  loggedErrors = 0;
  input.beginGslx680();
  assert(!input.hasTouch() && loggedErrors == 1 && Wire.writes.size() == 26);
  Wire = {};
  Wire.magicValid = false;
  loggedErrors = 0;
  input.beginGslx680();
  assert(!input.hasTouch() && loggedErrors == 1);

  for (const bool nack : {false, true}) {
    Wire = {};
    Wire.failAt = nack ? 0 : -1;
    input.touchDataEnabled = true;
    gpioLevels[3] = HIGH;
    gpioHeld[3] = true;
    assert(input.prepareForDeepSleep() == !nack);
    assert(Wire.writes.size() == 1 &&
           Wire.writes[0] == std::vector<uint8_t>({0xE0, 0x88}));
    assert(Wire.ended && gpioLevels[3] == LOW && gpioHeld[3] &&
           !input.hasTouch());
    Wire = {};
    input.beginGslx680();
    assert(!gpioHeld[3] && gpioLevels[3] == HIGH && input.hasTouch());
  }
  Wire = {};
  input.touchDataEnabled = true;
  Wire.homeDown = true;
  fakeNow += 100;
  input.update();
  assert(input.wasHomeKeyPressed() &&
         !input.wasPressed(InputManager::BTN_BACK));
  Wire.homeDown = false;
  fakeNow += 100;
  input.update();
  assert(input.wasHomeKeyTapped() &&
         !input.wasPressed(InputManager::BTN_BACK) &&
         !input.wasReleased(InputManager::BTN_BACK));
  fakeNow += 100;
  input.update();
  assert(!input.wasHomeKeyTapped());
  Wire.homeDown = true;
  fakeNow += 100;
  input.update();
  fakeNow += 800;
  input.update();
  assert(input.wasHomeKeyLongPressed() &&
         !input.wasPressed(InputManager::BTN_BACK));
  Wire.homeDown = false;
  fakeNow += 100;
  input.update();
  assert(!input.wasHomeKeyTapped() &&
         !input.wasReleased(InputManager::BTN_BACK));
  // One continuous GSL contact cannot change from Home to screen or vice versa.
  const auto sample = [&](bool home, bool screen) {
    Wire.homeDown = home;
    Wire.screenDown = screen;
    fakeNow += 100;
    input.update();
  };
  for (const bool startHome : {false, true}) {
    sample(startHome, !startHome);
    assert(startHome ? input.wasHomeKeyPressed() : input.touchPressedEvent);
    sample(!startHome, startHome);
    assert(!input.wasHomeKeyTapped() && !input.wasHomeKeyPressed() &&
           !input.touchPressedEvent);
    sample(startHome, !startHome);
    assert(!input.wasHomeKeyTapped() && !input.wasHomeKeyPressed() &&
           !input.touchPressedEvent);
    sample(false, false);
    assert(!input.wasHomeKeyTapped());
    sample(false, true);
    assert(input.touchPressedEvent && input.touchPressed);
    sample(false, false);
    float x, y;
    assert(input.wasTouchTap(x, y));
  }
  sample(true, false);
  assert(input.wasHomeKeyPressed());
  input.suppressTouchContact();
  sample(false, false);
  assert(!input.wasHomeKeyTapped());
  sample(true, false);
  sample(false, false);
  assert(input.wasHomeKeyTapped());
  input.clearTouchTapEvent();
  assert(!input.wasHomeKeyTapped());
  std::puts("A4 firmware, failure handling, sleep/wake and Home without "
            "synthetic Back passed");
}
