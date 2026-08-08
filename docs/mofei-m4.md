# Mofei M4 board profile

Build with `FREEINK_DEVICE_MOFEI_M4=1` on an ESP32-S3 N16R8 target using DIO
flash and OPI PSRAM. The profile provides:

- SSD1677 800x480 on GPIO4/3/5/6/7/8, 20 MHz SPI;
- FT6336U at `0x2E` on SDA13/SCL12, active-low GPIO45 power, 10 ms polling;
- 4-bit SDMMC on CLK16/CMD15/D0=17/D1=18/D2=11/D3=14, active-low GPIO10 power;
- RX8010SJ at `0x32`, ADC9 battery with 2.0 divider, and active-low GPIO43 charging;
- cool GPIO47 and warm GPIO48 frontlight PWM.

Batch 2 with R13 is the default display configuration. Define
`FREEINK_MOFEI_M4_BATCH1=1` for the first no-R13 batch. AHT20 and SC7A20 are
deliberately not part of the initial reader profile.
