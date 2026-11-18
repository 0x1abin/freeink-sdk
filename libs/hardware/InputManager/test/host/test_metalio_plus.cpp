#include <InputManager.h>
#include <MetalioPlusIo.h>
#include <Wire.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "native.h"
bool busOk = true, lockOk = true, ioReady = false, portOk = true, held = false;
unsigned lockCalls = 0, unlockCalls = 0, resetCalls = 0, readCalls = 0, writeCalls = 0;
uint8_t port1 = 255, report[11] = {};
extern "C" bool metalio_i2c1_lock(int timeout) {
  assert(timeout > 0 && !held);
  ++lockCalls;
  held = lockOk;
  return held;
}
extern "C" void metalio_i2c1_unlock() {
  assert(held);
  held = false;
  ++unlockCalls;
}
extern "C" int metalio_i2c1_transmit_receive_locked(int port, uint8_t addr, const uint8_t* reg, size_t tx, uint8_t* out,
                                                    size_t n, int timeout) {
  assert(held && port == 1 && addr == 0x38 && tx == 1 && timeout > 0);
  ++readCalls;
  if (!busOk) return -1;
  if (*reg == 0x02) {
    assert(n == 11);
    memcpy(out, report, n);
  } else {
    assert(*reg == 0xa3 && n == 1);
    *out = 0x64;
  }
  return 0;
}
extern "C" int metalio_i2c1_write_reg_locked(int port, uint8_t addr, uint8_t reg, const uint8_t* data, size_t n,
                                             int timeout) {
  assert(held && port == 1 && addr == 0x38 && reg == 0xa4 && n == 1 && *data == 0 && timeout > 0);
  ++writeCalls;
  return busOk ? 0 : -1;
}
extern "C" bool epd_board_metalio_eink4_plus_ioexp_ok() { return ioReady; }
extern "C" void epd_board_metalio_eink4_plus_tp_reset() {
  assert(!held);
  ++resetCalls;
}
extern "C" bool epd_board_metalio_eink4_plus_ioexp_read_port(int p, uint8_t* out) {
  assert(p == 1 && !held);
  if (!portOk) return false;
  *out = port1;
  return true;
}
static void point(InputManager& in, unsigned x, unsigned y, unsigned count = 1, unsigned event = 2,
                  unsigned elapsed = 25) {
  memset(report, 0, sizeof(report));
  report[0] = count;
  report[1] = (event << 6) | (x >> 8);
  report[2] = x;
  report[3] = y >> 8;
  report[4] = y;
  fakeNow += elapsed;
  in.update();
}
static void up(InputManager& in) { point(in, 0, 0, 0); }
static bool tap(InputManager& in) {
  float x, y;
  return in.wasTouchTap(x, y);
}
int main(int argc, char** argv) {
  const bool checkCleanup = argc == 1 || strcmp(argv[1], "strip") != 0;
  const bool checkButtons = argc == 1 || strcmp(argv[1], "screen") != 0;
  static_assert(FREEINK_CAP_TOUCH && FREEINK_TOUCH_FT6336U);
  std::fill(std::begin(gpioLevels), std::end(gpioLevels), HIGH);
  assert(!freeink::metalioPlusTouchBegin() && resetCalls == 0);
  ioReady = true;
  assert(freeink::metalioPlusTouchBegin());
  assert(resetCalls == 1 && writeCalls == 1 && lockCalls == unlockCalls);
  assert(freeink::metalioPlusTouchBegin() && resetCalls == 1);
  InputManager in;
  in.begin();
  assert(in.hasTouch());
  point(in, 0, 0);
  assert(in.wasTouchPressed());
  auto p = in.getTouchPoint();
  assert(p.x == 0 && p.y == 683);
  point(in, 683, 1215);
  p = in.getTouchPoint();
  assert(p.x == 1215 && p.y == 0);
  up(in);
  assert(!tap(in));
  point(in, 100, 200);
  up(in);
  assert(tap(in));
  point(in, 100, 200);
  busOk = false;
  fakeNow += 25;
  in.update();
  assert(!in.isTouchPressed() && !tap(in));
  if (checkCleanup) assert(in.wasTouchReleased());
  busOk = true;
  point(in, 100, 200);
  assert(!in.isTouchPressed());
  up(in);
  point(in, 100, 200);
  up(in);
  assert(tap(in));  // recovery only after a genuine release
  point(in, 200, 1600);
  point(in, 200, 1600);
  assert(in.isPressed(InputManager::BTN_UP) && !in.isTouchPressed());
  assert(in.capacitivePageButtonMask() == (1u << InputManager::BTN_UP));
  point(in, 100, 200);
  bool crossedRelease = in.wasReleased(InputManager::BTN_UP);
  point(in, 100, 200);
  crossedRelease |= in.wasReleased(InputManager::BTN_UP);
  if (checkButtons) assert(!crossedRelease);
  assert(!in.isTouchPressed() && !in.isPressed(InputManager::BTN_UP));
  up(in);
  assert(!tap(in));
  point(in, 300, 1600);
  point(in, 300, 1600);
  in.suppressTouchContact();
  assert(!in.capacitivePageButtonMask());
  up(in);
  bool suppressedRelease = in.wasReleased(InputManager::BTN_DOWN);
  up(in);
  suppressedRelease |= in.wasReleased(InputManager::BTN_DOWN);
  if (checkButtons) assert(!suppressedRelease);
  point(in, 100, 200);
  point(in, 300, 1600);
  point(in, 300, 1600);
  assert(!in.isPressed(InputManager::BTN_DOWN) && !tap(in));
  up(in);
  assert(!tap(in));
  point(in, 100, 1600);
  assert(in.wasHomeKeyPressed() && in.isHomeKeyDown());
  up(in);
  assert(in.wasHomeKeyTapped());
  point(in, 100, 1600);
  point(in, 100, 1600, 1, 2, 800);
  assert(in.wasHomeKeyLongPressed());
  up(in);
  assert(!in.wasHomeKeyTapped());
  point(in, 300, 1600);
  point(in, 300, 1600);
  busOk = false;
  fakeNow += 25;
  in.update();
  bool failedRelease = in.wasReleased(InputManager::BTN_DOWN);
  fakeNow += 25;
  in.update();
  failedRelease |= in.wasReleased(InputManager::BTN_DOWN);
  if (checkButtons) assert(!failedRelease);
  assert(!in.isPressed(InputManager::BTN_DOWN) && !in.capacitivePageButtonMask());
  busOk = true;
  up(in);
  point(in, 100, 200, 2);
  point(in, 100, 200, 1);
  up(in);
  assert(!tap(in));
  point(in, 684, 1215);
  assert(!in.isTouchPressed());
  up(in);
  point(in, 200, 1600, 1, 3);
  assert(!in.capacitivePageButtonMask());
  up(in);
  port1 = 254;
  point(in, 0, 0, 0);
  point(in, 0, 0, 0);
  assert(in.isPressed(InputManager::BTN_UP));
  portOk = false;
  point(in, 0, 0, 0);
  point(in, 0, 0, 0);
  assert(!in.isPressed(InputManager::BTN_UP));
  if (checkButtons) assert(!in.wasReleased(InputManager::BTN_UP));
  // A real held physical key survives strip cancellation and later releases normally.
  portOk = true;
  port1 = 254;
  point(in, 200, 1600);
  point(in, 200, 1600);
  point(in, 100, 200);
  point(in, 100, 200);
  assert(in.isPressed(InputManager::BTN_UP));
  port1 = 255;
  up(in);
  up(in);
  assert(in.wasReleased(InputManager::BTN_UP));
  assert(!Wire.started && Wire.address == 0 && resetCalls == 1 && !held);
  // Lock acquisition failure must not unlock an unowned mutex or retain stale data.
  auto unlocks = unlockCalls;
  lockOk = false;
  uint8_t frame[11];
  assert(!freeink::metalioPlusTouchRead(frame, sizeof(frame)));
  assert(unlockCalls == unlocks);
  puts("Plus native input: touch, strip, failures, home and expander debounce passed");
}
