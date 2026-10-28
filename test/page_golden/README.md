# Real-font page oracle

This freezes pages built from production sources at `9738db01`, with SDK
`37d272d42732c31065ebed4d9cb90cef7bfa7416`. Use it when changing page assembly,
loading, queries, glyph pixels or page rendering. The public archive covers every
page in every spine item of all 18 `test/epubs/*.epub` books, plus a short public
chapter in `fixtures/`. No private font or book data is committed.

## Build and check

From the repository root:

```sh
git submodule update --init --recursive
cmake -S test/page_golden -B build/page-golden -DCMAKE_BUILD_TYPE=Release
cmake --build build/page-golden -j 6
python3 test/page_golden/run.py check --work-dir build/page-golden/check
ctest --test-dir build/page-golden --output-on-failure
```

The standalone build needs CMake, Python 3.9+, the repository Rust toolchain,
and a C++20 compiler. It needs neither PlatformIO nor image decoder downloads.
The full host suite registers `PageGolden` and `PageGoldenComparator` as well.
Use a different build/work directory for concurrent runs.

`capture` refuses if tracked production `lib/`, `src/`, `rust/` or the SDK differ
from `9738db01`; it also checks the actual SDK HEAD and SDK working tree. Build
from those sources before capture. A check deliberately allows production changes.
Never recapture to accept a port's changed output.

```sh
python3 test/page_golden/run.py capture --work-dir /tmp/page-capture
python3 test/page_golden/run.py capture --golden /tmp/page-repeat.json.gz --work-dir /tmp/page-repeat
cmp test/page_golden/golden.json.gz /tmp/page-repeat.json.gz
```

The two gzip archives are deterministic, including their zero timestamps. A
mismatch writes `actual.json.gz` in the work directory and names the first changed
page and observation channels. Full canonical `records.tsv`, extracted chapters,
section caches, stderr logs, and sample PBM planes remain under `case-NNNN/`.
`summary.json` reports counts, wall-clock duration and sizes. Without `--work-dir`,
a temporary directory is removed at exit.

## Production boundary

The executable compiles production Section, Page, TextBlock, ImageBlock,
ChapterHtmlSlimParser, ChapterPageAssembler/Services, Paragraph and CSS code,
linked to the normal Rust host archive. Measurement and rasterization use real
GfxRenderer, EpdFont, EpdFontFamily, SdCardFont, FontDecompressor,
FontCacheManager and TtfEpdFont/SDK FreeType code. No text measurement is stubbed.

The host storage adapter reads/writes ordinary files. OPF/ZIP discovery reuses
`section_golden.run.prepare`; the Epub stub supplies those extracted spine, TOC,
language and CSS inputs to production Section. The package parsers themselves
remain covered by `package_golden`. The display adapter reuses text_golden's
800×480 framebuffer. Host heap reports are fixed at 200 KiB free/largest block;
these are not firmware resource measurements.

Built-in Noto Sans 12/14/16/18 and Ubuntu 8/10/12 are registered with their real
style fallback rules. The Noto faces include group-compressed bitmaps, served by
the real decompressor. Built-in CSS size resolution calls the production
`builtinReaderFontVariant`. Public SD uses the frozen, OFL-licensed Noto Sans 14
fixture from `test/text_golden/fonts/`; its production loader and prewarm paths
run normally. SD font discovery, variant loading and eviction are not modeled.
A requested unavailable SD size follows production's existing base-font fallback.

Image dimension probing is real. Image pixel decoder lookup returns null, and
lazy extraction is not installed. `Page::render` therefore exercises production
missing-image/failure placeholders; image pages also run
`renderWithImagePlaceholders` separately. Real image decode/cache pixels remain
an `image_golden` and emulator gate. This oracle does not claim real image pixels.

## Matrix

Each public chapter is built four times. Viewports subtract the margin on both
sides of the logical panel; the physical framebuffer remains 800×480.

| Config | Body font | Layout orientation | Margin | Line compression | Alignment | Other differences |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Noto Sans 12 | Portrait | 12 px | 1.0 | Justify | Hyphenation/focus off, indent 2 |
| 1 | Noto Sans 18 | Landscape CW | 32 px | 1.0 | Left | Hyphenation/focus on, extra paragraph spacing, indent 2 |
| 2 | Ubuntu 12 | Portrait inverted | 12 px | 1.25 | Center | Hyphenation on, indent 0, tracking +1, word spacing 125% |
| 3 | SD Noto Sans 14 | Landscape CCW | 12 px | 0.85 | Right | Hyphenation on, indent 2 |

Embedded styles are enabled. Each loaded page is rendered in **all four**
orientations, both overlay and absolute gray modes, BW/LSB/MSB, and full-frame
or assembled physical strips of 31, 80 and 127 rows. This includes the short
last band. Reorienting a cached page without relayout deliberately tests edge
clipping; the four layout configurations also cover normal oriented viewports.

A real FontCacheManager scan/prewarm scope precedes drawing. BW starts white;
overlay planes start at zero; absolute planes start white after activating the
absolute base. Every strip has 16-byte guard regions, and the inactive framebuffer
must remain untouched. These are checked assertions, not just frozen hashes.
The assembled strip result is frozen independently from full-frame output; no
assumption that the old implementation makes them equal is introduced.

The supplemental chapter exercises all five element tags, ruby, mixed font sizes,
small caps, SUP/SUB, decorations, soft hyphens, focus splitting, links, footnotes,
RTL/combining text, borders/backgrounds and drop caps. Public Noto lacks CJK and
some RTL coverage, so those scripts include replacement/missing-glyph behavior.
Private SD runs add actual Korean/CJK glyphs. Public samples do not prove every
allocation-failure, cache-pressure or font-eviction schedule.

## Record format

`golden.json.gz` is a format-1 JSON document containing input hashes and cases.
Each case records spine paths, chapter page counts, coverage counts, and every
page's observations. A page key is `spine-index/page-index`. The comparator checks
input identities, complete page sets, page bytes, semantic/query records and pixels.

Each raw `records.tsv` page has these ordered records; strings are UTF-8 hex:

- `raw`: bytes cut from the section's page LUT boundaries, SHA-256 in the archive.
- `wire`: bytes reserialized after `Section::loadPage`/`Page::deserialize`, with a
  separate SHA-256. This distinguishes write-path changes from loaded data changes.
- `page`: visible text offset; element/footnote/link counts; image queries and union bounds.
- `element`: tag, x/y, and the exact serialized element payload. `element-fields`
  decodes non-line payloads through the production section codec: rule x/y/width/
  thickness; drop-cap x/y/font/scale/style/text length and text; border x/y/width/
  height, four width/style pairs and shade flag. Signed fields retain their codec
  u32 representation. Images record both paths and width/height.
- `line`: validity, word count, block font, line/extra ascent and ruby presence.
  `style` lists all BlockStyle fields in header order, including nonserialized
  layout fields restored to their default values.
- `word`: index, text, byte length, x, style, small-caps flag, discretionary-hyphen
  flag, font ID, focus byte boundary/suffix x, source start/end, paragraph-start
  flag and word y offset. `ruby` retains every loaded ruby vector entry.
- `footnote` and `link`: complete strings and page link geometry. Layout-only
  TextBlock LinkSpans have already moved into Page links before serialization;
  loaded blocks do not retain them.
- `query`: selection ordinal; x/y/width/height, row, original page word ordinal,
  resolved font, source range, paragraph-start and hyphen flags; three hit-test
  results at center, inclusive slop corner and exclusive right slop edge.
  `selection` exercises the production clipping append helper, including removal
  of discretionary hyphens. Query construction mirrors the uncapped dictionary
  lookup path in ClipSelectionActivity, with RTL row reversal and visible-word
  filtering. The interactive activity, 240-word reader cap, scrolling and input
  state machine are not executed.
- `pixels`: orientation, placeholder path, absolute flag, strip height and plane,
  followed by FNV-1a-64 of all 48,000 assembled framebuffer bytes.
- `calls`: native word-draw entries, glyph-miss callbacks, direct scaled-codepoint
  entries, page-draw entries, font-load attempts, bitmap-source selections,
  fallback residency requests, scan batches, image callbacks and font-family
  descriptor requests, metric misses, coverage misses, kerning misses and bidi
  acquire/release calls per render case, summed across bands. The final twelve
  values repeat the page/owner counters for that orientation’s prewarm scan. These are diagnostics:
  `check` permits count changes because G2/R1 intentionally remove crossings.
  All page/pixel/record observations remain strict.

The archive retains every pixel hash and SHA-256 of every full ordered semantic/
query record stream, plus up to 40 diagnostic lines from the first page of every
chapter. Full records are in the work directory, not discarded before hashing.
The public archive is capped at 1.5 MB. PBM files for the first page of the first
chapter retain physical BW/LSB/MSB planes; overlay plane pictures visualize bits,
not composite gray luminance.

`instrument.py` makes build-directory copies of GfxRenderer, Page and TextBlock.
Only the renderer receives observers: text_golden's host-only Rust placement
observer, plus native draw entry and C++ bitmap-miss callback counters. The
frozen archive counts the original per-glyph emissions; candidate counts measure
miss callbacks after glyph integration. Page/TextBlock copies are unchanged and
exist for mutation testing.
The page harness also asserts that Rust makes no allocation request during each
scan or draw. This does not count allocations made by existing C++ font/image
owners. Scan batches preserve codepoint order within each line/font/style and
use a bounded buffer. Cached and rich pages use the Rust page entry; plain
in-memory dictionary pages and standalone settings previews keep TextBlock's
existing render path.

The script checks hook counts. If a later port removes an observer anchor, adapt
the observer without changing the record schema or frozen behavior archive.

## Mutation checks

Use a dedicated build directory; the script edits only its generated copies and
restores/rebuilds them even on failure. It removes the instrumented object files
before each build to avoid filesystem timestamp reuse. It compares the supplemental case against
the **committed** archive through the same `check` command.

```sh
python3 test/page_golden/mutation_check.py build/page-golden /tmp/page-mutations
```

It proves rejection of a single framebuffer pixel flip at the draw observer, a loaded word x increment,
a dropped loaded footnote, and one serialized byte changed in ignored footnote
padding. The pixel experiment also requires exactly one changed bit in its PBM;
semantic/raw data must stay identical. The padding experiment requires identical
semantic records and pixels, so raw-byte hashing independently detects it.
`proof.json` records the affected pages/channels; restored output must pass.

For focused debugging only:

```sh
python3 test/page_golden/run.py check --case synthetic/public/0 --work-dir /tmp/page-focus
```

An unknown case fails. Capture cannot select a subset. CTest always checks the
complete public matrix.

## Private runs

Both the archive and working directory must be outside the repository. Repeat
`--private-epub` for each book; every unique `.cpfont` under the font directory is
paired with every book, using configuration 3 and the same full render matrix.
Do not put private excerpts, record files or fonts in this directory.

```sh
python3 test/page_golden/run.py capture \
  --private-epub /external/book.epub --private-font-dir /external/fonts \
  --golden /external/page-private.json.gz --work-dir /external/page-capture
python3 test/page_golden/run.py check \
  --private-epub /external/book.epub --private-font-dir /external/fonts \
  --golden /external/page-private.json.gz --work-dir /external/page-check
```

TTF code is linked to preserve the real renderer, but this page matrix does not
register TTF faces or claim TTF support on C3. Keep text_golden's TTF tests, existing
section/package/image tests, and emulator/device evidence for their separate
contracts. No shipped firmware or firmware build configuration changes here.

## Strip-rendering host spike

`STRIP_SPIKE` enables a host-only Rust entry point (`strip-spike` Cargo feature).
The firmware build does not enable it. The normal oracle and its frozen archive
remain unchanged. This mode adds byte-for-byte comparisons against the current
full-frame draw for native row heights 1, 8, 16, 32, 37, 48, 64 and 480, in all
four orientations and BW/LSB/MSB, with both overlay and absolute planes.

```sh
cmake -S test/page_golden -B build/strip -DCMAKE_BUILD_TYPE=Release \
  -DSTRIP_SPIKE=ON \
  -DXP_FFI_HOST_FEATURES=chapter-test,chapter-trace,package-trace,strip-spike
cmake --build build/strip -j 6
python3 test/page_golden/run.py check --binary build/strip/PageGolden \
  --strips --work-dir /external/strip-public
python3 test/page_golden/strip_cost.py /external/strip-public /external/strip-cost
```

Use the private-run arguments above to check the private books and fonts against
their frozen archive. Keep those outputs outside the repository.

The preparation pass uses only a guarded one-row buffer and must leave it and
the inactive framebuffer untouched. It writes a caller-owned physical row range
for each page element (8 bytes per element). Rendering skips nonintersecting
records before word/glyph walking; the existing rasterizer clips resident glyphs.
Missing glyph metrics and drop caps conservatively retain the whole row range.
Rebuild bounds after any page, font, orientation or origin change. Bounds cover
all planes and can be reused for the X3 old-frame copy.

To exercise real image pixels, add `-DSTRIP_IMAGES=ON` and
`-DDECODER_SOURCE_DIR=/absolute/path/to/.pio/libdeps/default` when configuring a
separate build directory. Run with `--strips --decoded-images`. This installs a
host file extractor and the production PNG/GIF/JPEG converters, decodes once,
and compares every strip to the current full-frame cache playback. The ordinary
page archive contains placeholders, so decoded-image mode checks schedule/page
coverage and exact strip/full equality rather than comparing image pixels with
that archive. The independent image oracle checks decode output. A `.px2` cache
contains dimensions followed by packed 2-bpp luminance rows; the new target skips
off-band rows/columns before unpacking. Existing C++ cache I/O is unchanged.

`cost` records contain orientation, placeholder flag, absolute flag, height and
plane, followed by host wall microseconds, preparation microseconds, record
checks, records dispatched, glyph placements visited, preparation record checks
and preparation glyph placements. Full-frame baseline counters use zero because
that path is uninstrumented; preparation counters provide its traversal count.
Scaled drop-cap glyphs are excluded from glyph-placement counters. Timing includes
buffer clear, assembly and guard checks, but strip storage is allocated once per
plane before timing. Prewarm and image decode are outside timing. Ratios are host
estimates, not device cycle measurements; concurrent host load adds noise.

The CSV retains every page/orientation/plane. The summary selects overlay BW
without placeholders and excludes references below 20 microseconds from timing
ratios. It reports preparation separately and estimates two draws as
`(preparation + 2 * strip_draw) / full_draw`; SPI transfer and panel refresh time
are not included in that estimate. Physical X3 (792×528), X4 (800×480), and odd
widths also have direct target tests in `xp-core::strip::tests`.
