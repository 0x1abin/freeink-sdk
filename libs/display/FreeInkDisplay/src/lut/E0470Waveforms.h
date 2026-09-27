#pragma once

// E0470A01 (684 x 1216, 40-pin) waveform data for the raw-parallel LgfxEpd path.
//
// PROVENANCE
//   Converted from the vendor epdiy tables shipped with the board
//   (MindReset/read_pico_firmware, components/e0470_epaper_waveform/waveforms/
//   {gc16,gl16,du}.h, Apache-2.0). The frozen port decision is to start from
//   E0470_FULL_WAVEFORM (GC16 48 phases, GL16 48 phases, DU 20 phases, one
//   0-50 C range), so no boot-time trim run and no re-implementation of
//   e0470_waveform_trim() is needed. The two boot-time derivations of that
//   waveform *are* applied here, baked into const flash data:
//     * e0470_complete_du_build()  - the threshold DU (dest 0-7 -> source to=0,
//       dest 8-15 -> source to=15) built from du.h;
//     * e0470_gl16_white_tick()    - one extra white push at (to=15, from=15)
//       on gl16.h (frame 45).
//   See docs/engineering/read-pico.md 2.5/2.6 and blocker B3.
//
// WHAT THE TARGET MODEL ACTUALLY IS  (M5GFX 0.2.20 lgfx::Panel_EPD)
//   A LUT entry is LUT_MAKE(d0..d15): 16 x 2-bit actions, d[i] = the action for
//   gray level i (0 = black ... 15 = white). Action 1 = "to black", 2 = "to
//   white", 3 = "no operation"; 0 means "end of data" and terminates that
//   pixel's sequence, so every table must end with an all-zero row. That
//   terminator row is one extra non-driving frame on top of the vendor frame
//   count (Panel_EPD clocks it, and M5GFX's own built-in tables do the same),
//   which is why the *Step constants below are Frames + 1.
//   The level that indexes d[] is the low byte of Panel_EPD::_step_framebuf,
//   and task_update() overwrites that byte with the pixel's level from _buf -
//   i.e. its TARGET level - before the mode's own table runs
//   (Panel_EPD.cpp:990-1004 generic, :952-986 epd_text, :930-950 fast; the
//   "end of data" branch reloads it from the request slot, :727-733 in the
//   Xtensa path and :822-827 in the C fallback).
//   The epdiy tables are 2-D: data[frame][to][from/4], 4 bytes per (frame, to).
//
// CONVERSION RULE (lossy by construction - see below)
//   d[to] = the action that the majority of the 16 `from` cells receive in that
//   frame (ties -> the lowest action value). epdiy action 0 (hold) maps to
//   Lgfx 3 (no op); 1 and 2 keep their meaning.
//
// WHY IT IS NOT FAITHFUL
//   Only 62.1% of the GC16 (frame, target) columns are constant across
//   `from`; the `from` dependence is the vendor's per-transition "hold pad +
//   erase ramp" (the further a pixel has to travel, the earlier its push
//   starts). The Lgfx model has one vector per frame indexed by the destination
//   only, so that axis cannot be expressed at all. Measured against the real
//   tables, the projection reproduces 94.1% of every (frame, target,
//   from) action for GC16, and 100.0% of them over the last 19 of the 48
//   frames - the region that carries the settled gray ladder, where the
//   per-target net push (whiten minus darken) is reproduced exactly. GL16
//   93.8% overall / 99.5% in the tail; DU 85.6% (its `from`
//   axis is intrinsic - a DU transition IS a travel distance - so it is the
//   table that pays the most for the projection). This is a partial, documented
//   mapping, NOT a faithful port: the erase/ramp half of every transition is
//   approximated by the majority action, so ghosting behaviour is the thing to
//   A/B against the reference firmware on hardware (read-pico.md blocker B3).
//
// Gating: only built for the Read Pico build, so no other target's flash
// changes. Nothing here allocates; the tree is 119 const entries = 476 bytes of
// flash. If the board config never references these symbols the linker drops
// the whole object. A board config reaches these declarations through the
// public forwarder include/LgfxEpdWaveforms.h (`#include <LgfxEpdWaveforms.h>`),
// because headers under src/ are private to this library.
//
// RESIDUAL DEVIATIONS FROM THE VENDOR SEQUENCE (all need hardware A/B)
//   1. M5GFX prepends its own fixed 2-frame lut_eraser to every epd_text /
//      epd_quality refresh, indexed by the PREVIOUS target level
//      (Panel_EPD.cpp:1002-1004 arms it, :149-156 is the table). It is not
//      vendor data; epd_fast / epd_fastest skip it (Panel_EPD.cpp:942). So a
//      page refresh here is 2 eraser frames + 48 GC16 frames + 1 terminator.
//   2. epdiy's "hold" is 2-bit code 0, Lgfx's no-op is code 3. Both mean "do not
//      drive" in the code that ships them (M5GFX pads its own tables with ~0u
//      rows, i.e. all-3) and M5GFX tabulates the meaning as
//      "0 == end of data / 3 == no operation" (Panel_EPD.cpp:80), but how this
//      panel's source driver decodes 0 versus 3 cannot be settled from source.
//      Code 0 is unusable for hold here: it is the terminator.
//   3. The frame period comes from the i80 line timing, not from the vendor's
//      E0470_WAVEFORM_FRAME_US (11090 us) - read-pico.md 2.4 / blocker B4.
//   4. The terminator row costs one extra non-driving frame per refresh.

#include <stddef.h>
#include <stdint.h>

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_READPICO) && FREEINK_DEVICE_READPICO

namespace freeink {

// GC16, 48 vendor frames -> 48 LUT entries + terminator. Full-screen 16-level
// page refresh; select it for a mode that receives the dithered 8-bit canvas
// (the LgfxEpd driver's epd_text, i.e. RefreshMode::Full / Half).
extern const uint32_t kE0470Gc16[];
constexpr size_t kE0470Gc16Frames = 48;
constexpr size_t kE0470Gc16Step = kE0470Gc16Frames + 1;

// GL16, 48 vendor frames (with the live white push already applied) -> 48 + 1.
// Landed for A/B comparison and for a future grayscale page mode; NOT wired to
// a driver mode yet because LgfxEpdDriver only ever selects epd_text/epd_fast
// and every extra wired table costs internal DMA RAM (see kE0470*Step notes).
extern const uint32_t kE0470Gl16[];
constexpr size_t kE0470Gl16Frames = 48;
constexpr size_t kE0470Gl16Step = kE0470Gl16Frames + 1;

// Threshold DU, 20 vendor frames -> 20 + 1. Only levels 0 and 15 are ever
// produced on the path that uses it (epd_fast renders 1 bpp), which is exactly
// what the threshold DU is for.
extern const uint32_t kE0470Du[];
constexpr size_t kE0470DuFrames = 20;
constexpr size_t kE0470DuStep = kE0470DuFrames + 1;

}  // namespace freeink

#endif  // FREEINK_DEVICE_READPICO
