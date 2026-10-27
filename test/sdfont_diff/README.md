# SD font parser differential

The `oracle/` files are byte-exact copies of `SdCardFont.{h,cpp}` and
`SdCardFontRegistry.{h,cpp}` from `9738db01f107a67f5ee392a45b26f97e90c65e06`.
They compile only into host oracle executables. Production candidates use the
current C++ adapters and Rust archive. Do not update the oracle to accept a port.

Run from the repository root:

```sh
cmake -S test/sdfont_diff -B build/sdfont-diff
cmake --build build/sdfont-diff -j 6
ctest --test-dir build/sdfont-diff --output-on-failure
```

`SdFontDifferential` and `SdFontAbi` are also in the full host suite. To include
private `.cpfont` files without copying them into the repository:

```sh
python3 test/sdfont_diff/run.py --build-dir build/sdfont-diff \
  --private-font-dir /path/to/private/fonts
```

The runner decompresses the existing public Noto fixtures into a temporary
folder, adds four small synthetic fonts, and optionally finds unique private
fonts by content hash. It compares every output record from both implementations.
Each font record contains acceptance and a hash of every parsed header field,
content hash, table offset, interval value, style presence and resident interval
representation. It truncates at every byte through the 160-byte maximum header
and directory region, flips every bit there and in the first/last intervals,
and mutates counts and offsets through zero, limits, file boundaries and uint32
wrap. Small synthetic inputs also cover every-byte whole-file truncation.

A separate 589,824-case glyph matrix covers dimensions, packing, data length,
bitmap offset and file extent through the original and production
`glyphEntryValid`. Filename cases cover suffixes, separators, length limits,
ASCII whitespace/signs/leading zeros, huge numbers and byte mutations.
`SdFontAbi` also compares 131,072 kerning-class cases and 2,048 ligature
byte mutations against native C++ records. It checks structure layouts,
short/null spans, unchanged outputs after
rejection, duplicate styles and the supported in-place interval output.
The Rust unit tests exercise truncated and bit-mutated byte spans directly.

`sdalloc_oracle` and `sdalloc_candidate` additionally compare load-time array
allocation requests, peak live requested bytes, largest request, resident bytes
and release. These are host C++ array sizes, including the host ABI's array
cookie; they are not ESP heap measurements. The file buffer and corpus are
outside the measured interval. Existing SD font tests cover allocation failures,
sharing, cache lifetime, prewarm and deferred corrupt-glyph behavior.

Compatibility is deliberate: table offsets retain uint32 wrap, tables may
overlap, bitmap validation remains deferred, and a full uint32 interval wraps
its span to zero as in the original validator. The port adds no eager rejection
or disk format change. Rust takes the load-time header/directory/interval rules,
glyph extent validation, deferred kern/ligature field decoding and `.cpfont`
filename size parsing. C++ retains HAL reads, native-layout resident buffers,
sharing, cache policy, bitmap I/O, vector-font discovery, canonical path
construction, installer magic checks and ArduinoJson download manifests.

For C++ memory/undefined-behavior checks, configure another build with
`-DCMAKE_BUILD_TYPE=Debug`,
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`, then run the same tests.
On macOS use `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
This instruments C++; Rust is covered by its checked slice operations and
host mutation tests, not by these sanitizer flags.
