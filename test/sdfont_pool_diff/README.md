# SD font cache operation traces

`reference/SdCardFont.{h,cpp}` freezes the cache implementation from `da609bbf`.
Both executables link the same production font accessors and UTF-8 helpers.
The reference is test-only and must not track production cache edits.

```sh
cmake -S test/sdfont_pool_diff -B build/sdfont-pool -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdfont-pool -j 6
ctest --test-dir build/sdfont-pool --output-on-failure
```

The runner unpacks the three existing public fonts into a temporary directory and
adds synthetic four-style, kerning/ligature, and above-BMP fixtures.
Use `--private-font-dir PATH` for an additional directory of private `.cpfont`
files, `--texts PATH` for UTF-8 text lines, and `--save PATH` to retain compressed
traces outside the repository. Private fonts and texts must stay outside Git.

Each operation records return values, glyph metrics and bounded bitmap bytes,
resident kerning/ligature tables, pair lookup results, coverage, style resolution,
advance lookup, and statistics. Separate rolling hashes record ordered SD seeks
and reads (offset and requested length), and array allocation/free events (request
ID, size and free order). Live, peak and largest requested array bytes are printed.
These are host ABI allocation requests, not ESP allocator block sizes; scalar
HAL allocations and allocator metadata require separate firmware measurements.

The sequence exercises metadata/full prewarm, kern top-up, subset hits, incremental
unions, getter batches with null entries, packed small-caps advances, persistent
cache clearing, overflow beyond eight slots, cache release/rebuild, and two fonts
sharing intervals. Every allocation in the successful sequence is failed once in
an independent replay. First-glyph bit mutations and file truncations exercise
partial reads and corrupt-glyph fallback. Small files truncate at every byte;
larger files truncate through the first two glyph records. The timer stub returns
zero, so timing is checked on the emulator rather than inferred from these traces.

The harness intentionally captures inherited behavior. It does not prove every
failure schedule, all style combinations, or hardware file-open failures. Run
with AddressSanitizer and UndefinedBehaviorSanitizer to check the C++ reference
and adapter; Rust bounds and allocation failures also require their own checks.

Zero-length resident glyph offsets are compared as zero: unstable sorts can place
an empty glyph before or after another glyph at the same file offset. This field
selects no bitmap bytes. All nonempty offsets, reads and allocations remain exact.
`SDPOOL_EXACT_ZERO_OFFSETS=1` restores raw offset comparison; `SDPOOL_BASE_ONLY=1`
runs only the initial valid-font sequence for auditing this exception separately.

`SDPOOL_USAGE=1` counts calls through the existing glyph-miss facade while looking
up each input text after full prewarm, per text and available style. Text lines
are corpus windows, not a replacement for paginated emulator measurements.
