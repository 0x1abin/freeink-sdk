# EEGO A4 board support

`FREEINK_DEVICE_EEGO_A4` targets an ESP32-S3 N16R8 with DIO flash, 8 MB OPI
PSRAM, a 768×552 UC8279C panel, GSLX680 touch, PCF8563 RTC, and a dedicated
HSPI MicroSD bus. The implementation uses the SDK's existing `PanelDriver`,
`InputManager`, `BoardConfig`, and `PowerManager` abstractions.

The controller receives the 768×552 framebuffer bottom-up in its 768×600 RAM,
followed by 48 white rows. BUSY is active-low and display SPI runs at 20 MHz.
B/W refresh cadence belongs to the caller. FAST remains fast, including after
grayscale; HALF and FULL use the full waveform. The first B/W paint after
initialization clears unknown glass state with the full waveform. After grayscale,
the next B/W paint seeds the old plane with the inverse of the target, then drives
every pixel with the requested waveform. Each completed B/W paint copies the
displayed frame into the old plane for the next differential update.
The main framebuffer is 52,992 bytes. Each grayscale plane is also 52,992
bytes and is allocated lazily from PSRAM only. Allocation failure leaves the
existing black-and-white image visible and never falls back to internal DRAM.

Touch calibration is board data: raw Y `12..632` maps to display X, while raw
X `884..9` maps in reverse to display Y. The screen-key sentinel is the full
pair `rawXWord=0x03a0`, `rawYWord=0x1020`; a short press emits a Home tap and a
700 ms hold emits a Home hold once. The Reader selects their actions; no Back
button edge is synthesized. Deep sleep sends `0xE0=0x88`, floats SDA/SCL, and holds
GPIO3 low. Wake releases reset and reloads the controller data.

The controller-data and waveform-table byte hashes are recorded beside the
arrays so migrations can detect accidental changes. Applicable MIT attribution
is recorded in the repository `NOTICE.md`.

See [the support notes](eego-a4-support.md#open-items-need-a-physical-unit) for
pending physical acceptance, including long FAST-only navigation and AA page
turns without automatic full cleanup after grayscale.
