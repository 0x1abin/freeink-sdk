# CrossMux BSP alignment

Reference SDK: `7fcd3b34c108f28c086ade3b433924022dbc0998`.
Reference reader: `ba39565ce7b3a8aab14ca86da4fa4c3ca140f007`.

## Official reader devices

X3 (UC8253 and UC8279), X4, X4 Pro, X4 Classic, Sticky, Paper Mono, EEGO A4
and Metalio E-Ink 4 use the reviewed upstream board profiles, drivers, input,
storage and power behavior. LilyGo T5 S3 PRO/Lite uses the SDK's BoardT5S3 and
LgfxEpdDriver support. Runtime controller detection and SPI selection follow
the reference SDK. Existing allocation-failure safeguards remain in the
LgfxEpdDriver integration; no separate board configuration is copied into the
consumer.

The facade retains source-compatible refresh-context overloads for CrossMux.
For official drivers these forward to the original entry points: continuous
reading and dark-background hints do not replace upstream waveforms or cleanup.
Absolute/direct capabilities, upload cancellation and post-gray recovery come
from the upstream drivers. These capabilities require the consumer HAL and
renderer to select the corresponding mode; updating the SDK alone does not
change every sleep-image rendering path.

## Independent local boards

`CrossMuxSsd1677Driver` isolates the previously validated Murphy M4 and local
Waveshare 3.97 sequences. Its implementation is compiled
only for those devices, and only their profiles select it. It retains the
local refresh context, dark-background hint, batch calibration and power-off
behavior. Absolute grayscale remains disabled for these local profiles.
The SDK WS397 entry remains distinct from CrossMux WAVESHARE_EPAPER_397.
Read Pico retains its BoardReadPico and epdiy LCD integration; neither it nor
the other local profiles is replaced with a different upstream board merely
because it shares the same MCU or panel controller.

The copied SSD1677 backend deliberately preserves the existing local sequence
implementation while keeping the upstream backend independently reviewable;
future upstream driver changes must be reviewed for local applicability, not
blindly copied into this backend.

## CrossMux extensions

EEGO's GSL Home contact emits only Home tap/hold events; the Reader owns the
configured action. The consumer HAL holds configured power latches before
power-down, so the SDK does not repeat an EEGO-specific GPIO4 hold. Metalio's
expander initialization starts in `holdPowerRails()`, matching upstream boot
order; later callers reuse its checked, idempotent initialization.

Physical press observation and consumed touch-edge clearing are additive input
interfaces for the consumer's logical input ownership. They do not change the
upstream official-board sampling, debounce, gestures or touch decoding.
Local touch, power, audio and RTC drivers remain selected by local profiles.
Previously approved capacity-query fixes are retained.

Standby-face timed light sleep, BLE power guards and concurrency protection
belong to the consumer HAL, outside the ordinary upstream deep-sleep path.
No per-page full-frame allocation is introduced by this alignment. LilyGo's
existing driver allocations are accounted for in its
[support notes](lilygo-t5s3-support.md#display).

## Acceptance

Host command-sequence tests and official-source/profile comparisons establish
software alignment only. Physical residual-image, sleep-current and wake
acceptance must be recorded by device and panel batch. Historical local Metalio
calibration results do not establish acceptance for the adopted upstream
waveform or power-off path. SDK support entries do not enroll devices in
CrossMux builds, Nightly releases or OTA catalogs.
