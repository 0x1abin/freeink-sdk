#include "FrontlightManager.h"

#if FREEINK_CAP_FRONTLIGHT
#include <M5Pm1.h>

namespace {
constexpr uint32_t maxDuty(uint8_t bits) { return (1u << bits) - 1u; }

// Paper Mono: the PWM lives in the M5PM1 PMIC, not the ESP. PM1 GPIO3 routed to
// alt-function PWM0 drives the AW9967 frontlight driver. Duty register is
// 12-bit; the high byte's bit 4 is the channel-enable bit. Perception-weighted
// like M5Unified's bring-up: duty = brightness^2 scaled into 12 bits.
constexpr uint8_t PM1_PWM_ENABLE = 0x10;

void pm1FrontlightAttach(uint32_t freqHz) {
  freeink::m5pm1::beginBus();
  // GPIO3 to push-pull, alt-function PWM0.
  freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_DRV, 1u << 3, 0);
  freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_FUNC0, 0xC0, 0xC0);
  freeink::m5pm1::writeReg16(freeink::m5pm1::REG_PWM_FREQ_L, static_cast<uint16_t>(freqHz));
}

void pm1FrontlightWrite(uint32_t pct) {
  const uint32_t duty = (pct * pct * 4095u) / 10000u;  // 0-100% -> 12-bit, gamma ~2
  const uint8_t data[2] = {static_cast<uint8_t>(duty & 0xFF),
                           static_cast<uint8_t>(((duty >> 8) & 0x0F) | (duty ? PM1_PWM_ENABLE : 0))};
  freeink::m5pm1::writeBytes(freeink::m5pm1::REG_PWM0_DUTY_L, data, sizeof(data));
}

// Fixed LEDC channels for the Arduino-ESP32 2.x path (3.x keys by GPIO and allocates
// channels itself). Frontlight owns 0 (cool/primary) and 1 (warm); no other SDK LEDC
// user on a frontlight board takes these (the Buzzer uses the 3.x gpio-keyed API).
constexpr uint8_t LEDC_CH_COOL = 0;
constexpr uint8_t LEDC_CH_WARM = 1;

// Turn a 0-100 percentage into a duty, honoring active level. `pct` is pre-clamped.
uint32_t dutyFor(uint32_t pct, uint32_t full, bool activeHigh) {
  uint32_t duty = (pct * full) / 100u;
  return activeHigh ? duty : full - duty;
}

#if defined(ARDUINO) && ESP_ARDUINO_VERSION_MAJOR >= 3
void attachChannel(int8_t gpio, uint8_t /*ch*/, uint32_t freq, uint8_t bits) {
  ledcAttach(gpio, freq, bits);
}
void writeChannel(int8_t gpio, uint8_t /*ch*/, uint32_t duty) { ledcWrite(gpio, duty); }
#else
void attachChannel(int8_t gpio, uint8_t ch, uint32_t freq, uint8_t bits) {
  ledcSetup(ch, freq, bits);
  ledcAttachPin(gpio, ch);
}
void writeChannel(int8_t /*gpio*/, uint8_t ch, uint32_t duty) { ledcWrite(ch, duty); }
#endif
}  // namespace
#endif

void FrontlightManager::begin() {
#if FREEINK_CAP_FRONTLIGHT
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  const auto& i2c = BoardConfig::ACTIVE.i2cFrontlight;
  if (i2c.controller == BoardConfig::I2cFrontlightController::Lm3630a) {
    if (i2c.sda < 0 || i2c.scl < 0 || i2c.enable < 0 || i2c.address == 0) return;
    Wire.begin(i2c.sda, i2c.scl, i2c.i2cHz);
    Wire.setTimeOut(256);
    pinMode(i2c.enable, OUTPUT);
    digitalWrite(i2c.enable, LOW);

    // The OEM firmware contains an LM3630A path, but at least one retail EEGO
    // A4 revision has no frontlight hardware populated. Probe with the recovered
    // enable sequence so an absent option stays off and is not exposed as a
    // capability merely because its driver exists in a shared firmware image.
    digitalWrite(i2c.enable, HIGH);
    delay(2);
    Wire.beginTransmission(i2c.address);
    const bool detected = Wire.endTransmission() == 0;
    digitalWrite(i2c.enable, LOW);
    if (!detected) return;

    _begun = true;
    _brightness = 0;
    return;
  }
  if (fl.viaPm1Pwm) {
    pm1FrontlightAttach(fl.pwmFrequency);
    _begun = true;
    setBrightness(0);
    return;
  }
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;

  attachChannel(fl.gpio, LEDC_CH_COOL, fl.pwmFrequency, fl.pwmResolutionBits);
  if (fl.gpioWarm != BoardConfig::PIN_UNASSIGNED) {
    attachChannel(fl.gpioWarm, LEDC_CH_WARM, fl.pwmFrequency, fl.pwmResolutionBits);
  }
  _begun = true;
  setBrightness(0);
#endif
}

#if FREEINK_CAP_FRONTLIGHT
bool FrontlightManager::lm3630aWrite(const uint8_t reg, const uint8_t value) {
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  Wire.beginTransmission(cfg.address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool FrontlightManager::lm3630aRead(const uint8_t reg, uint8_t& value) {
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  Wire.beginTransmission(cfg.address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(cfg.address, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) != 1) return false;
  value = Wire.read();
  return true;
}

bool FrontlightManager::lm3630aUpdate(const uint8_t reg, const uint8_t mask, const uint8_t value) {
  uint8_t current = 0;
  if (!lm3630aRead(reg, current)) return false;
  return lm3630aWrite(reg, static_cast<uint8_t>((current & ~mask) | (value & mask)));
}

bool FrontlightManager::configureLm3630a() {
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  digitalWrite(cfg.enable, HIGH);
  delay(2);
  Wire.beginTransmission(cfg.address);
  if (Wire.endTransmission() != 0) {
    digitalWrite(cfg.enable, LOW);
    return false;
  }

  // Exact EEGO A4 sequence recovered from HalBacklight/LM3630A in CrossLink.
  // Register meanings follow TI SNVS974B: filter, config, boost, max-current A/B,
  // control, then the two brightness banks.
  bool ok = lm3630aWrite(0x50, 0x03);
  ok = lm3630aUpdate(0x01, 0x07, 0x00) && ok;
  ok = lm3630aWrite(0x02, 0x38) && ok;
  ok = lm3630aUpdate(0x05, 0x1f, 0x10) && ok;
  ok = lm3630aUpdate(0x06, 0x1f, 0x10) && ok;
  ok = lm3630aUpdate(0x00, 0x14, 0x00) && ok;
  ok = lm3630aUpdate(0x00, 0x0b, 0x00) && ok;
  delay(2);
  ok = lm3630aWrite(0x03, 0x00) && ok;
  ok = lm3630aWrite(0x04, 0x00) && ok;
  _i2cConfigured = ok;
  if (!ok) digitalWrite(cfg.enable, LOW);
  return ok;
}

void FrontlightManager::applyLm3630a() {
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  if (_brightness == 0) {
    if (_i2cConfigured) {
      lm3630aWrite(0x03, 0);
      lm3630aWrite(0x04, 0);
    }
    digitalWrite(cfg.enable, LOW);
    _i2cConfigured = false;
    return;
  }
  if (!_i2cConfigured && !configureLm3630a()) return;

  const uint8_t level = static_cast<uint16_t>(_brightness) * 255 / 100;
  uint8_t warm = static_cast<uint16_t>(level) * _warmPercent / 100;
  uint8_t cool = static_cast<uint16_t>(level) * (100 - _warmPercent) / 100;
  // The IC ignores brightness codes 1..3; preserve a visible nonzero request.
  if (warm != 0 && warm < 4) warm = 4;
  if (cool != 0 && cool < 4) cool = 4;

  lm3630aUpdate(0x00, 0x80, 0x00);  // leave software sleep
  delay(2);
  lm3630aWrite(0x03, warm);
  lm3630aWrite(0x04, cool);
  lm3630aUpdate(0x00, 0x04, warm >= 4 ? 0x04 : 0x00);
  lm3630aUpdate(0x00, 0x02, cool >= 4 ? 0x02 : 0x00);
}

void FrontlightManager::apply() {
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  if (!_begun) return;
  if (BoardConfig::ACTIVE.i2cFrontlight.controller == BoardConfig::I2cFrontlightController::Lm3630a) {
    applyLm3630a();
    return;
  }
  if (fl.viaPm1Pwm) {
    pm1FrontlightWrite(_brightness);
    return;
  }
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;

  const uint32_t full = maxDuty(fl.pwmResolutionBits);
  const bool dual = fl.gpioWarm != BoardConfig::PIN_UNASSIGNED;

  // Single channel: the primary carries the whole brightness. Warm/cool pair: split the
  // total brightness between cool and warm by the color-temperature mix, so overall
  // brightness stays ~constant as the color shifts (warm 0 = all cool, 100 = all warm).
  const uint32_t coolPct = dual ? (static_cast<uint32_t>(_brightness) * (100u - _warmPercent)) / 100u
                                : _brightness;
  writeChannel(fl.gpio, LEDC_CH_COOL, dutyFor(coolPct, full, fl.activeHigh));

  if (dual) {
    const uint32_t warmPct = (static_cast<uint32_t>(_brightness) * _warmPercent) / 100u;
    writeChannel(fl.gpioWarm, LEDC_CH_WARM, dutyFor(warmPct, full, fl.activeHigh));
  }
}
#endif

void FrontlightManager::setBrightness(uint8_t percent) {
#if FREEINK_CAP_FRONTLIGHT
  if (percent > 100) percent = 100;
  _brightness = percent;
  if (percent > 0) _lastBrightness = percent;
  apply();
#else
  (void)percent;
#endif
}

void FrontlightManager::off() { setBrightness(0); }
void FrontlightManager::on() { setBrightness(_lastBrightness); }

void FrontlightManager::setColorTemperature(uint8_t warmPercent) {
#if FREEINK_CAP_FRONTLIGHT
  _warmPercent = warmPercent > 100 ? 100 : warmPercent;
  // Only re-drives hardware when a warm channel exists; on single-channel boards this just
  // records the request (apply() ignores _warmPercent without a second channel).
  apply();
#else
  (void)warmPercent;
#endif
}
