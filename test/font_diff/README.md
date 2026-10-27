# Font table differential tests

`FontDiff` compares allocation-free Rust readers with the production EpdFont and
SdCardFont implementations. It visits all 21 built-in font/style tables: every
interval codepoint, interval boundaries and absent codepoints, every left/right
kerning-table pair (including zero pairs), ligatures and chains, malformed UTF-8
lookahead, and solid-block fallback metrics. The 25,048 glyph cases, 5,481,418
kerning pairs and 756 ligature sequences have field-based hashes captured before
the port at c9330408 in `builtin-oracle.txt`; padding is excluded. The CTest
`FontBuiltinGolden` verifies that stream separately from direct Rust comparisons.

```sh
cmake -S test/font_diff -B build/font-diff
cmake --build build/font-diff -j 6
ctest --test-dir build/font-diff --output-on-failure
build/font-diff/FontDiff /path/to/public.cpfont /path/to/private.cpfont
```

The default test generates a public synthetic four-style `.cpfont` v4 in the
build directory, with zero advances, dense kerning, ligature chains, replacement
and quotation glyphs, and a non-BMP interval. Additional files are supplied
explicitly and never copied into this directory. To generate a real public font,
use `lib/EpdFont/scripts/fontconvert_sdcard.py` with the repository's NotoSerif
sources and `--size 14 --intervals latin-ext`; its normal Python dependencies are
required. Private font files and their results belong outside the repository.

The existing host storage stub lends file bytes to the real SdCardFont loader.
The test prepares advances using `buildAdvanceTable`, the operation called by
`GfxRenderer::ensureSdCardFontReady`, then prewarms resident glyph/kern tables,
clears caches and releases resident storage. At each stage, every Unicode
codepoint is checked for coverage, advance presence/value and glyph residency.
The Rust scan must perform zero storage reads. Every C++ interval miss is
reported as `NotResident`, including an overflow-ring hit: the ring is accessed
through the C++ miss handler and remains outside the Rust view. Every Rust
allocation attempt fails during the tests, and the attempt count must stay zero.
This tests font preparation directly, not GfxRenderer's bidi/token preparation;
that integration belongs to the text oracle and the subsequent measurement port.

Test-only private-member inspection exposes resident advance and coverage tables.
Production code, allocation policy and cache visibility are unchanged. The native
view borrows the resolved style's current EpdFontData, sorted AdvanceEntry array,
and full coverage intervals (packed BMP or full-width). Its EpdFontData already
contains the resident mini kerning tables and loaded ligatures; the full on-disk
kerning matrix is never retained. Views must end before *any* cache mutation,
including advance growth, prewarm, eviction, font unload or a glyph miss callback.
Reconstruct the view afterward. C++ integration will need a scoped accessor for
these private tables; the test bridge is not a production accessor.

`Font::advance` preserves a cached zero as `Some(0)`, distinct from `None`.
The current renderer uses zero to trigger full metrics for non-combining glyphs,
and checks `hasAdvanceTable()` across all styles before selecting that path.
Neither rule should be inferred from presence in a single style's view.
Kerning and ligatures read exactly the current EpdFontData, including empty mini
kerning on metadata-only preparation. No attempt is made to infer on-disk pairs.

`Layout.h` asserts every field offset, size and alignment in the production
EpdFontData, glyph, interval, compression-group, packed kerning and ligature
structures. `SdLayout.h` checks the private advance/BMP layouts and the test ABI.
Matching Rust const assertions compile for 32-bit and 64-bit pointer widths.
To check the C3 side without firmware changes:

```sh
~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++ \
  -std=c++20 -march=rv32imc -mabi=ilp32 -Ilib/EpdFont \
  -Irust/crates/xp-ffi/include -c test/font_diff/Abi.cpp -o build/font-diff/abi-c3.o
```

The FFI is a differential-test surface and has no firmware caller. The Rust
inherent methods expose raw 12.4 advances, 4.4 kerning, glyph values, coverage,
ligatures and line metrics through `Font::data()`. Loading, bitmap decompression,
style selection, rendering and caches remain in C++.

## TTF integration boundary

`TtfEpdFont::setupFace` sets line metrics, glyph/coverage/bitmap/kerning handlers
and a small GSUB-derived ligature table; it does not publish intervals or a
resident glyph array. Faces initialize lazily, and their ligature table changes
when the first fault initializes a style. Its glyph vectors, slot indices and
kerning cache are private and may move or flush during a fault. A stable native
resident view is therefore not available through the current EpdFontData.

Use a callback face for TTF in the measurement integration. Generic Rust lookups
return `NotResident` for glyph misses and `None` for dynamic coverage or kerning;
they never invoke those handlers. CJK kerning still returns zero before the
handler, matching EpdFont. T4 must release the borrowed view before faulting and
read current metrics/ligatures again afterward. This task does not implement a
TTF adapter or prove runtime TTF measurement parity.
