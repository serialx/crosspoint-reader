# Real text renderer oracle

This freezes the C++ text behavior at `c9330408c0ff5fd22a95d0dbb635b1ff5ffa5dca`
with SDK `37d272d42732c31065ebed4d9cb90cef7bfa7416`. It compiles the production
`GfxRenderer`, `EpdFont`, `EpdFontFamily`, `SdCardFont`, `FontDecompressor`,
`FontCacheManager`, `TtfEpdFont`, MiniBidi and UTF-8 code. The SDK's vendored
FreeType, GSUB and GPOS code are included. The Rust archive supplies the existing
UTF-8 helpers. No measurement function is stubbed.

## Run

From the repository root, with CMake, a C++20 compiler, Python 3.9+ and the
repository's Rust toolchain available:

```sh
git submodule update --init --recursive
cmake -S test/text_golden -B build/text-golden -DCMAKE_BUILD_TYPE=Release
cmake --build build/text-golden -j 6
python3 test/text_golden/run.py check --work-dir build/text-golden/check
```

The same public check is registered as `TextGolden` in the full host CTest suite.
The standalone build needs no downloaded GoogleTest or decoder sources. The SDK
FreeType configuration matches the firmware's `FREEINK_FONT_ENABLE_AUTOHINT=1`;
`BOARD_HAS_PSRAM` enables the real TTF adapter on the host. That does not claim
TTF support on the ESP32-C3 firmware.

`capture` writes `golden.json.gz`; it refuses to capture if the relevant
production sources differ from the baseline revision or the SDK pin differs.
Use `check` after a port. Do not recapture to accept a changed result.

```sh
python3 test/text_golden/run.py capture --work-dir build/text-golden/capture
```

The optional private run tests each unique `.cpfont` found recursively under the
supplied directory, plus fallback routing from Ubuntu 10 to those fonts. It does
not repeat the public matrix. Both its archive and working directory must be
outside the repository:

```sh
python3 test/text_golden/run.py capture \
  --private-font-dir /path/to/private/fonts \
  --golden /path/to/private/golden.json.gz --work-dir /path/to/private/capture
python3 test/text_golden/run.py check \
  --private-font-dir /path/to/private/fonts \
  --golden /path/to/private/golden.json.gz --work-dir /path/to/private/check
```

Private font paths, hashes and derived glyph records stay in that external
archive. The program uses short relative font paths because the firmware loader
has a 128-byte path buffer. Omit `--work-dir` for automatically removed temporary
files. With it, `cases.tsv`, `records.tsv`, `corpus.json`, `fonts.tsv` and
`summary.json` remain available for diagnosis; allow roughly 500 MB for a public
run. Fonts in the repository are public fixtures only.

## Coverage and preparation

- All seven families/sizes registered by `src/main.cpp`: Noto Sans 12/14/16/18,
  Ubuntu 8/10/12, with their actual style fallback rules (21 built-in faces).
- Public SD fixtures: Noto Sans 14 (four styles), Noto Sans Arabic 14 and Noto
  Sans Hebrew 14 (regular/bold, exercising absent italic fallback). The committed
  gzip files are decompressed before the real `.cpfont` loader reads them.
- TTF: the same three public regular font sources at 14 pt, including the
  production adapter's synthesized bold/italic/bold-italic. Native alternate
  style files, variable fonts, and streamed TTF sources are not covered.
- Synthetic strings: Latin kerning/ligatures, Korean/Jamo, Chinese/Japanese,
  Arabic contextual forms/Lam-Alef/harakat, Persian, Hebrew niqqud, mixed bidi,
  combining and orphan marks, missing glyphs, solid symbols, spaces, small
  caps, sup/sub, controls, empty/single-codepoint strings and malformed UTF-8.
- The synthetic matrix crosses 16 styles (four base styles × plain/sup/sub/
  small caps), tracking -2 through +4, AUTO/LTR/RTL, and Layout/Rendered modes.
  Drawing runs for tracking -2/0/+4 in all directions/styles, in BW and both
  grayscale planes. Draw origin is (12, 50), portrait, on an 800×480 panel.
- Every distinct word and adjacent-word boundary-codepoint pair from every
  HTML/XHTML entry in `test/epubs/*.epub`. Each word is measured for all 16
  styles at tracking 0, AUTO/Layout, using the same `getTextAdvanceX` defaults
  as `ChapterPageAssembler::textAdvance`. Space contexts run across all 16
  styles. Duplicate word occurrences retain their document order in
  `corpus.json`, whose hash is frozen in the archive.

The tokenizer uses Python's HTMLParser: body text only, entities decoded,
script/style text omitted, block tags become spaces, inline text stays joined,
then Unicode whitespace splits words. It does not interpret CSS or reproduce
chapter semantic word splitting, hyphenation, bidi word order or pagination.
Thus this is a metric-input oracle, not a replacement for section goldens.

Each case prepares the packed text through `ensureSdCardFontReady`, including
space, hyphen and uppercase small caps. Rendered/drawing cases also use the real
FontCacheManager scan/prewarm scope. Caches persist in the defined case order;
this covers the SD advance-only and resident render-table paths, but not every
cold-cache, eviction or allocation-failure schedule. The host advertises 200 KiB
free/largest heap blocks and does not emulate memory pressure or storage timing.

Public Noto fonts do not cover CJK; their CJK results test replacement/missing
behavior. Real Korean/CJK glyphs require private SD inputs. Sup/sub records test
the renderer's scaling; the vertical shift owned by TextBlock is outside scope.
The T0 records do not test hardware display, orientation changes or refresh waveforms.
The separate [drawing extension](DRAWING.md) covers the orientation and clipping matrix.

## Records and use by T1–T5

`cases.tsv` is ASCII, with UTF-8 text hex encoded (so malformed bytes survive):

```text
key font style tracking direction mode draw fallback leftCp rightCp textHex
```

Each `records.tsv` line has four tab-separated columns:

1. Stable case key (`group:corpus-index-or-synthetic-label`).
2. CSV: resolved font ID, advance X, ink width, space width, space advance,
   kerning, ascender, line height, text height, line height compressed by 0.85.
   Pixel values are integers; the style and tracking inputs are in the case key.
3. Per logical input codepoint: `cp,found,advanceFP,top;` from the production
   `getCodepointMetrics`; `advanceFP` is in sixteenths of a pixel. `found` may
   refer to the replacement glyph, as in the production API.
4. When drawing: helper-entry placements `cp,x,y,fontId,style;` followed by
   `|BW-hash|LSB-hash|MSB-hash`. Placements include missing-glyph attempts,
   combining marks and ligature-substituted codepoints; x/y are the glyph
   cursor/baseline before bearings and scaling. Hashes are unsigned 64-bit
   FNV-1a of all 48,000 framebuffer bytes. BW starts at 0xff (white);
   overlay grayscale planes start at 0x00 (no update), matching the reader.

The archive stores SHA-256 of complete ordered record lines per configuration
and record counts, plus detailed tracking-0 drawing samples for every synthetic
script/style/font/direction and fallback. Large matrices are hash-only; keep a
baseline `records.tsv` to obtain the first differing row. `check` reports changed
groups and saves `actual.json.gz` on failure. Input font/EPUB hashes and the corpus
hash are also checked. `inventory.json` records source font hashes and capture
provenance; generated `.cpfont` hashes are in the archive.

- T1: compare bidi/Arabic visual codepoints and positions in drawing samples,
  especially niqqud, harakat and Lam-Alef; also keep the dedicated bidi tests.
- T2: compare advance/width/space/kern/codepoint fields for the synthetic matrix.
- T3: compare all built-in and SD font groups; TTF remains a callback control.
- T4: compare `words/` and `spaces/` groups for layout-facing numbers. Retain
  section goldens and real-font emulator screenshots for end-to-end parity.
- T5: compare glyph placements and all three framebuffer hashes. If the port
  removes the two rendering helpers, adapt only the observer anchors to the
  new glyph emission point, preserving this schema and the frozen archive.

`instrument.py` creates copies in the build directory. It inserts an observer
at entry to `renderCharImpl` and `renderCharScaled`, and exposes the private font
resolver in the copied header. It checks anchor counts and otherwise keeps the
production source intact. It never edits the production files. The observer
records values only; font lookup, shaping, kerning, glyph walking, decompression
and pixel writes still execute the production code.

## Additional drawing paths

[DRAWING.md](DRAWING.md) inventories the production glyph walks and documents the
`TextDrawGolden` check. It adds rotated text, direct scaled codepoints, white
text, four orientations, edge/UI/strip clipping, absolute gray planes and
per-word offsets from public EPUB excerpts. Its separate format-1 `draw.json.gz`
archive leaves every original T0 record and the original archive unchanged.

## Regenerate public fonts

Install the dependencies from `lib/EpdFont/scripts/requirements.txt` in a Python
virtual environment, then run:

```sh
python test/text_golden/generate_fonts.py
```

This calls `fontconvert_sdcard.py` with public source fonts already in the repo:
Noto Sans uses `reading`; Arabic uses `ascii,arabic`; Hebrew uses `ascii,hebrew`.
All available source styles are included at 14 pt. The capture used FreeType
2.13.2 and fontTools 4.63.0. Conversion versions can change rasterization; normal
checks use the frozen generated fixtures and need no Python font packages.
The three OFL notices are beside the fixtures. Gzip timestamps are zero.
