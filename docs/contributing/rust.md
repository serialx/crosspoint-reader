# Rust core and C ABI

The firmware links a small Rust static library. `rust/crates/xp-core` contains
portable `no_std` code and host tests. `rust/crates/xp-ffi` owns the C ABI,
ESP-IDF allocator, and panic handler. Its public header is
`rust/crates/xp-ffi/include/xp_ffi.h`.

Install rustup, then install the pinned C3 toolchain:

```sh
rustup toolchain install 1.99.0 --profile minimal --component rustfmt,clippy
rustup target add --toolchain 1.99.0 riscv32imc-unknown-none-elf
```

Put Cargo on `PATH` (normally `~/.cargo/bin`). `pio run -e default` runs
`scripts/build_rust.py`, which builds `xp-ffi` in release mode and links it into
the firmware. Cargo outputs live under `.pio/build/<environment>/rust`.
The C3 build uses offline mode. The script never installs toolchains. Cargo
fingerprints and the archive dependency trigger incremental rebuilds and relinking.
The linker places Rust after the firmware libraries so Rust's bundled weak
math helpers do not replace the existing libm and libgcc implementations.

ESP32-S3 environments require the Espressif toolchain and `rust-src`, installed
with `espup`:

```sh
cargo install espup --locked --version 0.17.1
espup install --targets esp32s3 --toolchain-version 1.97.0.0
```

The script selects `cargo +esp` and builds `core` and `alloc` for
`xtensa-esp32s3-none-elf`. Cargo needs registry access on the first S3 build to
fetch the sysroot dependencies; `--locked` keeps their resolved versions fixed.
CI installs the same Espressif toolchain for every S3 matrix entry.

Keep rustup's Cargo on `PATH`. Sourcing `~/export-esp.sh` is not required for
this static library: it uses no bindgen, so it needs no `LIBCLANG_PATH`, and
PlatformIO supplies the GCC linker. The script does not use espup's GCC on `PATH`.

Run portable checks from the workspace:

```sh
cd rust
cargo fmt --check
cargo clippy -p xp-core --all-targets -- -D warnings
cargo test -p xp-core
```

The release profile uses `opt-level = "z"` for size, enables fat LTO, and aborts on panic.
Overflow checks retain the release default (off); hash arithmetic wraps
explicitly. Debug symbols remain in the ELF, not the firmware binary.

The boot self-check compares Rust FNV-1a and CRC-32 against C++ and miniz and
logs the ABI version and result. It borrows a constant buffer and allocates
nothing. Future Rust allocations use byte-addressable internal RAM and must
have an explicit budget and failure policy; the allocator returns null on OOM.
Rust panics call C `abort()` to enter the existing ESP-IDF panic path.

## Hyphenation boundary

`Hyphenator` keeps the existing C++ interface and stores a Rust language id.
`xp_hyphen_language` applies the existing tag and ISO-639 alias rules;
`xp_hyphen_breaks` writes ordered UTF-8 byte offsets and insertion flags into
a caller buffer. An insufficient buffer returns `NEED_OUTPUT` with the full
required count and leaves the buffer unchanged. The header documents pointer,
alignment, length, and non-overlap requirements.

The core uses no heap allocation. It streams the legacy UTF-8 decoder and
limited Latin composition rules, retaining original source offsets. Pattern
lookup keeps the existing 160-byte augmented-word and 70-codepoint limits.
Long words still receive fallback and separator breaks, without imposing a
new input limit. Two deterministic passes count and emit results at the ABI.

The C++ adapter uses 157 local `XpBreak` entries (1,256 bytes): a 160-byte word
has at most 160 codepoints and the smallest 2/2 prefix/suffix limits permit
157 breaks. It allocates the returned vector at its exact size. Inputs with
more than 157 breaks use a checked temporary buffer and retry; this buffer
coexists with the result until the call returns. No scratch or input pointer
survives the call. Language selection remains externally serialized, as in
the C++ implementation.

`test/hyphenation_eval/golden-words.txt` stores raw words as hexadecimal bytes
(`-` means empty). `golden-breaks.txt` stores tag, fallback flag, zero-based word
index, and ordered `byteOffset:insertHyphen` pairs. These outputs were captured
from the original C++ engine before its removal, with `GoldenDump.cpp`.
The default compact fixtures contain 652 words and 15,648 break lists across
12 representative language tags and both fallback flags, plus a separate
31-tag language-mapping check. They retain separator and punctuation cases,
long words, CJK, combining marks, embedded NULs, and malformed UTF-8.
Set `XP_HYPHEN_GOLDEN_FULL` to the external full archive to verify all 43,471
words and 2,695,202 break lists, covering every corpus word and all 31 tags.
Both Cargo and the host C++ suite compare every selected output exactly.
See the [golden fixture guide](../../test/hyphenation_eval/README.md) for the
selection method, archive format and commands. Do not regenerate goldens from
Rust to make a failing differential test pass.

The host CMake build compiles the same `xp-ffi` static library and links it
into the hyphenation and line-breaking suites. The existing F1 thresholds
also run through the C++ facade. `HyphenationCommon` retains only the explicit
and soft-hyphen predicates needed by `TokenBoundary`; its interface is unchanged.

## CSS

`xp-core::css` owns streaming CSS parsing, bounded selector/style pools,
matching, and v19 cache records. `CssParser` keeps HalFile I/O and the existing
`.tmp`/`.bak` cache transaction. It feeds 512-byte chunks and releases Rust
storage on `clear()`; the chapter-facing `CssStyle` layout is unchanged.

The ABI uses `XP_CSS_WIRE_BYTES` (88), checked against the C++ decoder at compile
time. All pointers are borrowed for one call; handles require exclusive access.
An empty stylesheet still needs a final feed call. Cache output emits its header
then one source-ordered record per call, ending with zero bytes written.
Cache input accepts chunks and validates completeness on the final call.

The three pools keep the original caps and doubling schedule: 1,500 eight-byte
index entries, 32,768 selector bytes, and 256 interned styles. Styles use the
88-byte wire representation in memory. All pool and scratch allocations are
fallible; lookup allocates nothing. Parsing reports Partial after storage or
input limits; cache hydration reports LowMemory so the caller can retry.

Host CMake tests build the same Rust archive with a host allocator shim. See
[test/css_parser/README.md](../../test/css_parser/README.md) for oracle provenance,
wire-byte checks, and allocation-failure coverage. The cache version remains 19.

## ZIP directory boundary

`ZipFile` keeps HalFile I/O, cached slim stats, sequential lookup position, and
last-lookup caching. `xp-core::zip` parses the directory without allocation.
The C inflater and content-protection route are unchanged.

The caller initializes a 16-byte `XpZipWalker` with the central-directory and
starting offsets. `xp_zip_request` returns a 46-byte absolute header read;
`xp_zip_next` consumes its reply and returns an entry, a wrap request, or end.
Each entry contains an absolute filename read request and the next record offset.
The caller can skip names that cannot match, and copies a name only when retained
in an existing C++ cache. Input pointers are never retained. A scan starting in
the middle wraps once and stops on reaching its original starting offset.

EOCD scanning keeps the last-1-KiB limit and searches backwards for the final
signature with room for the 22-byte record. The C++ adapter owns one checked
buffer of at most 1,024 bytes until parsing returns. Directory operations reuse
the existing 256-byte name buffer; header parsing uses 46 bytes in a helper frame.
Names of 256 bytes or more are skipped. Names match raw bytes: normalization
remains with callers. Batch hashes use the shared Rust FNV-1a function, while
C++ retains deque indexing and duplicate-target counting.

`test/zip_file/directory-golden.txt` was recorded from the C++ scanner at
`194016e7`, before replacement, using `RECORD_ZIP_GOLDENS=1` with `ZipFileTest`.
It covers every checked-in EPUB plus deterministic generated archives, including
large directories, name limits, extra fields, duplicate names, missing EOCD and
comments at the scan-window boundary. The transcript contains entry order/CRCs,
size batches, local data offsets, cursor state, and cached stats. Cached map
iteration is checked against the native map separately; its transcript list is
sorted because libc++ and libstdc++ have different iteration orders. Do not
regenerate this fixture from Rust. Run it with:

```sh
cmake -S test -B build/test
cmake --build build/test --target ZipFileTest
build/test/zip_file/ZipFileTest
```

The ABI rejects invalid null/alignment/length arguments and offset overflow;
short local headers and failed seeks/reads fail without parsing uninitialized
bytes. The compatibility corpus does not define results for the old scanner's
unchecked short reads. ZIP64 and multi-disk support are not added by this change.

## Chapter and section compatibility

The [section byte oracle](../../test/section_golden/README.md) captures the full
production Section serialization before migrating word storage or layout.
Its 312 cases cover every XHTML spine item in the 18 checked-in EPUBs under
three fixed font, viewport and setting combinations. Deterministic host metrics
make changes to word positions, source ranges, page boundaries and lookup tables
visible as byte differences. Image records use the production serializer too.

Run it before and after a paragraph-core change:

```sh
cmake -S test -B build/test
cmake --build build/test --target SectionGoldenDump -j 6
ctest --test-dir build/test -R '^SectionGolden' --output-on-failure
```

Keep the captured C++ goldens when porting code. The harness uses host adapters
for EPUB discovery, storage and font metrics; it does not replace firmware
builds, device/emulator comparisons, allocation measurements or OOM tests.
Paragraph storage and line layout are behind the Rust ABI. The paragraph-level
[layout oracle](../../test/section_golden/README.md#paragraph-layout-oracle)
checks every call against the original C++ trace as well.

`WordStore` now delegates its arena to `xp-core::para`. Its opaque state uses
four pointer-sized words inside the C++ owner, with no separate handle allocation.
The C++ allocation callback uses the existing nothrow allocator and retries once
following `MemoryManager::ensureFree(request + 8192)`. Rust returns every buffer
to the matching C++ release callback. On firmware these allocations use the
existing C++/ESP-IDF heap path; no Rust global allocation is added.

Text chunks remain 2,048 bytes, with exact-fit chunks for larger words and a
65,534-byte payload limit. Descriptors start at eight slots and double, keeping
both descriptor arrays live only during growth. Each descriptor is 12 bytes on
C3 and 16 bytes on a 64-bit host. Non-tail text chunks retire when their live-word
count reaches zero; the tail remains available for appends. Suffix handles share
one release obligation with their original handle. An allocation failure leaves
the append output unchanged and existing word bytes valid. Rust tests inject
failure at every allocation in a multi-chunk growth sequence and check cleanup.

Token metadata also uses caller-owned opaque state (26 pointer-sized words).
Handles grow in 512-byte chunks of 64 entries; consumed whole chunks retire,
while the pointer table retains its capacity. Four byte arrays store style,
focus boundary, link id and size slot; a u16 array stores visible-offset deltas.
Two boundary flags share two bits per token. These arrays start at 16 entries
and double with checked sizes. They remain separate allocations so the largest
metadata request is the u16 array, rather than one large combined token vector.

For capacity C, these arrays use 6.25 × C bytes, plus 512 bytes per live handle
chunk and the handle-pointer table. Growth temporarily retains the old and new
buffer of the array being copied. Sparse rebases use two-word entries and a
doubling array; NFC source positions use 64-delta linked chunks (140 bytes on
C3). All storage belongs to one paragraph and is released at destruction; no
new process-wide cache exists. Failure can retain grown capacity but cannot
partially insert a token or shift source offsets. New text is released when
metadata insertion fails, and the paragraph latches its existing failure flag.
A failed hyphenation insertion leaves the original word intact.

The C++ facade still performs normalization, tokenization, font measurement,
MiniBidi ordering and TextBlock construction. Sparse visible-offset rebasing and
NFC absorbed-position accounting live in Rust. Ruby strings, link targets,
size-slot font tables and MiniBidi scratch retain their C++ ownership. The ABI
owns no rendering or storage service and adds no task or synchronization.

## Paragraph layout

Rust owns the DP and greedy breakers, hyphenation candidate
selection and token splitting, line justification and positions, and line source
metadata. The algorithms preserve the original rounding, cost ties and fallback
order. Hyphenation calls the existing Rust engine directly. C++ supplies renderer
advances, kerning, space metrics and ruby adjustments through synchronous
callbacks; MiniBidi runs at the same point and supplies the original visual order.
Metric callbacks cannot reenter layout or retain borrowed pointers.

The width cache owns 2 × N bytes initially and doubles only when a split needs
more slots. DP owns exact `int32_t[N]` and `size_t[N]` arrays (8 × N bytes on C3);
greedy has no DP scratch. Break indices grow from one pointer-sized slot and
double. Completed-line numeric metadata uses one exact allocation: 11 × W bytes,
or 14 × W with focus annotations, for at most 10,000 words. This replaces the
separate position, style, source-range and focus vectors; word strings and BiDi
scratch remain C++ inputs to the existing reorder hook. TextBlock copies borrowed
metadata into its existing arena before the line owner is destroyed.

All new buffers use the paragraph allocator, check size arithmetic, and return
allocation failure. Their lifetime is one paragraph layout call or one extracted
line; none is cached globally. Failed growth preserves existing values, and the
C++ facade marks the paragraph failed so Section cannot commit missing text.
Tests cover DP allocation failure, width growth failure, split rollback, line
limits and failed line allocation. Verify layout against the captured C++
goldens:

```sh
cmake -S test -B build/test
cmake --build build/test -j 6
ctest --test-dir build/test --output-on-failure -j 6
```

## XML events

`xp-core::xml::Parser` owns incremental XML state. Feed a byte slice, drain
`next()` until `NeedInput`, then offer the unconsumed suffix. `feed` copies at
most 1024 new bytes per call and reports the number consumed. Its `final` flag
applies when the entire offered slice was consumed. Events borrow parser
storage until the next operation. `xp-ffi` exposes the same driver contract
through `xp_xml_*`; the header documents span and handle lifetimes.

Container, encryption, OPF, nav and NCX consumers retain their C++ interfaces and
storage ownership. `XmlPullParser` dispatches Rust events synchronously to their
existing handlers. The chapter driver consumes `xp_xml_*` events directly in
1,024-byte steps, passes original byte positions to table prescan, and honors
cancellation after the active handler returns. It restores `&name;` for the
existing HTML entity lookup in text only, reusing a checked read buffer.
KOReader XPath resolution uses a local pull loop so it can stop dispatch
immediately at a match and retain comment, PI and CDATA text-node boundaries.
A match first delivered during finalization still returns empty, as the old
driver rejected the aborted final parse. SDK OPDS, OpenSearch and XML catalog
parsers use the Expat API adapter in `lib/expat`, backed by `xp_xml_*`; the
Expat C parser is retained only in `test/vendor/expat` for host oracles.
The shared Rust core preserves raw qualified names, original byte positions,
UTF-8/UTF-16/Latin-1 decoding and the vendored GE=0 entity rules; it does not
perform HTML entity expansion.

The parser retains an incomplete token, open element names, entity names and
DTD attribute declarations. Input, decoded-token, event and attribute scratch
buffers reuse their capacity. Comments retain only their original source bytes;
the scanner keeps a dash count and normalizes that source buffer in place for
the borrowed event. Latin-1 reserves space for UTF-8 expansion in the same
buffer. Only input storage grows with a comment; it adds no comment-sized
decoded-token or event allocation. Scratch capacity from earlier tokens may
remain, and vector growth can temporarily retain both old and new allocations.
Long tokens grow through `try_reserve`; there is no fixed tag/attribute truncation. Limits are allocator capacity and Rust's
addressable vector size, rather than a 1024-byte token limit. All Rust-owned
allocations are released with the handle; C++ never frees event spans. The
host ABI test injects allocation failures at successive allocation sites and
checks cleanup. The streaming test feeds 2 MiB of text and CDATA without
retaining the document.

Run the vendored C++ oracle and consumer/cache checks separately from the
existing host suite:

```sh
cmake -S test/xml_oracle -B build/xml-oracle
cmake --build build/xml-oracle
ctest --test-dir build/xml-oracle --output-on-failure
cargo test --manifest-path rust/Cargo.toml -p xp-core
```

`test/xml_oracle/README.md` documents the committed oracle format and fixture
provenance. Rust test data is translated from those JSON goldens by
`test/xml_oracle/rust_fixtures.py`; changing the Rust implementation must not
regenerate the C++ expectations. The XML-name bitmap preserves the vendored
Expat character set and license; `test/xml_oracle/name_tables.py` reproduces it.
Native CMake suites share `test/cmake/xp_ffi_host.cmake`, which builds a host
archive in the CMake build directory and supplies allocator adapters.

## KOReader XPath compatibility

KOReader compatibility is checked by `XPathGoldens` in the main host CTest suite.
`test/koreader_xpath_resolver/README.md` documents the pre-port public-method
outputs for every repository EPUB spine item and the boundary fixtures. The
resolver strips prefixes without changing element case and retains its existing
codepoint counting and whitespace-anchor rules. The XML core emits literal line
endings separately, splits CDATA text before closing brackets, and defers retry
of an incomplete token until the available input doubles (or input is final).
These details matter because a whitespace-only callback cannot supply an anchor.
They do not establish parity for every possible Expat callback fragmentation or
its parsing-status flags.

The XPath drivers retain one Rust handle per scan, released on normal completion,
parse failure or an early match. Their existing C++ path and sibling containers
are unchanged. The added XML retry state consists of a byte count and a boolean;
no event queue or extra text buffer is allocated. Rust allocation failure returns
an empty XPath, and the host resolver test injects failures at successive Rust
allocation sites to verify cleanup. Existing C++ string/vector allocation behavior
is not covered by that injection test.

## SDK XML compatibility

`lib/expat/ExpatCompat.cpp` implements the Expat API subset used by the pinned
SDK without modifying SDK sources. It owns a Rust handle, callback pointers,
user data, line/error state and a reusable checked `XML_GetBuffer` allocation.
Only OPDS needs that extra input buffer (up to 1,024 bytes per write); direct
`XML_Parse` callers feed borrowed input. The adapter releases both allocations
in `XML_ParserFree`. Stop requests latch until the active callback returns.

This is not a full Expat replacement API. Namespace mode, DTD callbacks,
resumable parsing, explicit encoding overrides and byte-index queries are not
implemented; adding a new SDK caller requires reviewing its API use first.
The original declarations and error strings retain their Expat licenses.
Global `XML_GE` and `XML_CONTEXT_BYTES` firmware flags are unnecessary; the
Rust parser supplies the fixed GE=0 behavior. The host baseline explicitly
retains both flags so comparisons use the former firmware configuration.

`test/expat_compat` compiles the unchanged SDK feed, OpenSearch and XML catalog
parsers against both vendored Expat and the adapter. Its pre-adapter goldens
compare complete public results, errors and line diagnostics across 30 inputs
and six chunk schedules. Focused tests cover C++ and Rust allocation failures,
buffer ownership, long writes and callback cancellation.

## Shared text helpers

`lib/Utf8` and `CjkBreaks` retain their C++ signatures and delegate to
`xp-core::utf8` and `xp-core::text`. Rust owns the single Latin/Vietnamese NFC
table, algorithmic Hangul composition, character categories, and CJK break
rules. Hyphenation uses the same table with its original smaller pair subset;
paragraph splitting uses the shared Hangul predicate. The two constexpr token
flag formulas remain in the C++ header to preserve compile-time callers.

Text helpers allocate no Rust storage and retain no pointers. C++ owns returned
strings and exact-sized offset vectors. Allocating NFC emits into the existing
C++ string, retaining replacement-character expansion for malformed input;
in-place NFC instead preserves uncomposed raw bytes and never grows its buffer.
Absorbed-offset callback failure still returns an empty string. The permissive
encoder and incomplete-at-byte-zero truncation behavior are preserved.

The [text oracle](../../test/utf8_compose/README.md) describes exhaustive
codepoint checks and composition/corpus coverage. Run it together with the
existing UTF-8, combining-mark, token-boundary, Korean, hyphenation, layout and
Section tests when changing these helpers. `VisibleTextUtils` remains in C++.
