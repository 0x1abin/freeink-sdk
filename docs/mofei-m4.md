# Mofei M4 board support

Build with `FREEINK_DEVICE_MOFEI_M4=1` on an ESP32-S3 N16R8 target using DIO
flash and OPI PSRAM. The profile provides:

- SSD1677 800×480 on GPIO4/3/5/6/7/8 at 20 MHz;
- FT6336U at `0x2E` on SDA13/SCL12, active-low GPIO45 power, and a fixed 10 ms
  polling task because the interrupt line cannot provide usable edges;
- 4-bit SDMMC on CLK16/CMD15/D0=17/D1=18/D2=11/D3=14 with active-low GPIO10
  power;
- RX8010SJ at `0x32`, ADC9 battery sensing with a 2.0 divider, and active-low
  GPIO43 charging status;
- cool GPIO47 and warm GPIO48 frontlight PWM.

Batch 2 with R13 is the default display configuration. Define
`FREEINK_MOFEI_M4_BATCH1=1` for the first no-R13 batch. AHT20 and SC7A20 are
not part of the initial reader profile.
