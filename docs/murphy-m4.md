# Murphy M4 board support

Build with `FREEINK_DEVICE_MURPHY_M4=1` on an ESP32-S3 N16R8 target using DIO
flash and OPI PSRAM. The profile provides:

- SSD1677 800×480 on GPIO4/3/5/6/7/8 at 20 MHz;
- GPIO1/2 navigation plus shared GPIO0 confirm/power (short confirm, hold power);
- FT6336U at `0x2E` on native I²C1 SDA13/SCL12 at 100 kHz, active-low GPIO44
  IRQ, active-low GPIO45 power, and GPIO7 reset shared with the display;
- 4-bit SDMMC on CLK16/CMD15/D0=17/D1=18/D2=11/D3=14 with active-low GPIO10
  power;
- RX8010SJ at `0x32` on the same native I²C1 bus with a 400 kHz device handle,
  ADC9 battery sensing with a 2.0 divider, and active-low GPIO43 charging status;
- cool GPIO47 and warm GPIO48 frontlight PWM at 25 kHz / 10-bit using the
  official gamma-1.6554 percentage curve.

The native I²C1 bus and its two device handles are allocated once and retained
until reset. Display initialization toggles the shared GPIO7 reset, so firmware
must call `InputManager::reinitializeTouchAfterSharedReset()` after
`FreeInkDisplay::begin()` to restore the FT6336U's volatile mode, threshold, and
report-rate registers.

The consumer detects the two production batches from the GPIO1/KEY1 RC network
before `InputManager::begin()`, then calls `InputManager::setMurphyM4Batch()`
and `FreeInkDisplay::setMurphyM4Batch()` before display initialization. The
display facade retains the selection and constructs the SSD1677 singleton from
one of two immutable configurations; it does not mutate shared driver config.
Batch 1 (no R13) uses HALF/window
pseudo-temperature `0x3C` and touch short-axis range `[-52,553]`; batch 2 (R13
fitted) uses `0x50` and `[-47,514]`. Batch 2 remains the inconclusive-probe
fallback. Define `FREEINK_MURPHY_M4_BATCH1=1` only for recovery or diagnostics.
First-batch hardware produced a 6008 µs median across 101 samples
(6004–6059 µs, 0.379% coefficient of variation), satisfying the batch-1 and
stability thresholds. Runtime detection uses seven `uint32_t` samples (28 bytes)
on the caller's stack and adds no heap allocation. Second-batch hardware
validation remains pending.
AHT20 and SC7A20 are not part of the initial reader profile.
