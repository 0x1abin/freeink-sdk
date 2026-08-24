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

Batch 2 with R13 is the default display configuration. Define
`FREEINK_MURPHY_M4_BATCH1=1` for the first no-R13 batch. AHT20 and SC7A20 are
not part of the initial reader profile.
