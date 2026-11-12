# Drawing paths and the frozen text oracle

The production inventory below describes firmware `b69dc74b` and pinned SDK
`6a573acd`. Paths and line numbers are relative to that source revision.
The drawing archive still freezes firmware `e75c7dda` with SDK `37d272d4`:
`draw.json.gz` adds format-1 `draw/` records beside the unchanged T0
`golden.json.gz`. Current production code must reproduce those records; neither
archive may be regenerated to accept a changed result.

## Current production inventory

| Entry and source | Current work and preserved contract |
| --- | --- |
| `GfxRenderer::drawText`, `lib/GfxRenderer/GfxRenderer.cpp:431`; `drawGlyphRun`, `:437` | Native text facade resolves the font, baseline, scan/preparation callback and physical target, then calls Rust `xp_native_draw_pixels` (`rust/crates/xp-ffi/src/textm.rs:579`). Rust performs glyph placement and painting; this is not a native codepoint drawing loop. Preserve style, direction, tracking, foreground color and fallback behavior. |
| `GfxRenderer::drawTextRotated90CW`, `GfxRenderer.cpp:1785` | Calls the same glyph-run facade with rotation, LTR and zero tracking. Preserve the rotated path's existing differences from horizontal shaping and SUP/SUB/small-caps behavior; do not infer equivalence from their shared entry. |
| `GfxRenderer::drawScaledCodepoint`, `GfxRenderer.cpp:1731` | Retains scan recording and the font/target loan, then calls `xp_native_draw_scaled_pixels` (`textm.rs:682`). Rust paints one always-black codepoint with bilinear scaling and returns its advance. This differs from the coverage scaling used for reduced text styles. |
| `GfxRenderer::drawCenteredText`, `GfxRenderer.cpp:425` | Computes x from `getTextWidth` and the oriented screen width, then calls `drawText`. Preserve requested style, direction and color. |
| Rust glyph placement and rasterization, `rust/crates/xp-core/src/textm/draw.rs`, `rust/crates/xp-ffi/src/textm.rs:550`, `rust/crates/xp-core/src/glyph/mod.rs:392` | Replaces the old native `renderCharImpl`, `renderCharScaled` and `drawGlyphBitmap` helpers. The physical target retains logical clipping, panel orientation, strip origin, BW/LSB/MSB and absolute-plane encoding. A 1-bit destination plane can receive 1-bit or 2-bit font sources; source bit depth and destination encoding are separate. |
| Direct Rust UI scene entry, `rust/crates/xp-ffi/src/app/scene.rs:21` | Shipping Rust owners borrow the active target and clear/text services, then enter `xp_ui::ffi::render`. Native-origin screens use `GfxRenderer::drawUiScene` (`lib/GfxRenderer/RustUiRenderer.cpp:40`) and `xp_ui_render`. SDK text boxes and resource painters remain synchronous native services; their inherited continuations are not removed by the direct scene entry. |

See [Drawing and resources](../../docs/contributing/rust-ui.md#drawing-and-resources)
for the scene/resource contract. Font resolution, measurement and preparation
still run through the real production services. The former C++ glyph helpers
are historical capture provenance, not current call sites.

### Page and UI call contracts

EPUB page painting now enters Rust through
`rust/crates/xp-ffi/src/page_render/direct.rs:218`. Its decoded-arena renderer
(`rust/crates/xp-core/src/page/render.rs:326`) applies cached word positions,
font/style selection, SUP/SUB offsets, focus-reading splits and ruby placement.
It does not dispatch each word through the old `PageLine::render` / `TextBlock`
C++ path. The drawing oracle supplies fixed word offsets and styles to the text
API; it does not execute the production page-layout algorithm. Page/section and
reader-strip tests remain separate integration coverage.

Rust-owned clipping, definition, footnote and keyboard scenes share the direct
scene renderer; native resources still provide their existing text services.
White labels, selection colors, clips and the active target must remain unchanged.
SDK `FreeInkUIGfxRenderer.h` forwards font/style/color and logical clipping; its
CW90 branch uses the rotated facade and other element rotations retain their
fallback behavior. Whole-panel orientation is independent. The drawing oracle
covers all four transforms on an 800×480 target, not every board's dimensions.

Find current native facade callers with:

```sh
rg -n 'drawText\(|drawTextRotated90CW\(|drawScaledCodepoint\(|drawCenteredText\(' \
  src lib freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIGfxRenderer.h
```

This search excludes direct Rust page and scene calls; it is not a complete
inventory of firmware drawing. The direct entries are listed above.

## Run and record contract

Build using the commands in README.md, then run:

```sh
python3 test/text_golden/run.py check --work-dir build/text-golden/check
python3 test/text_golden/draw_run.py check --work-dir build/text-golden/draw-check
```

CTest registers these as `TextGolden` and `TextDrawGolden`. `draw_run.py capture`
is permitted only against the original production source and pinned SDK. The
combined `.gz` fixtures, including public font files, must stay below 2,000,000 B.
The drawing matrix is mostly hash-only to keep that budget.

Private mode has the same flags and external-path restrictions as T0:

```sh
python3 test/text_golden/draw_run.py check \
  --private-font-dir /path/to/private/fonts --golden /path/to/private/draw.json.gz \
  --work-dir /path/to/private/draw-check
```

Every case begins with the existing eleven T0 fields. Keys start `draw/`; these
records bypass the original measurement record path. The appended fields are:

```text
path orientation black absolute x y clipX clipY clipWidth clipHeight stripY stripRows scale256 wordCount
```

Path values are text=0, rotated=1, scaled=2, words=3, centered=4. Each word then
adds `offsetX offsetY style direction textHex`. Offsets are fixed inputs, never
computed from the candidate's measurement results. RTL word schedules use
reverse x offsets. `excerpts` in the archive records the selected public EPUB
entry and six consecutive source tokens, plus synthetic RTL/combining/CJK lines.
They are representative draw inputs, not claims about actual paginated positions.
The archive freezes the complete input stream hash, font/EPUB hashes, per-group
ordered output SHA-256, counts and one detailed sample per group.

Each output has three tab-separated columns:

1. Case key (font/path/orientation group and all varying configuration labels).
2. BW helper-entry trace `cp,x,y,fontId,style;`, then three `|framebufferHash`
   values for BW, LSB, MSB. Strip cases append `,guardedStripHash` to each plane.
3. Scaled path only: `cp,returnedAdvance;` for each codepoint in the BW pass.

Absolute mode is activated after the BW pass, as in the reader; the harness
checks that the renderer actually enters the requested encoding for each plane.

The pixel hashes retain T0's FNV-1a 64-bit algorithm; ordered full records are
hashed with SHA-256. The current host instrumenter observes Rust placements
through `xp_native_draw_pixels_traced`, preserving the original helper-entry
record schema. The observer at `drawScaledCodepoint` entry records direct calls,
including missing or zero-scale attempts. See [README.md](README.md) for the
instrumentation boundary. All rasterization still uses production code. For strips,
16 bytes of 0xa5 surround the scratch allocation, and the framebuffer is filled
with 0x5a so accidental writes to the inactive target affect the recorded hash.

The main matrix crosses all 16 T0 styles, four orientations, both colors (except
always-black scaled codepoints), both gray encodings and 13 synthetic strings.
Edge/UI/empty-clip and strip cases cross four representative styles with the
same orientation/color/encoding dimensions. Additional cases cover tracking,
explicit direction, centered text, fallback routing, and scale boundaries.
Public fixtures exercise both 1-bit built-in and 2-bit built-in/SD/TTF glyphs;
real Korean/CJK glyph coverage requires the private run.

Limitations remain: no hardware waveform, display timing, allocation-failure
schedule, other panel dimensions, full page assembly, focus/ruby layout algorithm
or dynamic font discovery is modeled. Rotated cases prewarm horizontally because
the production rotated API has no cache scan branch. Private results and font
bytes must never be committed. Keep section/layout and real-font emulator/device
checks as separate integration gates.
