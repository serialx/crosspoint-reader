# Text measurement differential oracle

Build and run independently:

```sh
cmake -S test/textm_diff -B build/textm-diff
cmake --build build/textm-diff -j 6
ctest --test-dir build/textm-diff --output-on-failure
```

`extract.py` compiles the measurement method bodies from baseline `c9330408`.
`EpdFont`, `EpdFontFamily`, `SdCardFont`, UTF-8 helpers and MiniBidi are real
repository implementations. The renderer shell substitutes only registration
and SD prewarming for hardware ownership. Both measurement paths receive the
same visual string, prepared once using the baseline bidi byte-scan policy.
This tests measurement rules, not framebuffer drawing or bidi parity.

The public fixture combines Noto Sans 12 (four variants), Ubuntu 12 (style
fallback), and a generated version-4 SD font with fractional metrics and
partial coverage. It exercises every style byte, five tracking values including
both extremes, layout/render modes, three base directions, missing font IDs,
partial/full/absent SD advance tables, fixed corpus strings and 150 deterministic
generated strings. Pass an external `.cpfont` path as the first argument to use
private fonts without copying them into this repository.

The pre-port public baseline records 15,759,360 advance cases and FNV-1a digest
`0fb1ef2d6653c9b2` over little-endian integer observations. Private fixtures have
different digests. Generated data and executables remain in the build directory.

## Rust contract and integration

`xp-core::textm::FontFace` borrows one registered font family. It separates
coverage and table reads (no storage I/O) from `glyph` and `kerning` (which can
load SD glyphs or mutate a TTF cache). Glyph metrics are returned by value so
an overflow-ring eviction cannot invalidate an earlier glyph. `glyph` must
implement raw `EpdFont::getGlyph`, including replacement and turned-comma
fallback; `textm::glyph_metrics` adds the solid-block synthesis. Pair ligature
lookup must retain the Arabic presentation-form guard.

Resolve the font with `FontRegistry` on the original logical codepoints, then
shape/reorder once and pass a borrowed visual codepoint slice. The
`text_advance_x` predicate is the bidi provider's `isTransparentMark`; production
uses the native Rust bidi predicate. SD preparation and redirected-font prewarm
remain with the existing owner. T3 table loans must end before any miss handler
can rebuild their storage. Do not keep an immutable table loan across a mutable
glyph lookup.

Layout with an SD advance table accumulates fixed-point values and rounds once.
The general path rounds each previous advance plus the next kerning value.
Width measures ink bounds and intentionally does not apply the advance path's
small-caps/sup/sub scaling or transparent-mark exclusion. Space measurements
also preserve their separate rounding and SD style-resolution rules.

The `xp_textm_*` C ABI is a host comparison adapter. Production uses native
tables and retains callbacks for dynamic providers; see the
[native ownership contract](../../docs/contributing/rust.md#native-text-measurement).
Rust measurement allocates no storage; the test rejects every Rust allocation
attempt. The existing C++ bidi string allocation is outside this seam; production
measurement uses the shared caller-owned bidi scratch. The measurement-rule seam
has no fixed input limit; bidi retains its 128-codepoint compatibility limit.

`SideEffects.h` compares independent SD owners, including cold overflow-ring
loads and cache churn, and checks both results and storage-read counts.
`Edges.h` covers missing glyphs, absent/zero-width M fallback, negative/zero
ascenders, synthetic size clamping, and empty input. The ABI leaves codepoint
outputs unchanged when the glyph/font is absent.

## Additional corpus and cost measurements

An external UTF-8 corpus uses one hex-encoded string per line:

```sh
build/textm-diff/TextmDiff --corpus /absolute/path/corpus.hex
```

This runs the 16 base/small-caps/sup/sub variants, both modes, AUTO direction,
and zero tracking across three SD cache states. The built-in corpus retains
all 256 styles, three directions and five tracking values. T0's corpus can be
exported using its `run.py` `SYNTHETIC` and `corpus()` inputs; it is not a build
dependency of this test.

The host benchmark measures the already-visual word `office`, Noto Sans 12,
regular style, layout mode, zero tracking. UTF-8/codepoint preparation is
outside the loop. The C++ side still resolves its font map; the Rust side uses
the callback face with counters enabled. These are comparison-adapter costs,
not a prediction for T3's native tables or the device's flash cache.

```sh
build/textm-diff/TextmDiff --bench-cpp 5000000
build/textm-diff/TextmDiff --bench-rust 5000000
xctrace record --template 'CPU Counters' --output cpp.trace --launch -- \
  "$PWD/build/textm-diff/TextmDiff" --bench-cpp 5000000
xctrace export --input cpp.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="MetricTable"]' \
  --output cpp-metric-table.xml
python3 test/textm_diff/counters.py cpp-metric-table.xml 5000000
```

Repeat with `--bench-rust`. Instruments includes process startup in the cycle
sum; it is amortized over five million words. The callback count printed by
the benchmark excludes the separate transparent-mark predicate.

Count the callbacks made by the production paragraph layout engine with:

```sh
build/textm-diff/TextmDiff --layout-count
```

This lays out 100 paragraphs of 20 source words for each width/focus/hyphenation
configuration. It counts repeated prefix/suffix measurements and space/kerning
queries, rather than assuming one callback per source word. Its ASCII workload
uses the real built-in font; it does not represent a whole EPUB or SD workload.

## Native paragraph lifetime comparison

`NativeParagraph` compares the native provider with the callback backend using
`FrozenRenderer`, which compiles the pre-T4 C++ methods from `c9330408` through
`extract.py`. Each backend gets fresh SD and TTF owners. The paragraph engine is
identical on both sides; measurements come from independent implementations.
Unlike `TextmDiff`, this comparison includes each backend's bidi preparation.

The public Noto Sans SD/TTF fonts and two built-in families cover focus on/off,
hyphenation on/off, 80/240-pixel widths and missing U+10FFFF glyphs. It compares
packed line text, positions, styles, focus boundaries/suffix widths, source
ranges, paragraph offsets and font slots. Assertions require actual splits,
focus output and all four font families. A persistent-engine measurement
sequence cycles three stable families through the two-entry descriptor cache,
then measures TTF and SD again after eviction.

Both backends wrap the real regular TTF glyph-miss handler to release the
prepared SD resident cache during that callback. The native engine remains
alive across the callback and subsequent SD reads; assertions verify eviction
and later descriptor refresh. This is synchronous owner mutation under memory
pressure, not a concurrent-access test. The host memory-manager stub does not
simulate allocator exhaustion.

```sh
build/textm-diff/NativeParagraph build/textm-diff/native.cpfont \
  lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf
```

To instrument both languages on macOS, use the installed Homebrew LLVM compiler
and Rust sanitizer support (a separate build prevents mixing archives):

```sh
cmake -S test/textm_diff -B build/native-asan-llvm \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
RUSTC_BOOTSTRAP=1 RUSTFLAGS=-Zsanitizer=address \
  cmake --build build/native-asan-llvm --target NativeParagraph -j 6
UBSAN_OPTIONS=halt_on_error=1 \
  build/native-asan-llvm/NativeParagraph build/native-asan-llvm/native.cpfont \
  lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf
```

Rust and C/C++ use ASan; C/C++ also use UBSan. The original `NativeCache` test
separately covers explicit unregister/reload, fallback changes and cache rebuilds.
