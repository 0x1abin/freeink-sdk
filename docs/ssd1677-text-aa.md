# SSD1677 text-only combined AA

`FREEINK_SSD1677_COMBINED_AA` defaults to 1. On ESP32-S3 builds for Sticky,
X4 Pro, X4 Classic, Murphy M4, CrossMux Waveshare ePaper 3.97 and Metalio E-Ink4,
the facade additionally requires the detected SSD1677 controller, 800x480
geometry and successful PSRAM allocation. Set the flag to 0 to retain the
original route. UC controllers and ESP32-C3 never allocate the new buffers.

Only a grayscale base requested with `RefreshContext::TextOnlyAntiAliasing`
selects the PaperMono-derived three-tone engine. `supportsTextOnlyCombinedBase()`
reports availability before staging; `combinesGrayscaleBase()` reports the
active text transaction. The Overlay encoding and existing strip interface
remain unchanged. Absolute/Direct requests retain the original backend.

Images and unmarked callers keep their original driver, LUT and two distinct
intermediate gray planes. Text/image switches wait for outstanding work,
invalidate the baseline and perform the board's normal correction. Repeated
text pages share one pixel activation; repeated image pages retain their
original refresh sequence. Paper Mono retains its existing behavior, with
an SSD1677 fallback if its eight PSRAM allocations fail.

`src/lut/Ssd1677CombinedAa.h` holds separate calibration entries. Sticky's
validated timing is unchanged; other boards start at nominal 5ms frames and
16/24/32 kick/gray/black counts, with the original gray voltage tails. Their
optical behavior is not yet validated. The original drivers still implement
corrective B/W, including Murphy batch selection and Metalio black-pulse
cleaning. Native RAM direction follows the board, not Paper Mono's mount.

Eight 48,000-byte PSRAM planes persist for the driver lifetime. This page/glass
history cannot live on the worker stack. No additional full-page cache is
allocated; partial OOM frees every allocated plane and retains the original
route. Incomplete gray data falls back to B/W; canceled generations are dropped.
BUSY timeout stops writes and forces baseline resynchronization on recovery.

Run the host checks from `libs/display/FreeInkDisplay/test/host`:
`test_sticky_combined_aa.py`, `test_ssd1677_text_route.py`, `test_ssd1677.py`
and `run_pro.py`. Tests compare complete original image command/data traces,
exercise both Murphy batches, scan orientation, power/sleep, OOM and timeouts.
CrossMux's physical acceptance record and packaging instructions live in
`docs/engineering/ssd1677-text-aa.md` in the parent project. A single controller
activation is not itself proof of optically flicker-free output.
