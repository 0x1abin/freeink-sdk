#pragma once

#include <cstddef>
#include <cstdint>

namespace freeink {
// The vendor board/display must already own and initialize I2C1/TCA/rails.
// No bus creation, framebuffer initialization or reset on read failures here.
// BQ27220: read-only 0x08 voltage, 0x0c signed-current bits, 0x2c SOC.
// One-second native snapshot; false leaves value unchanged. No gauge/charger writes.
bool metalioPlusReadGauge16(uint8_t reg, uint16_t& value);
bool metalioPlusTouchBegin();
bool metalioPlusTouchRead(uint8_t* frame, size_t size);
// Provisional active-low TCA P10/P11/P17 -> InputManager Up/Down/Power.
// Failed port reads return false and zero; the caller cancels rather than
// turning an I/O fault into a normal button release. Polarity needs acceptance.
bool metalioPlusReadButtons(uint8_t& mask);
}  // namespace freeink
