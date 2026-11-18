#pragma once

namespace freeink {
// Boot phase, before SD and display/input start. Reuses native I2C1, restores
// P14 then P5 with native settle timing, preserving all unrelated TCA bits.
bool metalioPlusRestoreBootRails();

// Display/native I2C must be ready and storage flushed/unmounted. Prepares the
// board and checked EXT1 GPIO2 wake; caller then enters deep sleep directly.
// False requires a reboot, because display/SD pins may already be parked.
bool metalioPlusPrepareDeepSleep();
}  // namespace freeink
