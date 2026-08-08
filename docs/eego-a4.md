# eego A4 board support

The `FREEINK_DEVICE_EEGO_A4` profile targets ESP32-S3 N16R8 with DIO flash,
8 MB OPI PSRAM, a 768×552 UC8279C panel, GSLX680 touch, PCF8563 RTC, and an
independent HSPI MicroSD bus. Pin assignments and calibration live in
`libs/hardware/BoardConfig/include/BoardConfig.h`.

The UC8279C sends the 768×552 framebuffer bottom-up into 768×600 controller
RAM, followed by 48 white rows. It uses an active-low BUSY signal, 20 MHz SPI,
and a 30 second timeout. Four consecutive fast refreshes are allowed; the next
refresh is full. The main framebuffer is 52,992 bytes. Each of the two 52,992
byte grayscale planes is allocated lazily from PSRAM only; failure falls back
to black-and-white without consuming internal DRAM.

Touch calibration is deliberately board data: raw Y 12..632 maps to display X,
and raw X 884..9 maps to display Y. The screen key sentinel is the complete
pair `rawXWord=0x03a0`, `rawYWord=0x1020`; a short press emits Back and a 700 ms
hold emits Home once. Deep sleep sends `0xE0=0x88`, holds GPIO3 low, and floats
SDA/SCL. Wake releases reset and reloads the controller firmware.

## Binary provenance and release gate

`tools/extract_eego_a4_gsl_firmware.py` verifies the caller-supplied image and
reproduces `libs/hardware/InputManager/src/gsl/EegoA4GslFirmware.h`, which
records the source image and table SHA-256 values. The table came from the
verified eego A4 S4 firmware image. `libs/display/FreeInkDisplay/src/lut/Uc8279cA4Luts.h` contains
the panel waveform data used by the verified reference driver.

These binary-derived assets are present for engineering validation. Do not
publish an eego A4 release until their redistribution review is complete and
the resulting attribution/license decision is recorded in `NOTICE`.
