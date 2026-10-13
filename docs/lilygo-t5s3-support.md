# LilyGo T5 S3 (4.7" ED047TC1)

The LilyGo T5S3-4.7-ePaper (PRO/Lite) is an ESP32-S3 board. Its panel is an
**ED047TC1: a raw 960×540 16-gray parallel EPD with no on-glass controller**. The
MCU clocks every row over the S3 LCD (i80) peripheral and an external PMIC
generates the waveform rails — a different display class from FreeInk's SPI
single-chip drivers (SSD1677/UC8253/ED2208).

Reference port: [ShallowGreen123/t5s3-reader](https://github.com/ShallowGreen123/t5s3-reader),
whose `HalDisplay` delegates to the SDK's `EInkDisplay`/`LgfxEpdDriver`.

## Display

`LgfxEpdDriver` wraps **LovyanGFX's `Panel_EPD`/`Bus_EPD`** (bundled in
`m5stack/M5GFX`) and reports `usesExternalBus() == true`. It compiles under
`FREEINK_DRIVER_LGFX_EPD` (derived from `FREEINK_DEVICE_LILYGO` or
`FREEINK_DEVICE_PAPERS3`). Add M5GFX only to builds using that driver. The driver
holds an 8-bit grayscale `LGFX_Sprite` canvas in PSRAM:

- `display()` expands the 1-bpp framebuffer into the canvas and `pushSprite`s it
  at the requested `epd_mode` (FULL/HALF→text, FAST→fast).
- The grayscale path (`displayGray` + `copyGrayscaleLsb/Msb` +
  `writeGrayscalePlaneStrip`) combines the B/W base with the LSB/MSB planes into
  four gray levels in the canvas, then pushes with the same mode as the B/W base.
  Keeping that mode avoids re-driving unchanged pixels during the AA overlay.

At 960×540 the canvas occupies 518,400 bytes, and the two 1-bpp selector planes
occupy 64,800 bytes each: 648,000 bytes of PSRAM allocated once and reused.
These figures exclude M5GFX's internal panel/DMA buffers and the SDK's 64,800-byte
single B/W framebuffer. Stack storage is unsuitable for these full-screen
buffers. The driver keeps its allocation-failure checks; measure both internal
RAM and PSRAM free space and largest blocks on hardware.

`BoardConfig::LILYGO_T5S3` carries the geometry, `DisplayController::LgfxEpd`, the
GT911 touch config, the PWM backlight, the I²C battery gauge and the PCF8563 RTC.

## Board configuration

The panel's power comes from the board's PMIC (TPS65185) and a PCA9535 I²C
IO-expander, which is board-specific, so `LgfxEpdDriver` takes its pins and power
hooks from the SDK's
[BoardT5S3 configuration](../libs/hardware/BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp).
`FREEINK_DEVICE_LILYGO=1` selects `lilygoT5S3LgfxConfig()` automatically; a
consumer does not need to duplicate the configuration or define
`FREEINK_LGFX_EPD_CONFIG`. The configuration uses a 16 MHz bus, line padding 8
and native landscape rotation 0. Its output-enable is on the expander, so
`pinOe=-1`. `pinPwr=GPIO46` is required by the i80 driver and also serves as the
LoRa chip select; the prepare hook deselects LoRa before bus initialization to
keep the shared SD SPI bus usable.

Add the SDK's [BoardT5S3 library](../libs/hardware/BoardT5S3/library.json) and
`https://github.com/m5stack/M5GFX.git#0.2.20` to the device environment's
`lib_deps`. The 0.2.20 release is a Git tag; the registry dependency in the
older sample does not resolve that version. Enable the board's 8 MB OPI PSRAM
with `BOARD_HAS_PSRAM` and the matching `dio_opi` memory type.

Call `BoardConfig::holdPowerRails()` first in setup to assert the GPIO2 latch,
then `BoardT5S3::begin()` before input, SD or display initialization. The board
helper initializes I²C, prepares SD SPI, disables GPS/LoRa and installs the
expander-button hook. In CrossMux this call belongs in `HalGPIO::begin()`.

## Peripherals

- **Touch** — GT911, handled by `InputManager` (polled, reset/address dance). The
  profile uses `BoardConfig::LILYGO_T5_PRO_GT911`.
- **Backlight** — PWM `FrontlightConfig` on BL_EN (GPIO11), driven by
  `FrontlightManager`; `CAP_FRONTLIGHT` is on for this device.
- **Battery** — `BatteryMonitor`'s I²C fuel-gauge backend
  (`FREEINK_BATTERY_I2C_GAUGE`) reads SoC/voltage from the BQ27220 and charge
  status from the BQ25896, with addresses/pins from `BoardProfile.batteryGauge`. It
  presents the same API as the ADC path and uses a minimal raw-register read (no
  external library).
- **Power button and sleep** — the power button is the BOOT button (GPIO0, a
  direct RTC-capable GPIO); the profile sets `input.power = GPIO0`. `InputManager`
  reads it, and `PowerManager::armPowerButtonWakeup()` arms deep-sleep wake on it
  with the per-SoC source (`ext1` on the S3). This matches the reference port,
  which wakes on the same BOOT button.
- **PCA9535 user button** — a second button behind the I²C expander.
  `BoardT5S3::begin()` installs an `InputManager::setButtonHook()` callback that
  reads the PCA9535 and returns `BTN_DOWN`, so `InputManager` carries no expander code. This
  button is not a deep-sleep wake source (neither is it in the reference port).

## SDK board support and sleep

- **PCA9535 expander and TPS65185 PMIC** — the EPD power sequence (via
  `LgfxEpdConfig::power`) and the user-button read (via `setButtonHook`) both drive
  the same PCA9535 through `BoardT5S3::ScopedI2CLock`.
- **GT911 home key** — the GT911 backend surfaces the capacitive home-key bit
  (status `0x10`) directly via `InputManager::wasHomeKeyPressed()`.
- **RTC** — the profile selects PCF8563 at 0x51 on SDA39/SCL40. The vendor
  product table says PCF85063, but the schematic and selected SDK backend use
  PCF8563.
- **LoRa and GPS** — disabled by board initialization; this reader integration
  does not expose them.

Finish rendering and unmount storage before sleep. The display driver's sleep
path invokes the board's PMIC/expander power-off hook. Keep GPIO2 held high
through deep sleep, then wait for BOOT to be released and arm GPIO0 wake via
`PowerManager::deepSleepUntilPowerButton()`. CrossMux already performs the
latch hold in its HAL power manager.

Physical acceptance remains pending for the CrossMux integration: test USB and
battery boots, SD access after panel initialization, touch corners and rotation,
the capacitive Home key and expander button, FAST/HALF/FULL and grayscale pages,
and at least three BOOT sleep/wake cycles. Record internal/PSRAM heap and largest
blocks before and after display initialization and repeated reading. A successful
build or host test does not establish panel quality or sleep current.


### Pinned M5GFX allocation audit

The actual Git dependency at tag `0.2.20` resolves to
`d8074bbac8b75d22149d128d71d55b6a34dbc808`. At 960×540, its
`LGFX_Sprite.cpp` requests 518,401 bytes for the 8-bit canvas (one guard byte)
and `common.hpp` rounds this to 518,404 bytes. `SpriteBuffer.cpp` may fall back
from PSRAM to DMA memory; the SDK checks `esp_ptr_external_ram()` and releases
the canvas on null or non-PSRAM storage. The two SDK planes still request exactly
64,800 bytes each. These checked, persistent buffers are reused after initialization.

The pinned `platforms/esp32/Panel_EPD.cpp` additionally allocates:

| Buffer | Requested bytes | Capability |
| --- | ---: | --- |
| Step framebuffer | 1,036,800 | PSRAM, 16-byte alignment |
| Current 4-bit pixels | 259,200 | PSRAM, 16-byte alignment |
| Two scanline buffers | 248 each | DMA |
| Pixel-pair LUT | 45,056 | DMA |

The LUT uses 88 steps: eraser 4, quality 32, text 32, and the SDK's fast/fastest
10 each. This makes the known canvas/planes/panel PSRAM requests 1,944,004 bytes,
plus the SDK's 64,800-byte single B/W framebuffer (PSRAM-first, checked malloc
fallback). The display task requests a 4,096-byte stack and an eight-entry update
queue; allocator overhead, bus-driver state and FreeRTOS control blocks are extra.
These are source-derived allocation requests, not measured heap margins. Record
internal/PSRAM free and largest blocks plus task high-water marks on real hardware;
no runtime margin is claimed by the host or firmware build.
