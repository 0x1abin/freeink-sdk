# FreeInkFont

A standalone TTF/OTF font engine for e-paper firmware: real advances, kerning,
ligatures, per-codepoint fallback, and on-demand glyph rasterization to 8-bit
alpha bitmaps — **decoupled from any layout or book engine**. Depend on this
library alone to render fonts; you do not need the EPUB engine.

It is freestanding C++17 with no Arduino/ESP-IDF dependency and a strict no-heap
rule: the font file bytes are *borrowed* and all cache memory comes from a
caller-sized arena.

## API

- **`freeink::font::Font`** — metrics only: `advance()`, `lineHeight()`,
  `ascent()`, `kerning()`, `ligature()`, `covers()`. A layout pass can run
  host-side against this with no font files at all.
- **`freeink::font::RasterFont : Font`** — adds `hasGlyph()` and
  `rasterize(codepoint, sizePx) → const GlyphBitmap*` (8-bit coverage +
  bearings + advance).
- **`freeink::font::TtfFont : RasterFont`** — the stb_truetype backend.
  `init(const uint8_t* data, uint32_t len, Arena& glyphArena)`; the data is
  borrowed (PSRAM / mmap / arena-loaded SD file).
- **`freeink::font::FontChain : Font`** — up to 8 faces with per-codepoint,
  per-style fallback (user → Latin → CJK) so mixed scripts don't render tofu.
- **`freeink::font::Arena`** — the bump allocator backing glyph/advance caches.
- **`freeink::font::GlyphBitmap`** — `{pixels(w*h, 8-bit), width, height, xoff,
  yoff, advance}`.

Cache sizes are tunable via `-DFREEINK_FONT_ADVANCE_SLOTS` /
`-DFREEINK_FONT_GLYPH_SLOTS` (defaults 512 / 128).

## Using it from a third-party renderer (e.g. CrossPoint)

The whole point: adopt runtime TTF **without** replacing your existing text
layout. Add this one library, then bridge your renderer's font call-sites to
`RasterFont`:

```cpp
#include <TtfFont.h>          // FreeInkFont
using namespace freeink::font;

static uint8_t glyphArenaBuf[48 * 1024];   // ~32-64 KB per active size
Arena glyphArena(glyphArenaBuf, sizeof glyphArenaBuf);

TtfFont face;
face.init(ttfBytesInPsram, ttfLen, glyphArena);   // bytes borrowed

// measure:
int w = face.advance(cp, sizePx, StyleNone);
// draw:
if (const GlyphBitmap* g = face.rasterize(cp, sizePx)) {
  blit(g->pixels, g->width, g->height, penX + g->xoff, baseline + g->yoff);
}
```

Your pagination, line-breaking, and page cache stay exactly as they are — you
swap only the font backend (e.g. from a pre-rasterized bitmap format to live
outlines).

## Backends: stb_truetype and FreeType

- **`TtfFont`** — stb_truetype. Small, no extra deps; renders a font's default
  master only (no variable-font axes).
- **`FtFont`** — FreeType (vendored under `third_party/freetype`). Reads OpenType
  **variable-font axes** (real bold from the `wght` axis, real/oblique italic),
  streams large CJK faces, and does GPOS/kerning. Use this for variable fonts,
  multi-weight families, or CJK on constrained RAM. Both implement the same
  `RasterFont` interface, so consumers pick a backend without other changes.

### FtFont hinting options

`FtFont::setRenderOptions(RenderOptions)` picks the hinting/rasterization
strategy per face: `HintingMode::{None, Auto, Light, Native}`, plus
`monochrome` (1-bit output) and `stemDarkening` (auto-hinter only). This is
opt-in at build time, gated by three flags so a consumer that only wants the
default grayscale-AA path pays nothing extra:

- `FREEINK_FONT_ENABLE_NATIVE_HINTING` — compiles in FreeType's TrueType
  bytecode interpreter (`ftoption.h`), needed for `HintingMode::Native` and
  its `interpreterVersion` (35/40). Off by default: the interpreter has deep
  stack frames that can overflow a small render-task stack. A consumer that
  enables it should size that stack accordingly.
- `FREEINK_FONT_ENABLE_AUTOHINT` — registers the `autofit` module, needed for
  `HintingMode::Auto`/`Light` and `stemDarkening` to do anything.
  `FT_LOAD_FORCE_AUTOHINT` is a no-op without it.
- `FREEINK_FONT_ENABLE_MONOCHROME` — registers the classic B/W `raster`
  (`raster1`) renderer, needed for `monochrome` output. `FT_RENDER_MODE_MONO`
  fails without it.

None of these three change `TtfFont`. For `FtFont`, the "opt-in" guarantee is
precise, not absolute: compiling `FREEINK_FONT_ENABLE_AUTOHINT` and/or
`_MONOCHROME` in, by themselves, never change a caller's output if they never
touch `RenderOptions` — `HintingMode::Default` sets `FT_LOAD_NO_AUTOHINT` so
it can never silently prefer the auto-hinter just because the module became
available (confirmed identical output, hashed across sizes/codepoints, to a
build with neither compiled — see `test/host/run.sh`). `Default` deliberately
does **not** also set `FT_LOAD_NO_HINTING`: that bit gates both the native
bytecode hinter *and* FreeType's always-on phantom-point advance rounding
together (`ttgload.c`'s `IS_HINTED`), so disabling it to block one disables
the other too, truncating instead of rounding every advance — an earlier
version of this change did exactly that and regressed pagination in the
*default, no-flags* build, which is worse than the bug it was meant to fix.
When `FREEINK_FONT_ENABLE_NATIVE_HINTING` is compiled, `Default` may use the
native hinter with FreeType's normal interpreter version 40; when it is not
compiled, the same flags retain normal hinted advance behavior without an
auto-hint fallback. `Native` is the explicit mode for selecting interpreter
v35 or v40. `setRenderOptions()` returns `false` (without refusing the request)
when it needs a module this build didn't compile in, or an `interpreterVersion`
other than 35/40, so a caller with real logging can warn instead of getting
silently degraded output.

`interpreterVersion` and `stemDarkening` are FreeType *library-wide*
properties (`FT_Property_Set` has no per-face scope), not per-`FtFont`
settings, so they are deliberately **not** applied inside `setRenderOptions()`
(a one-shot call) — that would mean whichever face's `setRenderOptions()` ran
most recently wins for every OTHER face's subsequent renders, not whichever
face is actually about to draw. Instead they're re-applied immediately before
every `FT_Load_Char` (`advance()` and `rasterize()`), from that face's own
`options_`: Native selects its requested 35/40, while every non-Native mode
restores FreeType's normal v40. Only the face genuinely rendering right now
can affect what FreeType sees, closing cross-face property clobbering in both
directions.

**Vendoring note:** the `autofit`/`raster` module sources here are from
FreeType 2.14.3, while the rest of `third_party/freetype` in this tree is
2.13.3. `find_unicode_charmap` in `ftobjs.c`/`ftobjs.h` was changed from
`static` to `FT_BASE_DEF`/`FT_BASE` to match 2.14's exported linkage, which
`afadjust.c` in `autofit` calls directly — that's the only cross-version patch
this required. Validated with a host-side ASan/UBSan build across every
`HintingMode` plus monochrome against a real bundled font
(`test/host/FtFontRenderOptionsTest.cpp`), but the base FreeType tree hasn't
been bumped to 2.14 wholesale, so other 2.13/2.14 internal drift is possible
in code paths this didn't exercise. Bumping the whole vendored tree to one
matching version is worth doing as a follow-up.

### FreeType attribution (FTL)

`third_party/freetype` is a curated build of **FreeType** (https://freetype.org),
used under the **FreeType License (FTL)** — see `third_party/freetype/FTL.TXT`.
Per the FTL, products that include this library must credit FreeType in their
documentation:

> Portions of this software are copyright © The FreeType Project
> (www.freetype.org). All rights reserved.

## CJK / large fonts

stb_truetype needs the whole font file in RAM, which a no-PSRAM device can't do
for a multi-megabyte CJK face. A **FreeType** backend (streaming table access) is
the upgrade path and slots in behind the same `RasterFont` interface as a second
implementation alongside `TtfFont` — no API change for consumers. This library
ships the stb_truetype backend first.

## Relationship to FreeInkBook

This engine used to live inside FreeInkBook. It was extracted here unchanged;
FreeInkBook now re-exports the types under their historical names
(`freeink::book::BookFont == Font`, `RenderFont == RasterFont`, `TtfFont`,
`FontChain`, `Arena`) via thin alias headers, so its layout code and its full
host-test suite are unaffected.
