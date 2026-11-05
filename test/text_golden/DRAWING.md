# Drawing paths frozen before the Rust glyph-walk port

This inventory refers to firmware `e75c7dda` and SDK `37d272d4`. All file paths
below are relative to the repository root. `draw.json.gz` adds a format-1 `draw/`
record family beside the unchanged T0 `golden.json.gz`. The function names and
line numbers in the inventory describe that revision, not the current renderer.
Run both checks after a port; neither archive should be recaptured to accept a
changed drawing result.

## Production inventory at capture

| Function and source | Production callers | Parameters and behavior to preserve |
| --- | --- | --- |
| `GfxRenderer::drawText`, `lib/GfxRenderer/GfxRenderer.cpp:696` | `TextBlock::render`, centered-text wrapper, UI themes/activities, SDK `GfxRendererTarget::text` | Black or white; selected orientation; logical clip and physical strip; requested/fallback built-in, SD or TTF font; regular/bold/italic/bold-italic plus SUP/SUB/SMALL_CAPS; AUTO/LTR/RTL and tracking. Resolves fallback for the whole string, shapes bidi/Arabic, applies ligatures, fixed-point differential rounding and combining-mark anchors. |
| `GfxRenderer::drawTextRotated90CW`, same file:2321 | `BaseTheme.cpp:273,283,315`, `LyraTheme.cpp:173,181,202`, SDK `FreeInkUIGfxRenderer.h:260` | Theme buttons use SMALL_FONT_ID, regular black text; SDK accepts font/style/color and clips. Glyphs advance upward from the supplied y; all panel orientations still apply. It uses ligatures and mark anchors but does **not** call horizontal bidi shaping, small-caps conversion, SUP/SUB scaling or tracking. Freeze these existing differences. |
| `GfxRenderer::drawScaledCodepoint`, same file:2234 | `PageDropCap::render`, `lib/Epub/Epub/Page.cpp:140–148` | One codepoint at a time, always black, explicit baseline, font/style and scale in 1/256 units. Caller increments x by the returned integer advance; no word shaping/kerning. The assembler clamps production scale to 256–4096 (`rust/crates/xp-core/src/chapter/assembly/boxes.rs:258`); direct API probes also test zero/negative and fractional reductions. |
| `renderCharImpl`, `GfxRenderer.cpp:530` | Horizontal and rotated glyph walks | Unscaled bitmap/solid fallback; rotation selected by template; strips culled before bitmap loading; BW/LSB/MSB and absolute-plane override. |
| `renderCharScaled`, same file:469 | Horizontal SUP/SUB and lowercase small caps | 1/2 or 3/4 coverage-based reduction, scaled bearings, black/white, clip/orientation/strip via `drawPixel`. Different algorithm from drop-cap bilinear scaling. |
| `drawGlyphBitmap`, same file:592 | `renderCharImpl` | Rasterizes 1-bit or 2-bit glyph sources into a **1-bit** framebuffer/plane target; applies logical clip, orientation, physical bounds and strip origin. There is no packed 2-bit text target API: two gray planes plus BW encode the gray result. |
| `drawCenteredText`, same file:690 | Boot/sleep, network/settings/library/reader activities; see call-site list below | Computes x from `getTextWidth` and oriented screen width, then calls `drawText`; default black/regular/AUTO, explicit bold/white where requested. |

No other production GfxRenderer function walks codepoints to draw glyphs.
`resolveTextFontId`, `ensureSdCardFontReady`, fallback prewarming and measurement
walks prepare/measure inputs; their effects still execute through the real code.
The unused private `renderChar` declaration in GfxRenderer.h has no definition or caller.

### Page and UI call contracts

- `PageLine::render` (`Page.cpp:74`) adds page-element and page-margin offsets
  before calling TextBlock. `TextBlock.cpp:382–427` draws **each word separately**
  at cached `xposArr[i] + x`, with its own font/style and detected word direction.
  SUP raises y by 2/5 of the ascender; SUB lowers it by 1/4. Focus reading splits
  a bold prefix and normal suffix at a cached x offset. Ruby draws SUP strings
  or separate tokens at computed offsets (`TextBlock.cpp:430–454`). The oracle
  supplies explicit offsets, mixed styles and raised/lowered words to the same
  renderer API; it does not recreate TextBlock's layout or execute TextBlock.
- `PageDropCap::render` uses a loaded sized font or section-font fallback and
  adds page offsets to its baseline. The harness mirrors the returned-advance
  cursor loop, including missing glyphs and combining codepoints.
- `EpubReaderActivity.cpp:2063–2111` scans/prewarms a page and draws the BW base.
  `:2159–2165` and `:2222–2244` draw physical strips; the
  actual firmware band height depends on its allocated scratch size. `:2268–2278`
  clears overlay planes to 0x00 or absolute planes to 0xff and renders the page
  again for each plane. Absolute mode makes text a monochrome overlay on the
  image page. Font source bit depth and the destination plane are separate.
- `ClipSelectionActivity.cpp:749,769,866` redraws words at adjusted offsets;
  `DictionaryDefinitionActivity.cpp:675` draws individual lines. Keyboard
  cursor/toggle labels (`KeyboardEntryActivity.cpp:870,893`) and selected rounded
  theme titles (`RoundedRaffTheme.cpp:174`) use white text. Footnote selection
  numbers (`EpubReaderFootnoteSelectActivity.cpp:125`) are also white.
- SDK `FreeInkUIGfxRenderer.h:50–59,232–297` forwards logical clipping, selected
  font/style and foreground color. Its CW90 branch calls the rotated path;
  other text rotations degrade to horizontal. Global panel orientation remains
  one of Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise
  (`GfxRenderer.h:40`). The oracle tests those four transforms on the 800×480 panel.

For an exhaustive current list of direct call sites (including ordinary centered
status messages), run:

```sh
rg -n 'drawText\(|drawTextRotated90CW\(|drawScaledCodepoint\(|drawCenteredText\(' \
  src lib/Epub lib/GfxRenderer freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIGfxRenderer.h
```

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
