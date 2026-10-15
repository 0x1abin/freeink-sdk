#pragma once
#include <Arduino.h>
#include <cassert>
using gpio_num_t = int;
inline bool gpioHeld[49] = {};
inline void gpio_hold_dis(int pin) {
  if (pin == 44)
    assert(gpioLevels[pin] == LOW);
  gpioHeld[pin] = false;
}
inline void gpio_hold_en(int pin) { gpioHeld[pin] = true; }
