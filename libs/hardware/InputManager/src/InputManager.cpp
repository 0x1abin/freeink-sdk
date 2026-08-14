#include "InputManager.h"

#if FREEINK_CAP_TOUCH
#include <Wire.h>
#include <driver/gpio.h>

#include <cstring>
#if FREEINK_DEVICE_EEGO_A4
#include "gsl/EegoA4GslFirmware.h"
#endif
#endif
#if FREEINK_DEVICE_PAPERMONO
#include <PaperMonoBoard.h>
#endif
#if defined(TOUCH_PROBE_DEBUG)
#include <esp_rom_sys.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#endif

// Recorded ADC values from real devices
// BACK CONF LEFT RGHT   UP DOWN
// 3597 2760 1530    6 2300    6
// 3470 2666 1480    6 2222    5
// 3470 2655 1470    3 2205    3
//
// Averages
// BACK CONF LEFT RGHT   UP DOWN
// 3512 2694 1493    5 2242    5
//
// Setup ranges, if ADC value is between value `i` and `i + 1`, button `i` is
// being pressed. These ranges are based on real world values above, and are
// much more tolerant of different devices than a fixed threshold check. They
// are calculated by taking the midpoint of the pairs of averaged values above.
const int InputManager::ADC_RANGES_1[] = {ADC_NO_BUTTON, 3100, 2090, 750, INT32_MIN};
const int InputManager::ADC_RANGES_2[] = {ADC_NO_BUTTON, 1120, INT32_MIN};
const char* InputManager::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};

namespace {
constexpr int absInt(const int value) { return value < 0 ? -value : value; }

constexpr bool movedBeyondSlop(const int dx, const int dy, const int slop) {
  return absInt(dx) > slop || absInt(dy) > slop;
}

#if FREEINK_DEVICE_MOFEI_M4
constexpr uint8_t FT6336_REG_TD_STATUS = 0x02;
constexpr uint8_t FT6336_REG_TOUCH1 = 0x03;
constexpr uint8_t FT6336_REG_LIB_VERSION = 0xA1;
constexpr uint8_t FT6336_REG_FIRMWARE_ID = 0xA6;
constexpr uint8_t FT6336_REG_VENDOR_ID = 0xA8;
constexpr uint32_t FT6336_POLL_MS = 10;
constexpr uint16_t ft6336Axis(const uint8_t high, const uint8_t low) {
  return static_cast<uint16_t>(high & 0x0F) << 8 | low;
}
static_assert(ft6336Axis(0x41, 0x23) == 0x0123, "FT6336 coordinate decoding must ignore event/id bits");
#endif

#if defined(TOUCH_PROBE_DEBUG)
void touchDebugPrintf(const char* format, ...) {
  char buf[192];
  va_list args;
  va_start(args, format);
  const int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (len < 0) return;
  const size_t n = strnlen(buf, sizeof(buf));
#if FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_USB_CDC_WRITE
  Serial.write(reinterpret_cast<const uint8_t*>(buf), n);
#elif FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF
  esp_rom_printf("%s", buf);
#else
  if (Serial) {
    Serial.print(buf);
  }
#endif
}
#endif
}  // namespace

InputManager::InputManager()
    : currentState(0),
      lastState(0),
      pressedEvents(0),
      releasedEvents(0),
      lastDebounceTime(0),
      buttonPressStart(0),
      buttonPressFinish(0),
      powerButtonPressStart(0),
      powerButtonPressFinish(0),
      confirmBackPressStart(0),
      confirmBackPhysicalPressed(false),
      confirmBackLongPressActive(false),
      confirmPowerPressStart(0),
      confirmPowerPhysicalPressed(false),
      confirmPowerLongPressActive(false),
      twoButtonPhysicalState(0),
      twoButtonPressStart(0),
      twoButtonLongPressActive(false) {}

void InputManager::begin() {
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::XteinkAdcLadder) {
    pinMode(BUTTON_ADC_PIN_1, INPUT);
    pinMode(BUTTON_ADC_PIN_2, INPUT);
    pinMode(BoardConfig::ACTIVE.input.power, BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    analogSetAttenuation(ADC_11db);
    beginTouch();
    return;
  }

  const int8_t pins[] = {BoardConfig::ACTIVE.input.back, BoardConfig::ACTIVE.input.confirm,
                         BoardConfig::ACTIVE.input.left, BoardConfig::ACTIVE.input.right,
                         BoardConfig::ACTIVE.input.up,   BoardConfig::ACTIVE.input.down};
  for (const int8_t pin : pins) {
    if (pin >= 0) {
      pinMode(pin, INPUT_PULLUP);
    }
  }
  if (BoardConfig::ACTIVE.input.power >= 0) {
    pinMode(BoardConfig::ACTIVE.input.power, BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
  beginTouch();
}

int InputManager::getButtonFromADC(const int adcValue, const int ranges[], const int numButtons) {
  for (int i = 0; i < numButtons; i++) {
    if (ranges[i + 1] < adcValue && adcValue <= ranges[i]) {
      return i;
    }
  }

  return -1;
}

void InputManager::readButtonAdc(ButtonAdcSample& group1, ButtonAdcSample& group2) {
  group1 = {BUTTON_ADC_PIN_1, -1, -1};
  group2 = {BUTTON_ADC_PIN_2, -1, -1};
  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder) {
    return;
  }

  group1.raw = analogRead(BUTTON_ADC_PIN_1);
  group1.button = getButtonFromADC(group1.raw, ADC_RANGES_1, NUM_BUTTONS_1);

  group2.raw = analogRead(BUTTON_ADC_PIN_2);
  const int b2 = getButtonFromADC(group2.raw, ADC_RANGES_2, NUM_BUTTONS_2);
  group2.button = b2 >= 0 ? b2 + 4 : -1;  // map group-2 local 0/1 to BTN_UP / BTN_DOWN
}

uint8_t InputManager::getState() {
  uint8_t state = 0;

  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder) {
    state = getDigitalState();
    state |= serviceTouch();                    // run the touch machine; OR any synthesized button
    if (s_buttonHook) state |= s_buttonHook();  // board buttons (e.g. I2C expander)
    return state;
  }

  // Read GPIO1 buttons
  const int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons
  const int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }

  // Read power button (polarity per board; X4 active-LOW, de-link active-HIGH)
  const int powerActiveLevel = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
  if (digitalRead(BoardConfig::ACTIVE.input.power) == powerActiveLevel) {
    state |= (1 << BTN_POWER);
  }

  state |= serviceTouch();
  if (s_buttonHook) state |= s_buttonHook();  // board buttons (e.g. I2C expander)
  return state;
}

InputManager::ButtonHook InputManager::s_buttonHook = nullptr;

void InputManager::beginAsync(const uint8_t taskPriority, const uint32_t pollMs, const uint8_t queueLen) {
  if (_asyncTask) return;  // already running
  _asyncPollMs = pollMs;
  _asyncQueue = xQueueCreate(queueLen, sizeof(uint8_t));
  if (!_asyncQueue) return;
  _asyncTapQueue = xQueueCreate(queueLen, sizeof(float) * 2);
  _asyncSwipeQueue = xQueueCreate(queueLen, sizeof(float) * 4);
  xTaskCreate(asyncTaskTrampoline, "fi_input", 4096, this, taskPriority, &_asyncTask);
}

void InputManager::asyncTaskTrampoline(void* self) { static_cast<InputManager*>(self)->asyncPoll(); }

void InputManager::asyncPoll() {
  static const uint8_t kButtons[] = {BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_POWER};
  for (;;) {
    update();
    for (const uint8_t b : kButtons) {
      if (wasPressed(b)) xQueueSend(_asyncQueue, &b, 0);
    }
    float tap[2];
    if (_asyncTapQueue && wasTouchTap(tap[0], tap[1])) {
      xQueueSend(_asyncTapQueue, tap, 0);
    }
    float swipe[4];
    if (_asyncSwipeQueue && wasSwipe(swipe[0], swipe[1], swipe[2], swipe[3])) {
      xQueueSend(_asyncSwipeQueue, swipe, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(_asyncPollMs));
  }
}

bool InputManager::popPress(uint8_t& button) {
  if (!_asyncQueue) return false;
  return xQueueReceive(_asyncQueue, &button, 0) == pdTRUE;
}

bool InputManager::popTouchTap(float& nx, float& ny) {
  if (!_asyncTapQueue) return false;
  float tap[2];
  if (xQueueReceive(_asyncTapQueue, tap, 0) != pdTRUE) return false;
  nx = tap[0];
  ny = tap[1];
  return true;
}

bool InputManager::popSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) {
  if (!_asyncSwipeQueue) return false;
  float swipe[4];
  if (xQueueReceive(_asyncSwipeQueue, swipe, 0) != pdTRUE) return false;
  nxStart = swipe[0];
  nyStart = swipe[1];
  nxEnd = swipe[2];
  nyEnd = swipe[3];
  return true;
}

bool InputManager::isDigitalPressed(const int8_t pin) const { return pin >= 0 && digitalRead(pin) == LOW; }

uint8_t InputManager::getDigitalState() const {
  uint8_t state = 0;

  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmBackHold &&
      BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmPowerHold) {
    if (isDigitalPressed(BoardConfig::ACTIVE.input.back)) state |= (1 << BTN_BACK);
    if (isDigitalPressed(BoardConfig::ACTIVE.input.confirm)) state |= (1 << BTN_CONFIRM);
  }

  if (isDigitalPressed(BoardConfig::ACTIVE.input.left)) state |= (1 << BTN_LEFT);
  if (isDigitalPressed(BoardConfig::ACTIVE.input.right)) state |= (1 << BTN_RIGHT);
  if (isDigitalPressed(BoardConfig::ACTIVE.input.up)) state |= (1 << BTN_UP);
  if (isDigitalPressed(BoardConfig::ACTIVE.input.down)) state |= (1 << BTN_DOWN);
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  const bool powerPressed =
      powerPin >= 0 && digitalRead(powerPin) == (BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW);
  if (powerPressed && BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmBackHold &&
      BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmPowerHold) {
    state |= (1 << BTN_POWER);
  }

  return state;
}

void InputManager::applyStateChange(const uint8_t state, const unsigned long currentTime) {
  pressedEvents = state & ~currentState;
  releasedEvents = currentState & ~state;

  if (pressedEvents > 0 && currentState == 0) {
    buttonPressStart = currentTime;
  }

  if (releasedEvents > 0 && state == 0) {
    buttonPressFinish = currentTime;
  }

  if (pressedEvents & (1 << BTN_POWER)) {
    powerButtonPressStart = currentTime;
  }

  if (releasedEvents & (1 << BTN_POWER)) {
    powerButtonPressFinish = currentTime;
  }

  currentState = state;
  // Keep lastState in sync with the committed state so isDebouncePending() is
  // meaningful on every input style. A no-op for the debounced ADC path (state
  // already equals lastState at commit time), but the hold-style updates call
  // applyStateChange() directly without ever sampling through the debounce.
  lastState = state;
}

void InputManager::updateConfirmBackHold(const unsigned long currentTime) {
  const bool pressed = isDigitalPressed(BoardConfig::ACTIVE.input.confirm);
  const uint8_t nonSharedState = getDigitalState();
  bool emitConfirmClick = false;

  if (pressed && !confirmBackPhysicalPressed) {
    confirmBackPhysicalPressed = true;
    confirmBackLongPressActive = false;
    confirmBackPressStart = currentTime;
  }

  uint8_t nextState = nonSharedState;
  if (pressed && currentTime - confirmBackPressStart >= CONFIRM_BACK_HOLD_MS) {
    confirmBackLongPressActive = true;
    nextState |= (1 << BTN_BACK);
  }

  if (!pressed && confirmBackPhysicalPressed) {
    confirmBackPhysicalPressed = false;
    if (!confirmBackLongPressActive) {
      emitConfirmClick = true;
      buttonPressStart = confirmBackPressStart;
      buttonPressFinish = currentTime;
    }
    confirmBackLongPressActive = false;
  }

  applyStateChange(nextState, currentTime);

  if (emitConfirmClick) {
    pressedEvents |= (1 << BTN_CONFIRM);
    releasedEvents |= (1 << BTN_CONFIRM);
  }
}

void InputManager::updateConfirmPowerHold(const unsigned long currentTime) {
  const int8_t sharedPin =
      BoardConfig::ACTIVE.input.confirm >= 0 ? BoardConfig::ACTIVE.input.confirm : BoardConfig::ACTIVE.input.power;
  const bool pressed = isDigitalPressed(sharedPin);
  uint8_t nonSharedState = getDigitalState();
  nonSharedState |= serviceTouch();
  if (s_buttonHook) nonSharedState |= s_buttonHook();
  bool emitConfirmClick = false;

  if (pressed && !confirmPowerPhysicalPressed) {
    confirmPowerPhysicalPressed = true;
    confirmPowerLongPressActive = false;
    confirmPowerPressStart = currentTime;
  }

  uint8_t nextState = nonSharedState;
  if (pressed && s_sharedConfirmPowerShortPressEmitsPower) {
    nextState |= (1 << BTN_POWER);
  } else if (pressed && currentTime - confirmPowerPressStart >= CONFIRM_POWER_HOLD_MS) {
    confirmPowerLongPressActive = true;
    nextState |= (1 << BTN_POWER);
  }

  if (!pressed && confirmPowerPhysicalPressed) {
    confirmPowerPhysicalPressed = false;
    if (!confirmPowerLongPressActive) {
      if (!s_sharedConfirmPowerShortPressEmitsPower) {
        emitConfirmClick = true;
      }
      buttonPressStart = confirmPowerPressStart;
      buttonPressFinish = currentTime;
    }
    confirmPowerLongPressActive = false;
  }

  applyStateChange(nextState, currentTime);

  if (pressedEvents & (1 << BTN_POWER)) {
    powerButtonPressStart = confirmPowerPressStart;
  }

  if (emitConfirmClick) {
    pressedEvents |= (1 << BTN_CONFIRM);
    releasedEvents |= (1 << BTN_CONFIRM);
  }
}

void InputManager::updateDigitalTwoButton(const unsigned long currentTime) {
  const bool up = isDigitalPressed(BoardConfig::ACTIVE.input.up);
  const bool down = isDigitalPressed(BoardConfig::ACTIVE.input.down);
  const uint8_t physical = static_cast<uint8_t>((up ? 1u : 0u) | (down ? 2u : 0u));
  uint8_t auxiliaryState = serviceTouch();
  if (s_buttonHook) auxiliaryState |= s_buttonHook();
#if FREEINK_DEVICE_PAPERMONO
  // The power button reaches only the PM1 PMIC; clicks surface here as a
  // one-tick BTN_POWER pulse in the STATE, so applyStateChange() emits the
  // press this update and the release on the next. Never write the event
  // masks directly — applyStateChange() assigns them from the state diff,
  // clobbering direct writes the same tick.
  if (freeink::papermono::pollPowerButtonClicked(currentTime)) {
    auxiliaryState |= static_cast<uint8_t>(1u << BTN_POWER);
  }
#endif

  if (physical != twoButtonPhysicalState) {
    const uint8_t releasedPhysical = twoButtonPhysicalState;
    const bool emitShort = physical == 0 && !twoButtonLongPressActive;

    applyStateChange(auxiliaryState, currentTime);
    if (emitShort && (releasedPhysical == 1 || releasedPhysical == 2)) {
      const uint8_t logical = releasedPhysical == 1 ? BTN_UP : BTN_DOWN;
      pressedEvents |= static_cast<uint8_t>(1u << logical);
      releasedEvents |= static_cast<uint8_t>(1u << logical);
      buttonPressStart = twoButtonPressStart;
      buttonPressFinish = currentTime;
    }

    twoButtonPhysicalState = physical;
    twoButtonPressStart = currentTime;
    twoButtonLongPressActive = false;
    return;
  }

  if (physical == 0) {
    applyStateChange(auxiliaryState, currentTime);
    return;
  }

  uint8_t nextState = auxiliaryState;
  if (currentTime - twoButtonPressStart >= TWO_BUTTON_HOLD_MS) {
    twoButtonLongPressActive = true;
    const uint8_t logical = physical == 1 ? BTN_BACK : physical == 2 ? BTN_CONFIRM : BTN_POWER;
    nextState |= static_cast<uint8_t>(1u << logical);
  }
  applyStateChange(nextState, currentTime);
  if (pressedEvents & (1u << BTN_POWER)) powerButtonPressStart = twoButtonPressStart;
}

void InputManager::update() {
  const unsigned long currentTime = millis();

  pressedEvents = 0;
  releasedEvents = 0;
  touchPressedEvent = false;  // one-shot touch coord events, cleared each update()
  touchReleasedEvent = false;
  touchHomeKeyEvent = false;
  touchHomeKeyTapEvent = false;
  touchHomeKeyLongEvent = false;

  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalConfirmBackHold) {
    updateConfirmBackHold(currentTime);
    return;
  }
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalConfirmPowerHold) {
    updateConfirmPowerHold(currentTime);
    return;
  }
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalTwoButton) {
    updateDigitalTwoButton(currentTime);
    return;
  }

  const uint8_t state = getState();

  // Debounce
  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      applyStateChange(state, currentTime);
    }
  }
  if (touchHomeKeyTapEvent) {
    pressedEvents |= (1 << BTN_BACK);
    releasedEvents |= (1 << BTN_BACK);
  }
}

bool InputManager::isPressed(const uint8_t buttonIndex) const { return currentState & (1 << buttonIndex); }

bool InputManager::wasPressed(const uint8_t buttonIndex) const { return pressedEvents & (1 << buttonIndex); }

bool InputManager::wasAnyPressed() const { return pressedEvents > 0; }

bool InputManager::wasReleased(const uint8_t buttonIndex) const { return releasedEvents & (1 << buttonIndex); }

bool InputManager::wasAnyReleased() const { return releasedEvents > 0; }

unsigned long InputManager::getHeldTime() const {
  // Still hold a button
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }

  return buttonPressFinish - buttonPressStart;
}

unsigned long InputManager::getPowerButtonHeldTime() const {
  if (isPressed(BTN_POWER)) {
    return millis() - powerButtonPressStart;
  }

  return powerButtonPressFinish - powerButtonPressStart;
}

const char* InputManager::getButtonName(const uint8_t buttonIndex) {
  if (buttonIndex <= BTN_POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool InputManager::s_sharedConfirmPowerShortPressEmitsPower = false;

bool InputManager::isPowerButtonPressed() const { return isPressed(BTN_POWER); }

// ============================================================================
// Capacitive touch
//
// The public touch API is always available. Compiled only when
// FREEINK_CAP_TOUCH is set; the backend dispatches on
// BoardConfig::ACTIVE.touch.controller:
//   * CHSC6x (Murphy M3) — IRQ-driven, hand-rolled 16-byte frame decode.
//   * GT911  (LilyGo)    — polled status/point registers over I2C.
//   * FT5x06 (Paper Mono FT6336) — active-low IRQ + 0x02 point frame.
// Coordinates are delivered raw-panel-oriented; the app owns rotation.
// ============================================================================

bool InputManager::hasTouch() const {
#if FREEINK_CAP_TOUCH
  return touchDataEnabled;
#else
  return false;  // touch code not compiled in (FREEINK_CAP_TOUCH=0)
#endif
}

InputManager::TouchPoint InputManager::getTouchPoint() const { return touchPoint; }
bool InputManager::isTouchPressed() const { return touchPressed; }
bool InputManager::wasTouchPressed() const { return touchPressedEvent; }
bool InputManager::wasTouchReleased() const { return touchReleasedEvent; }

bool InputManager::wasTouchTap(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH
  if (!touchReleasedEvent) return false;
  if (touchMovedBeyondTapReleaseSlop) return false;
  // Tap position = the FIRST contact sample (touch-down), not the last: the
  // reported centroid drifts 10-20px as a finger rolls off during lift, which
  // made small targets (steppers) feel unreliable with release-point routing.
  // A tap routes to where the user touched, not where the finger let go.
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t w = (t.rawMaxX > t.rawMinX) ? static_cast<uint16_t>(t.rawMaxX - t.rawMinX) : 1;
  const uint16_t h = (t.rawMaxY > t.rawMinY) ? static_cast<uint16_t>(t.rawMaxY - t.rawMinY) : 1;
  float x = static_cast<float>(touchDownPoint.x) / w;
  float y = static_cast<float>(touchDownPoint.y) / h;
  nx = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  ny = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

bool InputManager::wasTouchPressedAt(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH
  // Press-edge analogue of wasTouchTap: true on the frame a touch begins,
  // writing the touch-down position normalized 0..1 in the panel's native
  // frame. Lets the app highlight what's under the finger on touch-down (before
  // release).
  if (!touchPressedEvent) return false;
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t w = (t.rawMaxX > t.rawMinX) ? static_cast<uint16_t>(t.rawMaxX - t.rawMinX) : 1;
  const uint16_t h = (t.rawMaxY > t.rawMinY) ? static_cast<uint16_t>(t.rawMaxY - t.rawMinY) : 1;
  float x = static_cast<float>(touchDownPoint.x) / w;
  float y = static_cast<float>(touchDownPoint.y) / h;
  nx = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  ny = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

bool InputManager::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
#if FREEINK_CAP_TOUCH
  if (!touchPressed || touchMovedBeyondTapSlop) return false;
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t w = (t.rawMaxX > t.rawMinX) ? static_cast<uint16_t>(t.rawMaxX - t.rawMinX) : 1;
  const uint16_t h = (t.rawMaxY > t.rawMinY) ? static_cast<uint16_t>(t.rawMaxY - t.rawMinY) : 1;
  float x = static_cast<float>(touchDownPoint.x) / w;
  float y = static_cast<float>(touchDownPoint.y) / h;
  nx = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  ny = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
  heldMs = millis() - touchDownPoint.timestamp;
  return true;
#else
  (void)nx;
  (void)ny;
  (void)heldMs;
  return false;
#endif
}

bool InputManager::isTouchHeldAt(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH
  // Live drag tracking: the latest contact sample (touchUpPoint is refreshed on
  // every sample while pressed), with no tap-slop gate.
  if (!touchPressed) return false;
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t w = (t.rawMaxX > t.rawMinX) ? static_cast<uint16_t>(t.rawMaxX - t.rawMinX) : 1;
  const uint16_t h = (t.rawMaxY > t.rawMinY) ? static_cast<uint16_t>(t.rawMaxY - t.rawMinY) : 1;
  float x = static_cast<float>(touchUpPoint.x) / w;
  float y = static_cast<float>(touchUpPoint.y) / h;
  nx = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  ny = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

unsigned long InputManager::lastTouchHeldMs() const {
#if FREEINK_CAP_TOUCH
  return lastTouchHeldDurationMs;
#else
  return 0;
#endif
}

bool InputManager::wasTouchActivity() const {
#if FREEINK_CAP_TOUCH
  return touchPressedEvent || touchReleasedEvent;
#else
  return false;
#endif
}

bool InputManager::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
#if FREEINK_CAP_TOUCH
  if (!touchReleasedEvent) return false;
  // A flick: travelled past a distance threshold within a time window. Distance
  // is measured in native px; the dominant axis is left to the app (after
  // mapping to its logical frame).
  if (lastTouchHeldDurationMs > TOUCH_SWIPE_MAX_MS) return false;
  const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
  const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
  const int adx = absInt(dx);
  const int ady = absInt(dy);
  if (adx < TOUCH_SWIPE_MIN_PX && ady < TOUCH_SWIPE_MIN_PX) return false;
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t w = (t.rawMaxX > t.rawMinX) ? static_cast<uint16_t>(t.rawMaxX - t.rawMinX) : 1;
  const uint16_t h = (t.rawMaxY > t.rawMinY) ? static_cast<uint16_t>(t.rawMaxY - t.rawMinY) : 1;
  auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
  nxStart = clamp01(static_cast<float>(touchDownPoint.x) / w);
  nyStart = clamp01(static_cast<float>(touchDownPoint.y) / h);
  nxEnd = clamp01(static_cast<float>(touchUpPoint.x) / w);
  nyEnd = clamp01(static_cast<float>(touchUpPoint.y) / h);
  return true;
#else
  (void)nxStart;
  (void)nyStart;
  (void)nxEnd;
  (void)nyEnd;
  return false;
#endif
}

bool InputManager::wasHomeKeyPressed() const { return touchHomeKeyEvent; }

bool InputManager::wasHomeKeyTapped() const { return touchHomeKeyTapEvent; }

bool InputManager::wasHomeKeyLongPressed() const { return touchHomeKeyLongEvent; }

void InputManager::prepareForDeepSleep() {
#if FREEINK_CAP_TOUCH
  const auto& t = BoardConfig::ACTIVE.touch;
  switch (t.controller) {
    case BoardConfig::TouchController::Ft6336:
#if FREEINK_DEVICE_MOFEI_M4
      if (ft6336Task) {
        vTaskDelete(ft6336Task);
        ft6336Task = nullptr;
      }
      if (ft6336Mutex) {
        vSemaphoreDelete(ft6336Mutex);
        ft6336Mutex = nullptr;
      }
      Wire.end();
      if (t.sda >= 0) pinMode(t.sda, INPUT);
      if (t.scl >= 0) pinMode(t.scl, INPUT);
      if (t.powerEnable >= 0) {
        pinMode(t.powerEnable, OUTPUT);
        digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? LOW : HIGH);
      }
      touchDataEnabled = false;
#endif
      return;
    case BoardConfig::TouchController::Gslx680:
#if FREEINK_DEVICE_EEGO_A4
      gslx680Write32(0xe0, 0x00000088);
      delay(5);
      Wire.end();
      if (t.sda >= 0) pinMode(t.sda, INPUT);
      if (t.scl >= 0) pinMode(t.scl, INPUT);
      if (t.reset >= 0) {
        const auto reset = static_cast<gpio_num_t>(t.reset);
        gpio_hold_dis(reset);
        pinMode(t.reset, OUTPUT);
        digitalWrite(t.reset, LOW);
        gpio_hold_en(reset);
      }
#endif
      return;
    case BoardConfig::TouchController::None:
    case BoardConfig::TouchController::Chsc6x:
    case BoardConfig::TouchController::Gt911:
    case BoardConfig::TouchController::Ft5x06:
      return;
  }
#endif
}

void InputManager::beginTouch() {
#if FREEINK_CAP_TOUCH
  const auto& t = BoardConfig::ACTIVE.touch;
  switch (t.controller) {
    case BoardConfig::TouchController::None:
      return;
    case BoardConfig::TouchController::Chsc6x:
      if (t.sda >= 0 && t.scl >= 0 && t.i2cAddress != 0) {
        Wire.begin(t.sda, t.scl, 100000);
        Wire.setTimeOut(4);
        touchDataEnabled = true;
      }
      return;
    case BoardConfig::TouchController::Gt911:
      beginGt911();
      return;
    case BoardConfig::TouchController::Ft5x06:
      beginFt5x06();
      return;
    case BoardConfig::TouchController::Gslx680:
#if FREEINK_DEVICE_EEGO_A4
      beginGslx680();
#endif
      return;
    case BoardConfig::TouchController::Ft6336:
#if FREEINK_DEVICE_MOFEI_M4
      beginFt6336();
#endif
      return;
  }
#endif
}

uint8_t InputManager::serviceTouch() {
#if FREEINK_CAP_TOUCH
  if (!touchDataEnabled) {
    return 0;
  }
  const unsigned long now = millis();
  const auto& t = BoardConfig::ACTIVE.touch;

  switch (t.controller) {
    case BoardConfig::TouchController::None:
      return 0;
    case BoardConfig::TouchController::Chsc6x:
      updateTouchFromIrq(now, 0);
      if (touchPressedEvent) touchIrqPulseUntil = now + TOUCH_IRQ_PULSE_MS;
      break;
    case BoardConfig::TouchController::Gt911:
      pollGt911(now);
      break;
    case BoardConfig::TouchController::Ft5x06:
      pollFt5x06(now);
      break;
    case BoardConfig::TouchController::Gslx680:
#if FREEINK_DEVICE_EEGO_A4
      pollGslx680(now);
#endif
      break;
    case BoardConfig::TouchController::Ft6336:
#if FREEINK_DEVICE_MOFEI_M4
      pollFt6336(now);
#endif
      break;
  }

  return (t.synthesizeConfirm && now < touchIrqPulseUntil) ? (1 << BTN_CONFIRM) : 0;
#else
  return 0;
#endif
}

#if FREEINK_CAP_TOUCH

void InputManager::updateTouchFromIrq(const unsigned long now, const int irqRaw) {
  // Poll the controller over I2C on a fixed cadence, independent of the IRQ.
  // The CHSC6x IRQ is a brief (~24ms) pulse at touch-down, not a level held for
  // the contact, so edge/level-gated reads missed quick taps. readChsc6xPoint
  // only returns true for a real touch (data[3] touch bit), so polling can't
  // latch the idle phantom frame. A valid read sets the press and refreshes the
  // release deadline; once reads stop coming, the touch releases after a short
  // hold-over.
  (void)irqRaw;
  if (now >= touchReadAt) {
    touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;
    TouchPoint point = {false, 0, 0, 0};
    if (readChsc6xPoint(point)) {
      updateTouchContact(point);
      touchReleaseAt = now + TOUCH_IRQ_PULSE_MS;
    }
  }

  if (touchPressed && now >= touchReleaseAt) {
    releaseTouch(now);
  }
}

bool InputManager::readChsc6xPoint(TouchPoint& point) {
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(TOUCH_READ_COMMAND);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  uint8_t data[TOUCH_FRAME_SIZE] = {};
  const uint8_t received = Wire.requestFrom(addr, TOUCH_FRAME_SIZE, static_cast<uint8_t>(true));
  if (received != TOUCH_FRAME_SIZE) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < TOUCH_FRAME_SIZE; ++i) {
    data[i] = Wire.read();
  }
  return decodeChsc6xFrame(data, TOUCH_FRAME_SIZE, point);
}

bool InputManager::decodeChsc6xFrame(const uint8_t* data, const size_t len, TouchPoint& point) const {
  if (len < 7) {
    return false;
  }
  // data[3] bit 7 is the touch-present flag: 0x80 while a finger is down, 0x00
  // when idle. The controller keeps returning a stale coordinate frame between
  // touches, so without this gate every read looks like a phantom touch (which
  // is why polling reported a fixed point and IRQ-gated reads were needed to
  // dodge it). Release transitions briefly show 0x40/0xff — both fail this test
  // or the coordinate sanity check below.
  if ((data[3] & 0x80) == 0) {
    return false;
  }
  const uint16_t rawX = data[4];                                          // X: one byte
  const uint16_t rawY = (static_cast<uint16_t>(data[5]) << 8) | data[6];  // Y: 16-bit big-endian
  if ((rawX == 0 && rawY == 0) || (rawX == 0xff && rawY == 0xffff)) {
    return false;
  }
  point = mapTouchPoint(rawX, rawY, millis());
  return true;
}

uint16_t InputManager::mapTouchAxis(uint16_t raw, const uint16_t rawMin, const uint16_t rawMax,
                                    const uint16_t outMax) const {
  if (raw <= rawMin) return 0;
  if (raw >= rawMax) return outMax;
  return static_cast<uint32_t>(raw - rawMin) * outMax / (rawMax - rawMin);
}

InputManager::TouchPoint InputManager::mapTouchPoint(const uint16_t rawX, const uint16_t rawY,
                                                     const unsigned long now) const {
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t sx = t.swapXY ? rawY : rawX;
  const uint16_t sy = t.swapXY ? rawX : rawY;
  const uint16_t width = t.rawMaxX > t.rawMinX ? t.rawMaxX - t.rawMinX : 1;
  const uint16_t height = t.rawMaxY > t.rawMinY ? t.rawMaxY - t.rawMinY : 1;
  uint16_t x = mapTouchAxis(sx, t.rawMinX, t.rawMaxX, width);
  uint16_t y = mapTouchAxis(sy, t.rawMinY, t.rawMaxY, height);
  if (t.flipX) x = static_cast<uint16_t>(width - x);
  if (t.flipY) y = static_cast<uint16_t>(height - y);
  return {true, x, y, now};
}

void InputManager::updateTouchContact(const TouchPoint& point) {
  touchPoint = point;
  if (!touchPressed) {
    touchPressedEvent = true;
    touchDownPoint = point;
    touchMovedBeyondTapSlop = false;
    touchMovedBeyondTapReleaseSlop = false;
  }
  touchUpPoint = point;
  const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
  const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
  if (movedBeyondSlop(dx, dy, TOUCH_TAP_SLOP_PX)) touchMovedBeyondTapSlop = true;
  if (movedBeyondSlop(dx, dy, TOUCH_TAP_RELEASE_SLOP_PX)) touchMovedBeyondTapReleaseSlop = true;
  touchPressed = true;
}

void InputManager::releaseTouch(const unsigned long now) {
  if (touchPressed) {
    touchReleasedEvent = true;
    lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
    touchUpPoint = touchPoint;
  }
  touchPressed = false;
  touchPoint.valid = false;
}

// --- FT5x06 / FT6336 (M5Stack Paper Mono) ----------------------------------

bool InputManager::ft5x06WriteReg(const uint8_t reg, const uint8_t value) {
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool InputManager::ft5x06ReadReg(const uint8_t reg, uint8_t* buf, const uint8_t len) {
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const uint8_t got = Wire.requestFrom(addr, len, static_cast<uint8_t>(true));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

void InputManager::beginFt5x06() {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.sda < 0 || t.scl < 0 || t.i2cAddress == 0) return;

#if FREEINK_DEVICE_PAPERMONO
  // The FT6336's power rail and reset line live on the M5IOE1 expander, not
  // ESP GPIOs — raise/release them before the probe below.
  freeink::papermono::enableTouch();
#endif

  // The bus is shared with M5PM1/M5IOE1/RX8130, whose standing profile is
  // 100 kHz. FT6336 accepts that rate even though M5GFX uses 400 kHz for its
  // controller-specific transactions.
  Wire.begin(t.sda, t.scl, 100000);
  Wire.setTimeOut(10);
  if (t.irq >= 0) pinMode(t.irq, INPUT_PULLUP);

  // Match M5GFX Touch_FT5x06::_check_init(): enter working mode, read the
  // chip/firmware/vendor window, then select polling/level interrupt mode.
  // Retried over ~600 ms: the FT6336 needs up to ~300 ms after a hardware
  // reset before its I2C interface answers, and on boards where the rail/reset
  // bring-up happens right here (Paper Mono: enableTouch() above) a one-shot
  // probe races the controller's boot and leaves touch dead for the session.
  // Gate on the transactions succeeding, NOT on the ID contents: Paper Mono
  // units ACK and serve the whole 0xA3..0xA8 window as zeros, so a vendor-byte
  // check reads as "absent" on a perfectly working controller.
  uint8_t id[6] = {};
  bool wrMode = false, rdId = false, wrIrq = false;
  for (int attempt = 0; attempt < 12 && !rdId; ++attempt) {
    if (attempt) delay(50);
    wrMode = ft5x06WriteReg(0x00, 0x00);
    rdId = wrMode && ft5x06ReadReg(0xA3, id, sizeof(id));
    wrIrq = rdId && ft5x06WriteReg(0xA4, 0x00);
  }
  touchDataEnabled = wrMode && rdId && wrIrq;
#ifdef TOUCH_PROBE_DEBUG
#if FREEINK_DEVICE_PAPERMONO
  // Expander state alongside the probe result: OUT should show TP_EN (bit 12)
  // and TP_RST (bit 5) high, MODE should show the configured output mask
  // (0x39B4). All-zero probe ids + correct expander state = the FT6336 itself
  // isn't answering; wrong expander state = the rail/reset never asserted.
  uint16_t ioeMode = 0xFFFF, ioeOut = 0xFFFF;
  freeink::m5ioe1::readReg16(freeink::m5ioe1::REG_GPIO_MODE_L, &ioeMode);
  freeink::m5ioe1::readReg16(freeink::m5ioe1::REG_GPIO_OUT_L, &ioeOut);
  touchDebugPrintf("[touch] IOE1 addr=0x%02X mode=0x%04X out=0x%04X\n", freeink::m5ioe1::g_addr, ioeMode, ioeOut);
  // Full bus scan with the touch rail up: expected residents are 0x32 (RX8130
  // RTC), 0x4F/0x6F (IOE1), 0x68 (BMI270), 0x6E (PM1), 0x50 (NFC on Pro) —
  // whatever ELSE ACKs is the touch controller (FT6336 = 0x38; some unit
  // revisions may carry a CST820 = 0x15 instead).
  touchDebugPrintf("[touch] i2c scan:");
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) touchDebugPrintf(" 0x%02X", a);
    delayMicroseconds(200);
  }
  touchDebugPrintf("\n");
#endif
  touchDebugPrintf(
      "[touch] FT5x06 probe: enabled=%d wrMode=%d rdId=%d wrIrq=%d "
      "cipher=0x%02X fw=0x%02X vendor=0x%02X irq=%d\n",
      touchDataEnabled, wrMode, rdId, wrIrq, id[0], id[3], id[5], t.irq);
#endif
}

void InputManager::pollFt5x06(const unsigned long now) {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (now < touchReadAt) return;
  touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;

  // The controller runs in interrupt-polling mode (G_MODE=0, set in begin),
  // where INT emits low PULSES at the report rate while a contact is held —
  // the line reads HIGH between pulses even with the finger down, so its
  // level must not be treated as a release (that splits one swipe into a
  // phantom tap plus a swipe). Idle fast-path gate only; while a contact is
  // live, the TD_STATUS zero-contact frame below is the release authority.
  const bool irqDown = t.irq < 0 || digitalRead(t.irq) == LOW;
  if (!irqDown && !touchPressed) {
    return;
  }

  // Register 0x02 is TD_STATUS followed by the first point's XH/XL/YH/YL.
  // One contact is enough for the app's tap/swipe/drag gesture model.
  uint8_t data[5] = {};
  if (!ft5x06ReadReg(0x02, data, sizeof(data))) {
    // Transient read failures happen on the shared PY32 bus; survive them.
    // But a controller that has stopped answering (rail glitch) must not
    // leave the contact latched — release once samples go stale.
    constexpr unsigned long STALE_RELEASE_MS = 100;
    if (touchPressed && now - touchPoint.timestamp > STALE_RELEASE_MS) releaseTouch(now);
    return;
  }
  if ((data[0] & 0x0F) == 0) {
    // FT6336 may keep INT low until TD_STATUS has been drained. Treat the
    // controller's zero-contact frame as authoritative; waiting only for the
    // GPIO to rise leaves touchPressed latched and drops every later tap.
    if (touchPressed) {
      releaseTouch(now);
#ifdef TOUCH_PROBE_DEBUG
      touchDebugPrintf("[touch] FT release via TD_STATUS=0 held=%lums\n", lastTouchHeldDurationMs);
#endif
    }
    return;
  }
  const uint16_t rawX = static_cast<uint16_t>((data[1] & 0x0F) << 8) | data[2];
  const uint16_t rawY = static_cast<uint16_t>((data[3] & 0x0F) << 8) | data[4];
  const bool firstContact = !touchPressed;
  updateTouchContact(mapTouchPoint(rawX, rawY, now));
#ifdef TOUCH_PROBE_DEBUG
  if (firstContact) {
    touchDebugPrintf("[touch] FT press raw=(%u,%u) panel=(%u,%u)\n", rawX, rawY, touchPoint.x, touchPoint.y);
  }
#endif
}

// --- FT6336U (Mofei M4) -----------------------------------------------------

#if FREEINK_DEVICE_MOFEI_M4
bool InputManager::ft6336ReadReg(const uint8_t reg, uint8_t* data, const uint8_t len) {
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, len, static_cast<uint8_t>(true)) != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
  return true;
}

void InputManager::beginFt6336() {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.sda < 0 || t.scl < 0 || t.i2cAddress == 0 || t.powerEnable < 0) return;

  gpio_hold_dis(static_cast<gpio_num_t>(t.powerEnable));
  pinMode(t.powerEnable, OUTPUT);
  digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? LOW : HIGH);
  delay(100);
  digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? HIGH : LOW);
  delay(350);
  if (t.irq >= 0) pinMode(t.irq, INPUT_PULLUP);

  Wire.begin(t.sda, t.scl, 400000);
  Wire.setTimeOut(50);
  uint8_t firmware = 0;
  uint8_t vendor = 0;
  uint8_t library = 0;
  uint8_t status = 0;
  const bool detected = ft6336ReadReg(FT6336_REG_FIRMWARE_ID, &firmware, 1) &&
                        ft6336ReadReg(FT6336_REG_VENDOR_ID, &vendor, 1) &&
                        ft6336ReadReg(FT6336_REG_LIB_VERSION, &library, 1) &&
                        ft6336ReadReg(FT6336_REG_TD_STATUS, &status, 1) && status < 0x10;
  if (!detected) {
    Wire.end();
    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? LOW : HIGH);
    return;
  }

  ft6336Mutex = xSemaphoreCreateMutex();
  if (!ft6336Mutex) {
    Wire.end();
    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? LOW : HIGH);
    return;
  }
  // The fixed task stack is internal DRAM by FreeRTOS design. No heap work is
  // performed per sample; the task publishes one fixed-size state snapshot.
  const BaseType_t taskCreated =
      xTaskCreatePinnedToCore(ft6336TaskTrampoline, "fi_ft6336", 2048, this, 5, &ft6336Task, 0);
  if (taskCreated != pdPASS) {
    vSemaphoreDelete(ft6336Mutex);
    ft6336Mutex = nullptr;
    Wire.end();
    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? LOW : HIGH);
    return;
  }
  touchDataEnabled = true;
#ifdef TOUCH_PROBE_DEBUG
  touchDebugPrintf("[touch] FT6336 probe: addr=0x%02X fw=0x%02X vendor=0x%02X lib=0x%02X enabled=1\n", t.i2cAddress,
                   firmware, vendor, library);
#endif
}

void InputManager::ft6336TaskTrampoline(void* self) { static_cast<InputManager*>(self)->ft6336TaskLoop(); }

void InputManager::ft6336TaskLoop() {
  bool previousContact = false;
  uint16_t downRawX = 0;
  uint16_t downRawY = 0;
  uint16_t lastRawX = 0;
  uint16_t lastRawY = 0;

  for (;;) {
    uint8_t status = 0;
    if (!ft6336ReadReg(FT6336_REG_TD_STATUS, &status, 1)) {
      vTaskDelay(pdMS_TO_TICKS(FT6336_POLL_MS));
      continue;
    }

    const bool contact = (status & 0x0F) != 0;
    uint16_t rawX = lastRawX;
    uint16_t rawY = lastRawY;
    if (contact) {
      uint8_t point[4] = {};
      if (!ft6336ReadReg(FT6336_REG_TOUCH1, point, sizeof(point))) {
        vTaskDelay(pdMS_TO_TICKS(FT6336_POLL_MS));
        continue;
      }
      rawX = ft6336Axis(point[0], point[1]);
      rawY = ft6336Axis(point[2], point[3]);
      lastRawX = rawX;
      lastRawY = rawY;
      if (!previousContact) {
        downRawX = rawX;
        downRawY = rawY;
      }
    }

    if (xSemaphoreTake(ft6336Mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
      ft6336State.contact = contact;
      if (contact) {
        ft6336State.rawX = rawX;
        ft6336State.rawY = rawY;
        if (!previousContact) {
          ft6336State.downRawX = downRawX;
          ft6336State.downRawY = downRawY;
          ft6336State.pressLatched = true;
        }
      } else if (previousContact) {
        ft6336State.releaseRawX = lastRawX;
        ft6336State.releaseRawY = lastRawY;
        ft6336State.releaseLatched = true;
      }
      xSemaphoreGive(ft6336Mutex);
    }
    previousContact = contact;
    vTaskDelay(pdMS_TO_TICKS(FT6336_POLL_MS));
  }
}

void InputManager::pollFt6336(const unsigned long now) {
  if (!ft6336Mutex) return;
  Ft6336TaskState snapshot;
  if (xSemaphoreTake(ft6336Mutex, pdMS_TO_TICKS(1)) != pdTRUE) return;
  snapshot = ft6336State;
  ft6336State.pressLatched = false;
  ft6336State.releaseLatched = false;
  xSemaphoreGive(ft6336Mutex);

  if (snapshot.pressLatched) {
    updateTouchContact(mapTouchPoint(snapshot.downRawX, snapshot.downRawY, now));
  }
  if (snapshot.contact) {
    updateTouchContact(mapTouchPoint(snapshot.rawX, snapshot.rawY, now));
  }
  if (snapshot.releaseLatched) {
    touchPoint = mapTouchPoint(snapshot.releaseRawX, snapshot.releaseRawY, now);
    releaseTouch(now);
  }
}
#endif

// --- GSLX680 (EEGO A4) ------------------------------------------------------

#if FREEINK_DEVICE_EEGO_A4
bool InputManager::gslx680Write(const uint8_t reg, const uint8_t* data, const uint8_t len) {
  Wire.beginTransmission(BoardConfig::ACTIVE.touch.i2cAddress);
  Wire.write(reg);
  if (len && data) Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool InputManager::gslx680Write32(const uint8_t reg, const uint32_t value) {
  const uint8_t data[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                          static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  return gslx680Write(reg, data, sizeof(data));
}

bool InputManager::gslx680Read(const uint8_t reg, uint8_t* data, const uint8_t len) {
  Wire.beginTransmission(BoardConfig::ACTIVE.touch.i2cAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BoardConfig::ACTIVE.touch.i2cAddress, len, static_cast<uint8_t>(true)) != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
  return true;
}

void InputManager::gslx680ClearRegisters() {
  gslx680Write32(0xe0, 0x00000088);
  delay(20);
  gslx680Write32(0x80, 0x00000003);
  delay(5);
  gslx680Write32(0xe4, 0x00000004);
  delay(5);
  gslx680Write32(0xe0, 0);
  delay(20);
}

void InputManager::gslx680ResetChip() {
  gslx680Write32(0xe0, 0x00000088);
  delay(20);
  gslx680Write32(0xe4, 0x00000004);
  delay(10);
  gslx680Write32(0xbc, 0);
  delay(10);
}

void InputManager::gslx680StartChip() {
  gslx680Write32(0xe0, 0);
  delay(10);
}

bool InputManager::gslx680LoadFirmware() {
  size_t i = 0;
  while (i < freeink::EEGO_A4_GSL_FIRMWARE_COUNT) {
    freeink::Gslx680FirmwareEntry entry;
    memcpy_P(&entry, &freeink::EEGO_A4_GSL_FIRMWARE[i], sizeof(entry));
    if (entry.offset == 0xf0) {
      if (!gslx680Write32(entry.offset, entry.value)) return false;
      ++i;
      continue;
    }
    uint8_t payload[64];
    uint8_t words = 0;
    const uint8_t firstReg = entry.offset;
    while (i < freeink::EEGO_A4_GSL_FIRMWARE_COUNT && words < 16) {
      memcpy_P(&entry, &freeink::EEGO_A4_GSL_FIRMWARE[i], sizeof(entry));
      if (entry.offset == 0xf0 || entry.offset != static_cast<uint8_t>(firstReg + words * 4)) {
        break;
      }
      memcpy(payload + words * 4, &entry.value, sizeof(entry.value));
      ++words;
      ++i;
    }
    if (!words || !gslx680Write(firstReg, payload, words * 4)) return false;
  }
  return true;
}

bool InputManager::gslx680Check() {
  uint8_t status[4] = {};
  return gslx680Read(0xb0, status, sizeof(status)) && status[0] == 0x5a && status[1] == 0x5a && status[2] == 0x5a &&
         status[3] == 0x5a;
}

void InputManager::beginGslx680() {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.reset >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(t.reset));
    pinMode(t.reset, OUTPUT);
    digitalWrite(t.reset, HIGH);
    delay(20);
  }
  Wire.begin(t.sda, t.scl, 400000);
  Wire.setTimeOut(256);
  uint8_t probe = 0;
  if (!gslx680Read(0xf0, &probe, 1) || !gslx680Write32(0xf0, 0x00000012) || !gslx680Read(0xf0, &probe, 1)) {
    touchDataEnabled = false;
    return;
  }
  gslx680ClearRegisters();
  gslx680ResetChip();
  gslx680ResetChip();
  gslx680ClearRegisters();
  gslx680ResetChip();
  if (!gslx680LoadFirmware()) {
    touchDataEnabled = false;
    return;
  }
  gslx680StartChip();
  gslx680ResetChip();
  gslx680StartChip();
  if (!gslx680Check()) {
    gslx680ResetChip();
    gslx680StartChip();
  }
  touchDataEnabled = gslx680Check();
}

void InputManager::pollGslx680(const unsigned long now) {
  if (now < touchReadAt) return;
  touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;
  uint8_t frame[24] = {};
  if (!gslx680Read(0x80, frame, sizeof(frame))) return;

  const auto finishHomeKey = [this, now]() {
    if (!touchHomeKeyDown) return;
    lastTouchHeldDurationMs = now - touchHomeKeyDownAt;
    if (!touchHomeKeyLongFired) touchHomeKeyTapEvent = true;
    touchHomeKeyDown = false;
    touchHomeKeyLongFired = false;
  };
  const uint8_t count = frame[0] > 5 ? 5 : frame[0];
  if (!count) {
    finishHomeKey();
    releaseTouch(now);
    return;
  }

  const uint16_t rawYWord = static_cast<uint16_t>(frame[5]) << 8 | frame[4];
  const uint16_t rawXWord = static_cast<uint16_t>(frame[7]) << 8 | frame[6];
  const bool homeKeyDown = count == 1 && rawXWord == 0x03a0 && rawYWord == 0x1020;
  if (homeKeyDown) {
    if (!touchHomeKeyDown) {
      touchHomeKeyEvent = true;
      touchHomeKeyDown = true;
      touchHomeKeyLongFired = false;
      touchHomeKeyDownAt = now;
    } else if (!touchHomeKeyLongFired && now - touchHomeKeyDownAt >= HOME_KEY_LONG_PRESS_MS) {
      touchHomeKeyLongEvent = true;
      touchHomeKeyLongFired = true;
    }
    touchPressed = false;
    touchPoint.valid = false;
    return;
  }
  finishHomeKey();

  const uint16_t rawY = rawYWord & 0x0fff;
  const uint16_t rawX = rawXWord & 0x0fff;

  // EEGO A4 (GSLX680) coordinate calibration. The linear raw-range mapping is
  // wrong for this panel: the GSL reports a portrait digitizer whose raw axes
  // run 0..~920 x 0..~680, while the UC8279C framebuffer is landscape 768x552.
  // Recovered from the official 1.2.7 GSL firmware's FUN_4204c510 (CrossLink
  // 1.0.10's narrower transform only covered ~536 of the 552 short-axis rows,
  // making edge/front-key hits inaccurate):
  //   portraitX = min(rawY, 680) * 551 / 680
  //   portraitY = (920 - min(rawX, 920)) * 767 / 920
  // The digitizer is portrait while the framebuffer is landscape, so swap the
  // calibrated axes when returning panel-native coordinates.
  const uint16_t limitedY = rawY > 680 ? 680 : rawY;
  const uint16_t limitedX = rawX > 920 ? 920 : rawX;
  const uint16_t portraitX = static_cast<uint32_t>(limitedY) * 551 / 680;
  const uint16_t portraitY = static_cast<uint32_t>(920 - limitedX) * 767 / 920;

  // GfxRenderer's Portrait transform rotates panel-native coordinates clockwise
  // and therefore computes logical X as (551 - nativeY). The digitizer's raw-Y
  // axis grows in logical left-to-right order, so nativeY must be mirrored here.
  // Without this final reflection every horizontal touch target is reversed.
  const TouchPoint point = {true, portraitY, static_cast<uint16_t>(551 - portraitX), now};
  updateTouchContact(point);
}
#endif

// --- GT911 (LilyGo) ---------------------------------------------------------

void InputManager::beginGt911() {
  const auto& t = BoardConfig::ACTIVE.touch;

  // Power the touch rail first (boards that gate it, e.g. Sticky's TOUCH_EN on
  // GPIO42). Active-high + settle, before the reset dance and I2C probe;
  // without this the GT911 never ACKs and touch is reported absent. No-op when
  // unassigned. gpio_hold_dis first: the sleep path holds this pin LOW and the
  // hold survives the deep-sleep wake reset; the HIGH write is a no-op until it
  // is released.
  if (t.powerEnable >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(t.powerEnable));
    pinMode(t.powerEnable, OUTPUT);
    // ON level: HIGH for active-high enables (Sticky), LOW for active-low (X4
    // Pro GPIO2).
    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? HIGH : LOW);
    delay(50);
  }

  if (t.sda >= 0 && t.scl >= 0) {
    Wire.begin(t.sda, t.scl, 400000);
    Wire.setTimeOut(10);
  }

  auto resetWithIntLevel = [&](const uint8_t level) {
    if (t.reset < 0 || t.irq < 0) return;
    pinMode(t.irq, OUTPUT);
    pinMode(t.reset, OUTPUT);
    digitalWrite(t.reset, LOW);
    digitalWrite(t.irq, level);
    delay(10);
    digitalWrite(t.reset, HIGH);
    delay(10);
    digitalWrite(t.irq, level);
    delay(50);
    pinMode(t.irq, INPUT);
    delay(50);
  };

  auto probeCandidates = [&]() {
    const uint8_t candidates[2] = {t.i2cAddress, t.i2cAddressAlt};
    for (uint8_t a : candidates) {
      if (a == 0) continue;
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        gt911Addr = a;
        return true;
      }
    }
    return false;
  };

  // Reset + address-select dance: INT level as RST rises selects the address.
  // Boards differ in which strapped address survives their module wiring, so
  // try the primary-select level first, then the alternate level before
  // declaring the touch controller absent.
  gt911Addr = 0;
  resetWithIntLevel(LOW);
  if (!probeCandidates()) {
    resetWithIntLevel(HIGH);
    probeCandidates();
  }

  touchDataEnabled = (gt911Addr != 0);
#ifdef TOUCH_PROBE_DEBUG
  touchDebugPrintf(
      "[touch] GT911 probe: addr=0x%02X enabled=%d (sda=%d scl=%d "
      "cand=0x%02X/0x%02X)\n",
      gt911Addr, touchDataEnabled, t.sda, t.scl, t.i2cAddress, t.i2cAddressAlt);
#endif
}

bool InputManager::gt911ReadReg(const uint16_t reg, uint8_t* buf, const uint8_t len) {
  Wire.beginTransmission(gt911Addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t got = Wire.requestFrom(gt911Addr, len, static_cast<uint8_t>(true));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

void InputManager::gt911ClearStatus() {
  Wire.beginTransmission(gt911Addr);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(static_cast<uint8_t>(0x00));
  Wire.endTransmission();
}

void InputManager::pollGt911(const unsigned long now) {
  if (gt911Addr == 0) {
    return;
  }
  uint8_t status = 0;
  if (!gt911ReadReg(0x814E, &status, 1)) {
    return;
  }

  // Capacitive home key long-press (status bit 0x10). Fire from the LATCHED
  // down-state + wall clock, BEFORE the buffer-ready gate below: a motionless
  // hold stops producing new-data frames (0x80 stays clear), so gating the hold
  // timer on fresh frames would never let it cross the threshold. The
  // press/release EDGES still come from fresh frames (handled after the gate).
  if (touchHomeKeyDown && !touchHomeKeyLongFired && now - touchHomeKeyDownAt >= HOME_KEY_LONG_PRESS_MS) {
    touchHomeKeyLongEvent = true;  // crossed the threshold (a hold shortcut)
    touchHomeKeyLongFired = true;  // once per hold; also suppresses the release tap
  }

  if (!(status & 0x80)) {  // buffer not ready
    return;
  }

  // Home-key press/release edges (need a fresh frame). Short tap = primary
  // "home" action, fires on release; the long hold above suppresses it.
  const bool homeKeyDown = (status & 0x10) != 0;
  if (homeKeyDown && !touchHomeKeyDown) {  // press edge
    touchHomeKeyEvent = true;
    touchHomeKeyDownAt = now;
    touchHomeKeyLongFired = false;
  } else if (!homeKeyDown && touchHomeKeyDown && !touchHomeKeyLongFired) {
    touchHomeKeyTapEvent = true;  // release edge of a short press
  }
  touchHomeKeyDown = homeKeyDown;

  const uint8_t count = status & 0x0F;
  if (count > 0) {
    uint8_t pt[8] = {};
    if (gt911ReadReg(0x8150, pt, 8)) {
      // Coordinate bytes start at 0 (no track-id, e.g. M5Paper) or 1 (datasheet
      // standard, e.g. LilyGo) depending on the board's GT911 config.
      const uint8_t o = BoardConfig::ACTIVE.touch.gt911CoordsAtByte0 ? 0 : 1;
      const uint16_t rawX = static_cast<uint16_t>(pt[o]) | (static_cast<uint16_t>(pt[o + 1]) << 8);
      const uint16_t rawY = static_cast<uint16_t>(pt[o + 2]) | (static_cast<uint16_t>(pt[o + 3]) << 8);
      const bool firstContact = !touchPressed;
      updateTouchContact(mapTouchPoint(rawX, rawY, now));
#ifdef TOUCH_PROBE_DEBUG
      if (firstContact)
        touchDebugPrintf(
            "[touch] press pt=[%02X %02X %02X %02X %02X %02X %02X "
            "%02X] raw=(%u,%u) mapped=(%u,%u)\n",
            pt[0], pt[1], pt[2], pt[3], pt[4], pt[5], pt[6], pt[7], rawX, rawY, touchPoint.x, touchPoint.y);
#endif
    }
  } else {
    releaseTouch(now);
  }

  gt911ClearStatus();  // GT911 requires clearing 0x814E after each read
}

#endif  // FREEINK_CAP_TOUCH
