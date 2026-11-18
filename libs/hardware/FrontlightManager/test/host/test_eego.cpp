#include <FrontlightManager.h>
#include <EegoDeviceId.h>
#include <Wire.h>
#include <cassert>
#include <cmath>
#include <cstdio>

static void reset() {
  Wire = WireStub{};
  Wire.deviceAddress = 0x36;
  fakeEegoUid = 0xBB0409;
  fakeEegoMacOk = true;
}

// LM3630A exponential light model; check total output, not the split algorithm.
static double output(uint8_t code) {
  return code < 4 ? 0 : std::pow(0.85, 44.0 - (code + 1.0) / 5.8);
}

int main() {
  reset();
  FrontlightManager light;
  light.begin();
  assert(light.present() && light.hasColorTemperature());
  for (unsigned brightness = 1; brightness <= 100; ++brightness) {
    light.setColorTemperature(0);
    light.setBrightness(brightness);
    assert(Wire.regs[3] == 0 && Wire.regs[4] >= 4);
    const double total = output(Wire.regs[4]);
    uint8_t priorWarm = 0, priorCool = Wire.regs[4];
    for (unsigned warmth = 0; warmth <= 100; ++warmth) {
      light.setColorTemperature(warmth);
      const uint8_t warm = Wire.regs[3], cool = Wire.regs[4];
      assert(warm == 0 || warm >= 4);
      assert(cool == 0 || cool >= 4);
      const double mixed = output(warm) + output(cool);
      assert(std::abs(mixed / total - 1) < 0.04); // permits brightness-code quantization
      assert(warm >= priorWarm && cool <= priorCool);
      priorWarm = warm; priorCool = cool;
      if (warmth == 100) assert(cool == 0 && warm >= 4);
    }
  }
  light.setColorTemperature(50);
  light.setBrightness(100);
  assert(Wire.regs[3] >= 229 && Wire.regs[4] >= 229);
  light.off();
  assert(light.brightness() == 0 && Wire.regs[3] == 0 && Wire.regs[4] == 0);
  light.on();
  assert(light.brightness() == 100 && Wire.regs[3] && Wire.regs[4]);
  light.off();
  light.prepareForDeepSleep();
  assert(!Wire.visible());

  reset();
  FrontlightManager retry;
  retry.begin();
  Wire.failRegister = 0x50;
  retry.setBrightness(30);
  assert(Wire.regs[3] == 0 && Wire.regs[4] == 0);
  retry.setBrightness(30);
  assert(Wire.regs[3] && Wire.regs[4]);

  reset();
  Wire.present = false;
  FrontlightManager absent;
  absent.begin();
  absent.setBrightness(50);
  assert(!absent.present() && !Wire.visible());

  for (uint32_t uid : {0xBB0408u, 0xBB040Cu, 0xBB0418u}) {
    reset(); fakeEegoUid = uid;
    FrontlightManager unlit;
    unlit.begin(); unlit.setBrightness(50);
    assert(!unlit.present() && !unlit.hasColorTemperature() && !Wire.visible());
  }
  reset(); fakeEegoMacOk = false;
  uint32_t uid = 0x123456;
  assert(!freeink::readEegoA4Uid(uid) && uid == 0x123456);
  FrontlightManager unknownUid;
  unknownUid.begin();
  assert(unknownUid.present()); // preserve probe result if factory UID cannot be read
  std::puts("EEGO frontlight: endpoints, constant-output mix, minimum level, off/sleep, retry and factory UID passed");
}
