#pragma once

#include <cstddef>
#include <cstdint>

namespace freeink {
// The vendor board/display must already own and initialize I2C1/TCA/rails.
// No bus creation, framebuffer initialization or reset on read failures here.
// BQ27220: read-only 0x08 voltage, 0x0c signed-current bits, 0x2c SOC.
// One-second native snapshot; false leaves value unchanged. No gauge/charger writes.
bool metalioPlusReadGauge16(uint8_t reg, uint16_t& value);
// PCF8563 uses the existing native owner. Begin is read-only; explicit time
// writes touch control0/STOP and seven calendar bytes, preserving alarm/CLKOUT.
bool metalioPlusRtcRead(uint8_t reg, uint8_t* data, size_t size);
bool metalioPlusRtcWriteTime(const uint8_t* bcd);
// Poll-only SC7A20H on native I2C1. Never changes IRQ/reset/FIFO routing.
bool metalioPlusImuBegin();
bool metalioPlusImuWake();
bool metalioPlusImuSleep();
// Three signed, left-aligned high-resolution axes; failure leaves them unchanged.
bool metalioPlusImuRead(int16_t* axes);
bool metalioPlusTouchBegin();
bool metalioPlusTouchRead(uint8_t* frame, size_t size);
// Provisional active-low TCA P10/P11/P17 -> InputManager Up/Down/Power.
// Failed port reads return false and zero; the caller cancels rather than
// turning an I/O fault into a normal button release. Polarity needs acceptance.
bool metalioPlusReadButtons(uint8_t& mask);
}  // namespace freeink
