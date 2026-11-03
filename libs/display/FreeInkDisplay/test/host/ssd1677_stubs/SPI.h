#pragma once
#include <cstdint>
struct FakeNoBusSpi { void* bus() { return nullptr; } int8_t pinSS() { return -1; } void end() {} void begin(int8_t,int8_t,int8_t,int8_t) {} };
inline FakeNoBusSpi SPI;
