# SD font cache operation traces

`reference/SdCardFont.{h,cpp}` freezes the cache implementation from `da609bbf`.
The candidate uses the frozen `7bab9148` native adapter in `test/sdfont_legacy`
and the current Rust pool through host-only ABI wrappers. Both executables link
the same production font accessors and UTF-8 helpers. The native copies are
historical compatibility oracles; they do not track shipping owner edits.

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
selects no bitmap bytes. All nonempty offsets and reads remain exact. Allocation comparison uses the growth-move normalization below.
`SDPOOL_EXACT_ZERO_OFFSETS=1` restores raw offset comparison; `SDPOOL_BASE_ONLY=1`
runs the initial valid-font sequence without mutation/OOM replays. With `--texts`,
it also records prewarm and advance results for each supplied corpus window,
including its glyph queries, reads and array allocations, then checks teardown.

`SDPOOL_USAGE=1` counts calls through the existing glyph-miss facade while looking
up each input text after full prewarm, per text and available style. Text lines
are corpus windows, not a replacement for paginated emulator measurements.

The logging stub also captures the original level, origin and formatted text.
`run.py` compares each operation's log multiset without changing the ordered
state, allocation or I/O records. `SDPOOL_IO_FAILURES=1` additionally fails every
open, seek and read in the successful operation sequence, one at a time; the
existing OOM and mutation replays remain enabled. This can produce millions of
records for a font with many intervals. The historical adapter callback is nullable and
must not reenter the Rust cache.

For bitmap growth, `RelocationTrace.h` observes the candidate's existing private
slots with test-only access control disabled; no hook is added to the Rust pool.
When an old bitmap is freed with a larger glyph payload ready, the observer
requires advance, mini interval and mini glyph moves in that order, skipping
absent, zero-capacity or over-8,192-byte tables. It checks the new allocation's
capacity, initialized fields before the old table is freed, and the published
pointer before the next event. A failed move must keep the old pointer.

Only these verified move allocation/free pairs are removed. Their replacements
inherit the old table's logical allocation ID. Every other allocation/free event
is emitted and compared in order, as well as the rolling allocation hash and
logical request count. Actual live, peak and largest requested bytes include the
moves and remain exact comparisons, alongside state, I/O and diagnostic logs.
Missing, extra or reordered ordinary events fail; an invalid move fails inside
the observer. `--save` keeps both raw traces and per-case move records.

Historical OOM positions use the logical IDs, so added move requests cannot shift
them. Each successful baseline move also gets its own failure replay against the
reference baseline. `SdFontResidentGrowth` separately exercises the production
owner with a populated advance table and all combinations of failed moves.
