#include <FrontlightManager.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
struct Pad {
  bool held = false;
  bool attached = false;
  uint32_t duty = 0;
  uint32_t frequency = 0;
  uint8_t bits = 0;
  int level = 0;
};
std::array<Pad, 64> pads;
std::vector<uint32_t> written;
int cool = 0, warm = 0;
uint32_t maximum = 0;
void checkOutput(bool off = false) {
  const bool high = BoardConfig::ACTIVE.frontlight.activeHigh;
  const uint32_t c = high ? pads[cool].duty : 1023 - pads[cool].duty;
  const uint32_t w = high ? pads[warm].duty : 1023 - pads[warm].duty;
  assert(c <= maximum && w <= maximum);
  if (off)
    assert(c == 0 && w == 0);
  else
    assert(c || w);
}
}  // namespace

unsigned long millis() { return fakeNow; }
void delay(unsigned long ms) { fakeNow += ms; }
bool ledcAttach(uint8_t pin, uint32_t frequency, uint8_t bits) {
  assert(!pads[pin].held && bits == 10);
  pads[pin].attached = true;
  pads[pin].frequency = frequency;
  pads[pin].bits = bits;
  return true;
}
bool ledcWrite(uint8_t pin, uint32_t duty) {
  assert(pads[pin].attached && !pads[pin].held);
  assert(duty <= 1023);
  pads[pin].duty = duty;
  written.push_back(duty);
  return true;
}
bool ledcDetach(uint8_t pin) {
  pads[pin].attached = false;
  return true;
}
void gpio_hold_dis(int pin) { pads[pin].held = false; }
void gpio_hold_en(int pin) { pads[pin].held = true; }
void pinMode(int pin, int mode) { assert((pin == cool || pin == warm) && mode == OUTPUT && !pads[pin].attached); }
void digitalWrite(int pin, int level) {
  assert(!pads[pin].attached && !pads[pin].held);
  pads[pin].level = level;
}

int main() {
  assert(FREEINK_CAP_FRONTLIGHT && FREEINK_CAP_WARMLIGHT);
  const auto& cfg = BoardConfig::ACTIVE.frontlight;
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  assert(cfg.gpio == 6 && cfg.gpioWarm == 7 && cfg.pwmFrequency == 20000 && cfg.pwmResolutionBits == 10);
  maximum = 102;  // Vendor 10% of the physical 10-bit range, not full LED duty.
#else
  assert(cfg.gpio == 8 && cfg.gpioWarm == 9);
  maximum = 1023;  // Existing X4 Pro output must retain its full range.
#endif
  cool = cfg.gpio;
  warm = cfg.gpioWarm;
  pads[cool].held = pads[warm].held = true;
  FrontlightManager light;
  light.begin();
  assert(light.present() && light.hasColorTemperature());
  assert(pads[cool].frequency == cfg.pwmFrequency && pads[warm].frequency == cfg.pwmFrequency);
  checkOutput(true);
  std::array<uint32_t, 101> percentTotals{};
  std::array<uint32_t, 256> levelTotals{};
  for (unsigned warmth = 0; warmth <= 100; ++warmth) {
    light.setColorTemperature(warmth);
    for (unsigned brightness = 0; brightness <= 100; ++brightness) {
      light.setBrightness(brightness);
      checkOutput(brightness == 0);
      const auto total = pads[cool].duty + pads[warm].duty;
      if (warmth == 0)
        percentTotals[brightness] = total;
      else
        assert(total == percentTotals[brightness]);
      if (warmth == 0) assert(pads[warm].duty == 0);
      if (warmth == 100) assert(pads[cool].duty == 0);
    }
    for (unsigned level = 0; level <= 255; ++level) {
      light.setBrightnessLevel(level);
      checkOutput(level == 0);
      const auto total = pads[cool].duty + pads[warm].duty;
      if (warmth == 0)
        levelTotals[level] = total;
      else
        assert(total == levelTotals[level]);
    }
  }
  light.setBrightness(100);
  light.setColorTemperature(0);
  assert(pads[cool].duty == maximum && pads[warm].duty == 0);
  light.setColorTemperature(100);
  assert(pads[warm].duty == maximum && pads[cool].duty == 0);
  light.off();
  checkOutput(true);
  light.on();
  checkOutput();

  // The ceiling also covers off->on boost kicks, before the settled output.
  BoardConfig::ACTIVE.frontlight.minStartPermille = 800;
  BoardConfig::ACTIVE.frontlight.minHoldPermille = 200;
  light.off();
  written.clear();
  light.setBrightness(1);
  assert(fakeNow == 30);
  assert(std::all_of(written.begin(), written.end(), [](auto duty) { return duty <= maximum; }));

  // Active-low off is full physical duty, even with a lower logical ceiling.
  BoardConfig::ACTIVE.frontlight.activeHigh = false;
  light.off();
  checkOutput(true);
  light.setBrightness(100);
  checkOutput();
  light.prepareForDeepSleep();
  for (const auto pin : {cool, warm}) assert(pads[pin].held && !pads[pin].attached && pads[pin].level == HIGH);
  FrontlightManager awake;
  awake.begin();
  assert(!pads[cool].held && !pads[warm].held);
  checkOutput(true);
  awake.setBrightness(100);
  checkOutput();
  std::puts("Brightness/level/warmth sweeps, ceiling, kick, polarity and held-pad wake passed");
}
