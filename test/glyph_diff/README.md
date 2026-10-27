# Glyph raster and bitmap-source differential

`GlyphDiff` compares `xp_glyph_draw` with production `GlyphBitmap.h` and the
GfxRenderer glyph helpers. `instrument.py` exposes private members in build
copies and appends a wrapper around the production static helpers. It does not
replace their arithmetic or pixel writes. The firmware still uses C++ drawing.

```sh
cmake -S test/glyph_diff -B build/glyph-diff -DCMAKE_BUILD_TYPE=Release
cmake --build build/glyph-diff -j 6
ctest --test-dir build/glyph-diff --output-on-failure
```

The default run visits every codepoint of all 21 built-in faces and every style
of the three public SD fonts already stored under `test/text_golden/fonts`.
It unpacks those files into the build directory. No new font binaries are stored
here. The full matrix takes a few minutes. `GLYPH_DIFF_QUICK=1` selects one
clip/strip configuration per glyph and retains the synthetic edge matrix for
local debugging; it is not the complete acceptance run.

Every public glyph crosses four orientations, normal/rotated placement, three
planes, both states, both absolute-plane settings, nine positions (panel and
logical-clip edges), and three row bands. Each also exercises 1/2 and 3/4
coverage reduction and 384/256 bilinear scaling in every orientation.
Synthetic cases cross packing widths 0/1/3/7/16/31, both source bit depths,
coverage ratios 1/2, 3/4 and 1/1, and bilinear scale256 values
64/128/192/255/256/257/384/512/768/1024/4096. These scale cases cross the same
orientation/plane/state/absolute/edge/band dimensions. Solid fallback and scan
suppression use the real production fallback metrics and fillRect.

Panel tests cover 40x32, 63x47 with a padded 9-byte stride, 800x480, and 792x528.
Every comparison checks full buffers, nonuniform backgrounds, guard bytes and
inactive targets. Strip intersection results are compared separately.
The Rust allocator is forced to fail during each draw and bitmap lookup, and
its allocation-attempt count must remain zero.

Bitmap checks prewarm production FontDecompressor and SdCardFont in batches,
then compare resident pointers and lengths with GfxRenderer::getGlyphBitmap.
U+0000 cannot enter the null-terminated prewarm input; it correctly takes the
compressed-font miss path. SD overflow glyphs deliberately return a miss, even
when C++ already has their bitmap. Each source check also poisons font, glyph
and page-table pointers after changing generation to prove early rejection.
Rust unit tests cover truncated raster inputs, invalid descriptors, missing and
partial page slots, first-slot precedence, and stale generations.

`Abi.cpp` and Rust const assertions check 32-bit and 64-bit layouts. The C3 check:

```sh
~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++ \
  -std=c++20 -march=rv32imc -mabi=ilp32 -Ilib/EpdFont \
  -Irust/crates/xp-ffi/include -c test/glyph_diff/Abi.cpp -o build/glyph-diff/abi-c3.o
```

## Integration contract for G2

Fill `XpGlyphTarget` once per synchronous draw with the current write pointer,
physical width/height/stride, strip origin/rows, logical half-open clip,
orientation, plane, absolute-plane state and scan state. The target and input
bitmap must not alias. `XpGlyphDraw` carries copied glyph metrics and the
requested algorithm; `solid` selects the existing rectangle fallback. Bilinear
drop caps always paint black, as production does. Invalid descriptors return 0
before painting; valid empty or clipped draws return 1.

`XpGlyphSource` borrows the current EpdFontData and sorted FontDecompressor page
slots. PageEntry and PageSlot mirror the native layouts, avoiding a new table
or per-glyph allocation. C++ supplies resident glyph count and bitmap byte
extent. Native page slots have no length field: their extracted offsets must
refer to valid glyph-sized ranges in the page allocation. This is a trusted
resident-view contract, not an input-file parser.

Use the existing serialized font-table generation rule. A generation change
invalidates the whole descriptor before any borrowed font or glyph is read.
**G2 must add generation increments to FontDecompressor prewarm and page-buffer
clear/free**, which currently do not increment `fontTableGeneration`. Rebuild
views after any owner callback or mutation. Do not retain a returned pointer
across callbacks; the generation check is not synchronization or a lifetime
extension. SD's overflow view identifies the first glyph address, entry stride
and live entry count, so metadata fields inside an entry are never mistaken for
a glyph. TTF and nonresident/hot-group/overflow sources return a miss; C++ owns
loading and decompression. No miss callback is invoked while a Rust loan exists.

The new code is not wired into firmware. These host tests prove pixel rules and
borrowed view behavior; they do not measure future G2 draw speed or device
waveforms. G2 still needs integrated emulator/device tests and owner review. Before deleting
or delegating the C++ helpers, preserve the pre-port reference so this differential
continues to compare independent implementations.
