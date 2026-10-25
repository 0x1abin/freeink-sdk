#pragma once
#include <cstdint>
#include <cstring>
#define DRAM_ATTR
#define PROGMEM
inline uint8_t pgm_read_byte(const unsigned char* p) { return *p; }
inline uint32_t ticks = 0;
inline uint32_t millis() { return ticks; }
inline void delay(uint32_t n) { ticks += n; }
inline void esp_rom_printf(const char*, ...) {}

#define LOW 0
#define HIGH 1
#define INPUT 1
#define OUTPUT 3
#define INPUT_PULLUP 5
using gpio_num_t = int;
inline void gpio_hold_dis(gpio_num_t) {}
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
inline void delayMicroseconds(unsigned) {}
inline int digitalRead(int) { return 0; }
struct FakeSerial { template<class... T> void printf(const char*, T...) {} }; inline FakeSerial Serial;
