# Rust core and C ABI

The firmware links a small Rust static library. `rust/crates/xp-core` contains
portable `no_std` code and host tests. `rust/crates/xp-ffi` owns the C ABI,
ESP-IDF allocator, and panic handler. Its public header is
`rust/crates/xp-ffi/include/xp_ffi.h`.

The panel ABI is in [xp_display.h](../../rust/crates/xp-display/include/xp_display.h).
SDK board policy selects the controller features. The SDK display facade and bus
own hardware selection and transport; Rust owns controller policy. The SDK adapter
`RustPanelDriver.cpp` formats driver diagnostics.

Rust owns the EPUB parsers and layout, page records, build/load codecs, page
rendering, PNG/GIF decoding and image row processing, text measurement, glyph
rasterization and bitmap views, SD font parsing and cache policy, XTC parsing,
and the shared inflater for ZIP, PNG, ContentProtection, fonts and DictZip.
Rust also owns the loaded-font registry, including family/style slots and fallback
IDs. Rust also owns SD faces, per-style storage and the loaded-manager list.
Rust also owns SD discovery, family/size selection, variants and UI fallbacks.
C++ retains physical file I/O, TTF file and face resources, platform callbacks,
JPEGDEC integration, page storage and views, and
legacy screen resources. The Rust application owns loop/navigation policy, Home
and the EPUB session; native ActivityManager forwards into that owner and retains
the physical render task and lock. See the
[Rust UI ownership guide](rust-ui.md) and the
[chapter pipeline](chapter-pipeline.md) for page ownership and lifetime.

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
cargo clippy -p xp-core -p xp-gif --all-targets -- -D warnings
cargo test -p xp-core -p xp-gif
```

The release profile uses `opt-level = "s"` to balance code size and chapter-build
speed, with fat LTO, eight codegen units, and abort-on-panic behavior.
The decoder packages `miniz_oxide` and `xp-gif` use `opt-level = 3` in
[`rust/Cargo.toml`](../../rust/Cargo.toml) to keep inflate and GIF decoding within
their timing budgets. UI construction and input routing in `xp-ui` use `"z"`;
the remaining packages keep `"s"`. `xp-gif` is the local, dependency-free
package under [`rust/crates/xp-core/src/gif/decoder`](../../rust/crates/xp-core/src/gif/decoder),
reexported by `xp-core::gif`.
Overflow checks retain the release default (off); hash arithmetic wraps
explicitly. Debug symbols remain in the ELF, not the firmware binary.

With the pinned toolchains, eight codegen units saved 560 bytes on C3 and
272 bytes on S3 versus one unit. In the default C3 eink-qemu runs, Korean,
English, mixed matrix, mixed tables, and mixed PNG workloads had identical
pixels and heap snapshots across three interleaved runs per profile. No recorded
timing metric's median increased by more than 3 ms plus its baseline range
(maximum minus minimum). The Korean full chapter build had medians of 2,769 ms
with one unit and 2,771 ms with eight; baseline samples ranged from 2,768 to
2,778 ms, and eight-unit samples from 2,771 to 2,775 ms.

Fat LTO still emits one xp-ffi machine-code object. FFI-only `"z"` saved about
6 KB but increased open/page-render times by up to 26 ms in the original default
C3 eink-qemu comparison of those five workloads; keep `"s"`. These runtime
results do not establish device timing or S3 runtime behavior.

For C3 firmware, the build script enables LLVM's `--force-dwarf-frame-section`
option so the Rust LTO object retains call-frame information in `.debug_frame`.
That section stays in the ELF and is not flashed. The script then removes
`.eh_frame` and `.eh_frame_hdr` only from that LTO object in a separate copy of
the Rust archive, using PlatformIO's RISC-V `ar` and `objcopy`. Prebuilt
`compiler_builtins` members retain their original unwind records. Cargo's original
archive, host builds, and all C++/framework archives remain untouched.

The filtered copy lives in `release/firmware/libxp_ffi.a`; it changes only when
its bytes change, so an unchanged Cargo build does not force a relink. The pinned
release profile produces one xp-ffi LTO object. The build fails if that assumption
changes, so a toolchain/profile update requires review of the filter.

`panic = "abort"` does not remove metadata already present in the prebuilt C3
`core` and `alloc` libraries. With the pinned 1.99.0 toolchain, LTO retains their
`uwtable` attributes and an unwind-table module flag. Both
`-C force-unwind-tables=no` and explicit `-C panic=abort` leave the archive's
frame-table bytes unchanged; the former also leaves the linked map and firmware
size unchanged. Filtering after LTO removes the retained flashed tables while
`.debug_frame` preserves offline Rust stack decoding, including emulator traces.

The filter is enabled only when the installed framework's `esp32c3/sdkconfig`
explicitly disables `CONFIG_ESP_SYSTEM_USE_EH_FRAME`. In the pinned default profile
(pioarduino 55.03.311, ESP-IDF 5.5.5), that option and
`CONFIG_ESP_SYSTEM_USE_FRAME_POINTER` are unset and
`CONFIG_ESP_SYSTEM_NO_BACKTRACE=y`. The RISC-V `panic_print_backtrace` therefore
prints a raw stack-memory window, rather than parsing `.eh_frame`; CrossPoint's
`HalSystem.cpp` wrapper also captures a 1,024-byte stack window. Rust still calls
`abort()` on panic. C++/framework unwind tables, registration and panic handling
remain unchanged. A framework configuration without the explicit disabled
EH-frame option keeps the original Rust archive.

S3 remains on its existing build path: its `cargo +esp -Zbuild-std=core,alloc`
archive contributes no linked `.eh_frame` entries with the pinned toolchain.
The C3 scratch measurement removes 20,964 linked frame-table bytes and saves
20,976 firmware-image bytes after alignment, with `.flash.text` and
`.flash.rodata` unchanged. All original CFI address ranges remain in the ELF.
Whole-firmware table discard is a separate human configuration decision and is
not part of this filter.

The boot self-check compares Rust FNV-1a and CRC-32 against C++ and miniz and
logs the ABI version and result. It borrows a constant buffer and allocates
nothing. Future Rust allocations use byte-addressable internal RAM and must
have an explicit budget and failure policy; the allocator returns null on OOM.
Rust panics call C `abort()` to enter the existing ESP-IDF panic path.

## Rust size rules

Measure the linked image from the same worktree path and profile. Compare
`firmware.bin`, `.flash.text`, `.flash.rodata`, and static RAM; archive size
includes exports that the firmware may discard. [`scripts/rust_size.py`](../../scripts/rust_size.py) compares
saved build directories with the selected target's binutils and counts symbol
aliases once. Count the remaining C++ facade and callback adapters as part of a
port. A small Rust entry point can move its cost into shared callees.

Keep internal records naturally aligned when their fields need no padding.
Read packed input through the existing unaligned-copy boundary and assert wire
sizes and offsets. In the library port, changing only the internal record
alignment saved 622 C3 / 621 S3 bytes of archive code without changing the
64-byte header or 128-byte record. Do not apply this to formats that need padding.

Use bounded, shared algorithms where they remove measured duplication. The
library's shared sorter and bounded author-token sort reduced linked C3 sorting
code by 752 bytes. Keep fallible allocation and input checks; the shared panic
runtime did not grow in the library, BLE, settings, or display ports.

Inspect repeated callback setup and value copies before adding inline hints.
Rust LTO cannot inline through the separately compiled C++ services. Preserve
callback lifetime and synchronization contracts when changing these boundaries.
Seven BLE helpers marked cold and non-inline grew C3 archive code by 68 bytes
while saving 167 bytes on S3. A separate BLE policy crate at `"z"` grew code by
116 / 133 bytes. Neither is a general size rule.

Keep ordinary Rust iteration and target-sized indices unless measurement shows
a benefit. Both firmware targets have 32-bit `usize`; changing it to `u32` does
not narrow arithmetic. A bounded-slice rewrite of BLE lookup grew archive code
by 14 / 26 bytes. Check both targets, real text/page goldens, and reader timing
before retaining an optimization profile or source rewrite.

Function prologue sizes do not bound peak task stack use. Inlining and outlining
can move stack space between callers and callees whose frames coexist. Device
stack high-water marks and timing remain to be qualified on C3 and S3; default
C3 eink-qemu measurements do not establish hardware or S3 runtime behavior.

## Diagnostics

Diagnostics must preserve behavior and add no heap allocations. C++ owns the
`LOG_*` calls and log-level gates. The reader's `book_owner` formats its migrated
load, cache, package, CSS, ZIP and TXT/Markdown diagnostics in Rust.
`section_owner` formats Section build and Section cache diagnostics through the
same log leaf. Both preserve the native messages' origin tags and levels. Their shared
non-inlined formatter uses a 256-byte stack buffer, checks
`XpEpubLeaves::log_enabled`, then synchronously
passes borrowed origin/message spans to `XpEpubLeaves::log` in
`xp_epub_session.h`. C++ emits them with `%.*s`, mapping severity 0/1/2 to
debug/error/info. Inflate allocation failures still use `inflate_oom_log`, which
calls the cold native `InflateStream::logAllocationFailure` helper to format
the message with native heap/PSRAM measurements.

`render_resources/logging.rs` formats the reader's nine render and timing
diagnostics with the `ERS` origin. Its separate non-inlined formatter uses a
384-byte stack buffer and the same `log_enabled`/`log` leaf path. The
[144-case parity test](../../test/reader_render_resources/README.md) freezes
these messages against the parent formatter.

This reader boundary does not change the rule for other modules: C++ owns their
format strings and origin tags.
When an event ends a Rust call, return a status or failure detail for C++ to log.
For events inside a call that continues, use one nullable, synchronous diagnostic
callback in the services/options descriptor: context, `event, a, b, c, d`, and
optional borrowed text slices with lengths. A null callback means silent.
The callback must not reenter Rust or retain pointers. Text is borrowed only for
the callback; use `%.*s` in C++ when it is not NUL-terminated.

Do not add callbacks on per-word, per-glyph or per-row success paths. Keep copies
needed only for diagnostics in cold, non-inlined helpers so hot stack frames do
not grow. For example, `Assembly::add_line` calls `diagnose_dropped_link` only
after a rejected link; that helper copies at most 48 bytes in
`rust/crates/xp-core/src/chapter/assembly/mod.rs`.

When porting C++, inventory `LOG_*` statements before and after by counting
`(level, origin, format)` tuples, including duplicates. Restore each line for the
same event, or record why that event no longer exists. Document unavailable
arguments and borrowed-slice format changes. Compare emulator logs with upstream
for the same workloads, checking message text, values and occurrence counts.

Event tables and payload contracts live beside each relevant ABI block in
`rust/crates/xp-ffi/include/xp_ffi.h`; C++ mapping functions define the log lines.
Examples are `lib/Epub/Epub/parsers/ChapterPlatform.cpp`,
`lib/Epub/Epub/parsers/PackageDiagnostics.h`, `lib/Epub/Epub/css/CssPlatform.cpp`,
`lib/Epub/Epub/Page.cpp`, `lib/Epub/Epub/converters/ImageDiagnostics.h`,
`lib/PngDiagnostics/PngDiagnostics.h`, `lib/GifSupport/GifCommon.h` and
`lib/EpdFont/SdCardFont.cpp`.

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
also run through the C++ `Hyphenator` wrapper. The `HyphenationCommon` predicate facade was
deleted with the dead-code cleanup; `TokenBoundary` calls `xp_text_break_after_hyphen`
directly.

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

`ZipFile` keeps its native interface and the state behind it: the `HalFile`,
the cached slim stats, the sequential scan position, and the last lookup. Its
adapter is `lib/ZipFile/ZipPlatform.cpp`. `xp-core::book_io::zip_index` owns
lookup, enumeration, cache population, and batch matching; `xp-core::zip` parses
ZIP records without allocation.
ZIP entries use `InflateStream`, backed by `xp-core::inflate`. SDK
ContentProtection reaches it through the `mz_*` compatibility adapter in
`lib/miniz`; the Rust PNG decoder calls it directly. The vendored
[miniz_oxide subset](../../rust/crates/xp-core/src/inflate/miniz/README.md)
shares one block parser, bit reader and match-copy loop between fast lookup
tables and compact canonical tables. It owns no buffers; callers supply decoder
storage and output history.

`InflateReader` embeds compact Rust state for raw deflate. Its firmware caller is
DictZip, which supplies a 32 KiB history ring through native file I/O. Built-in
font groups decode through `font::runtime::Decoder` into Rust cache buffers.
Both paths use the existing compact SmallTables inflater.
`InflateReader`'s size and alignment are guarded because changing its footprint can
shift heap boundaries. See the [compact inflater checks](../../test/inflate_small/README.md)
for frozen outputs, malformed-input differences and recovery tests.

`ZipFile::indexServices` supplies synchronous seek, read, cache, and diagnostic
callbacks through `XpBookZipIndexServices`. `xp_book_zip_index`,
`xp_book_zip_populate`, and `xp_book_zip_batch` dispatch to Rust. In
`rust/crates/xp-core/src/book_io/zip_index.rs`, `Services::next` drives `Walker`
and the 46-byte central-directory reads; `Services::data_offset` drives the
30-byte local-header read; `Services::name` issues the entry-name reads. Rust
skips names that cannot match, and the native cache copies retained names. Input
pointers are never retained. A scan starting in the middle wraps once and stops
on reaching its original starting offset.

Extraction is Rust-owned as well. `xp_book_zip_memory` and
`xp_book_zip_stream` (`rust/crates/xp-ffi/src/book_io.rs:273,302`) dispatch to
`xp-core::book_io::zip_read` (`rust/crates/xp-core/src/book_io/zip_read.rs:74-276`),
which sequences the lookup, the seek to the entry's data offset whose result it
ignores, the stored copy, the method-8 deflate read, output allocation and
release, and the streaming early stop. The memory path reads deflate input
through a 1,024-byte buffer. Allocation and release, inflate, and the write sink
are still C++ callbacks that `extractServices` builds into `XpBookZipServices`
(`lib/ZipFile/ZipPlatform.cpp:60-82`), so buffers come from `malloc` at `:65` and
Rust only borrows them. `ZipFile::readFileToMemory` and
`ZipFile::readFileToStream` (`lib/ZipFile/ZipPlatform.cpp:387-405`) only build
the service records and forward those two calls.

`xp_zip_begin`, `xp_zip_request`, `xp_zip_next`, `xp_zip_local_request`, and
`xp_zip_data_offset` in `rust/crates/xp-ffi/src/zip.rs` are host-only test wrappers.
They are excluded from firmware builds and are not the production I/O boundary.

`xp_book_zip_details` drives EOCD scanning through `XpBookZipDetailServices`
(`rust/crates/xp-ffi/include/xp_book_residual.h:218-229`): `open`, `size`,
`allocate`, `read`, `save` (which stores the parsed EOCD), and `diagnostic`. It
keeps the last-1-KiB limit and searches backwards for the final signature with
room for the 22-byte record. The C++ adapter owns one checked buffer of at most
1,024 bytes until parsing returns. Directory operations borrow
`ZipFile::IndexContext`'s 256-byte name buffer. Names of 256 bytes or more are
skipped. Names match raw bytes: normalization remains with callers. Rust
`batch_match` computes the shared FNV-1a hash, searches the sorted targets, and
counts matches, including duplicate targets; C++ supplies deque access and
size-storage callbacks.

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

The test-only wrappers in `rust/crates/xp-ffi/src/zip.rs` (`xp_zip_eocd_request`,
`xp_zip_eocd`, `xp_zip_begin`, `xp_zip_request`, `xp_zip_next`,
`xp_zip_local_request`, `xp_zip_data_offset`, `xp_zip_name_matches`) reject invalid
null/alignment/length arguments and offset overflow. Five of them carry
`#[cfg(not(target_os = "none"))]` (`:54`, `:67`, `:84`, `:111`, `:123`) and so
never reach a firmware build; `xp_zip_eocd_request` (`:24`) and `xp_zip_eocd`
(`:38`) compile for every target but no firmware code references them, so the
linker drops them from shipped images; `xp_zip_name_matches` (`:148`) is the
eighth, referenced only from `test/zip_file/ZipFileTest.cpp`. The shipping callback
ABI leaves pointer validity to the caller: `xp_book_zip_index`
(`rust/crates/xp-ffi/src/book_residual.rs:98-112`) null-checks only the services
record, then writes `output` unguarded, and `Span::bytes`
(`rust/crates/xp-core/src/book/residual.rs:94-100`) checks nothing, so callers must
pass valid aligned storage. Short local headers and failed seeks/reads fail
without parsing uninitialized bytes. The compatibility corpus does not define
results for the old scanner's unchecked short reads. ZIP64 and multi-disk
support are not added by this change.

## Chapter and section compatibility

The reader's `ReaderSection` in `xp-ffi::app::epub::section_owner` owns the
Section cache path, readable partial state and optional incremental build. Its
`BuildOwner` owns the Rust Chapter handle and a separate stable `BuildServices`
allocation for files, input, Section-time CSS, paths, anchors and page records.
Chapter is destroyed before those services. The reader calls typed Rust methods;
there is no native Section build object or `section_*` service table.

`section_cache.rs` shares cache reads and transactions with the cache-only native
adapter in `book_section.rs`. `lib/Epub/Epub/SectionPlatform.cpp` retains five
queries for ProgressMapper: page count, anchor, paragraph, list item and visible
text offset. It owns only its cache path and call-local file/scratch resources.
The version-68 codec remains in `xp-core::section`. Header flags preserve native
bool normalization; the image setting retains its original byte value.

A loaded partial remains readable while a rebuild has fewer pages. Suspension
commits completed pages and drops the Chapter; resume rebuilds from the start.
Committed-page reads use a separate local file; active-build reads restore the
writer cursor after loading a Page. `page_owner` allocates and decodes the reader
Page directly through FileSlot; the native `page_load` bridge is retired.
No mutable Section/build borrow may cross a physical callback. Resource access
and the book cache-lock order remain those of the session.

Page records grow in fixed 64-entry chunks and anchors in 32-entry chunks.
Existing payloads stay in place while the small pointer directory grows exactly;
all allocations are fallible. Anchor text moves into its completed record.
Do not replace this with a doubling Vec that overlaps large old/new LUT storage.
The four low-level record/offset exports used by the old native codec remain
host-only under `section-test`; `test/section_io/SectionTestCodec.{h,cpp}` uses
them to construct and inspect cache fixtures with the shared production codec.
The [actual-owner fixture](../../test/reader_section/README.md),
[Section I/O oracle](../../test/section_io/README.md) and
[anchor-bound cases](../../test/section_golden/README.md) cover builds, partial
recovery, interleaved reads, faults and cache bytes. Historical policy fixtures
use a named `LegacySectionServices` variant only under `app-test`; new owner
compositions must use actual ReaderBook/ReaderSection and install no legacy table.

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

`xp-core::para::paragraph::Paragraph` owns the word arena and token storage.
The C++ `Paragraph` adapter embeds 64 pointer-sized, pointer-aligned words, with
compile-time Rust size/alignment checks and no separate handle allocation.
Arena/token buffers use the existing C++ nothrow allocation callback and retry
once after reclaiming for the failed request plus 8 KiB. The reader's
`section_owner/allocator.rs` calls `reclaim_caches` and `font_runtime::release`
directly; only legacy `Paragraph.cpp` / `ChapterPlatform.cpp` callers use
`MemoryManager::ensureFree`. Rust returns buffers to the matching release
callback. Link targets and temporary Rust vectors use
fallible `try_reserve` with the firmware's ESP-IDF global allocator.

Text chunks remain 2,048 bytes, with exact-fit chunks for larger words and a
65,534-byte payload limit. Descriptors start at eight slots and double, keeping
both descriptor arrays live only during growth. Each descriptor is 12 bytes on
C3 and 16 bytes on a 64-bit host. Non-tail text chunks retire when their live-word
count reaches zero; the tail remains available for appends. Suffix handles share
one release obligation with their original handle. An allocation failure leaves
the append output unchanged and existing word bytes valid. Rust tests inject
failure at every allocation in a multi-chunk growth sequence and check cleanup.

Token metadata is an internal Rust owner embedded in the paragraph.
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

Rust performs normalization/tokenization and owns visible-offset rebasing, NFC
source accounting, ruby text, link targets and size-slot font tables. Ruby metadata
uses 16-entry chunks (512 bytes each on C3), with a small fallible pointer table;
consuming a prefix releases whole chunks, and consuming all text releases the table.
Long annotation/target text has separate fallible byte storage; short text stays
inline. Rust executes measurement and bidi and owns loaded-font and cache policy.
Rust owns SD face storage and scoped file holders over native HAL leaves.
C++ retains TTF file/face resources and the shared bidi workspace and mutex.
Rust owns SD catalog/system policy. TextBlock is a borrowed
page view in firmware.
The ABI adds no rendering/storage service, task or synchronization.

## Paragraph layout

Rust owns the DP and greedy breakers, hyphenation candidate
selection and token splitting, line justification and positions, and line source
metadata. The algorithms preserve the original rounding, cost ties and fallback
order. Hyphenation calls the existing Rust engine directly. Production advances,
kerning, spaces, ruby adjustments and bidi word order run in Rust over native
font descriptors. C++ prepares fonts and handles dynamic glyph/kerning work;
host oracle adapters can still supply metric callbacks. Neither provider
callbacks nor metric callbacks may reenter layout or retain borrowed pointers.
See [native text measurement](#native-text-measurement) for the ownership contract.

The width cache owns 2 × N bytes initially and doubles only when a split needs
more slots. DP owns exact `int32_t[N]` and `size_t[N]` arrays (8 × N bytes on C3);
greedy has no DP scratch. Break indices grow from one pointer-sized slot and
double. Completed-line scratch uses one exact allocation for numeric metadata,
ruby spans, word offsets, visual order and sanitized NUL-separated text. For W
words and B packed bytes on C3, it uses `align4(W * (focus ? 14 : 11)) + 12*W + B`
bytes. The bounds are 10,000 words and 65,535 sanitized bytes, including NULs;
these are validation limits, not a guarantee that the device can allocate that
much memory. Rust bidi receives borrowed text and writes order indices. The
settings preview passes each completed line to the Rust page-byte builder. Chapter
assembly encodes the line and retains its links in Rust chapter storage
before scratch is released. Both sinks propagate destination allocation failure;
see [chapter ownership](chapter-pipeline.md).

Word/token storage, widths, breaks, numeric scratch and ruby chunks use the
paragraph allocator. Link/ruby byte owners, pointer tables and temporary vectors
use fallible Rust allocation. Lifetimes are paragraph-, layout- or line-scoped;
none is cached globally. Failed growth preserves existing values. Rust latches
dropped text so the section builder cannot commit missing text.
Tests cover DP allocation failure, width growth failure, split rollback, line
limits and failed line allocation. Verify layout against the captured C++
goldens:

```sh
cmake -S test -B build/test
cmake --build build/test -j 6
ctest --test-dir build/test --output-on-failure -j 6
```

## Reader book ownership

The ordinary EPUB/TXT/Markdown reader owns one `ReaderBook` in
`xp-ffi::app::epub::book_owner`. It owns the path, metadata bytes, cache file,
spine/TOC and size-window state, load-time CSS, package writer, ZIP/TXT resources
and protection loan. It calls the existing Rust parsing and cache algorithms
through native physical leaves; see [protected books](#reader-protected-book-boundary).
Paths and metadata are
byte strings; do not require UTF-8 or cap their lengths to fit an adapter.

The reader publishes a Rust-owned image extractor only after load succeeds.
The binding borrows the stable `ReaderBook` allocation and calls its stream
reader directly. It is detached before the book is dropped; sync can detach
and rebind it to the same live owner. Load-time CSS must be released before
this binding. Chapters, Bookmarks, Toolbar and progress/XPath mapping read the
same Rust book under resource access.

The shipping reader has no native book facade or book-open/load/getter table.
Registration of the six queries in `XpEpubBookBorrow` and the native facade
lifecycle remain only in host oracle fixtures; shipping Rust callers still use
the query functions directly. Those fixtures test the native reference-return
APIs and reject unsupported native load/metadata access. Ordinary `Epub` objects
used by Home, Library, Sleep and sync retain their existing native interfaces.
Book-owner logs use the leaf described under [Diagnostics](#diagnostics).

Image failure reset, RAM-cache release and decoded-cache eligibility run
directly in Rust. Lazy extraction streams 8,192-byte chunks into an inline,
pinned HAL file through a storage-only native sink, then flushes and closes it.
A failed stream removes the partial file. The `.px2` format and cache validation
rules are unchanged. Image decode selects formats in Rust and calls the shared
Rust pipeline operations directly; JPEGDEC remains the native physical decoder for JPEG.
The native extraction adapter and image lifecycle table slots remain only for
host oracles.

`ResourceAccess` precedes the cache lock, then storage. A cache row read releases
its guard before invoking a sink; the decoded row owns its bytes for the
synchronous callback, which may reenter another query or stream. Do not hold a
Rust mutable borrow across a native callback. Clipping and recents adapters lend
string views to synchronous stores, which copy into their final fields.
No metadata view survives the call. Rust owns one bookmark record vector,
its exact JSON encoding and the Kindle text export. Whole-file reads and
temporary-file replacement remain native HAL transactions under the original
StorageLock. `storage_read_file` constructs the SDK string in caller-owned
stack storage and returns borrowed bytes. Rust consumes them before calling
`storage_read_release`; no native read callback calls a Rust sink.
`storage_write_file_atomic` takes encoded bytes.
Whole-file HAL operations check readiness and emit SDK diagnostics directly;
the reader log leaf has no raw-byte mode, and file-open leaves have no readiness
flag. Despite its name, the write leaf is not power-loss atomic: the SDK removes
the destination before renaming the temporary file. A rename failure followed
by cleanup can leave neither file. The port preserves
that inherited behavior; a failed save does not always preserve the old file. The
bookmark filename convention is unchanged; `BookmarkUtil` remains for the two
FileBrowser rename-path calls until that screen moves to Rust. XTC and sync keep
`ProgressFile` and `EpubReaderUtils::saveProgress`.

Book cache v10, CSS cache v19, Section cache v68 and the target's
`std::hash<std::string>` path convention are unchanged. The native path-hash
leaf uses the equivalent borrowed `std::string_view` hash, avoiding a temporary
string allocation. Target hash parity is verified with the pinned C3 and S3
toolchains under emulation, not on a physical device. The
[reader lifecycle tests](../../test/reader_lifecycle/README.md) distinguish actual
owner/facade compositions from historical synthetic service fixtures.

## Reader persistence and service boundary

Rust reader code calls the progress writer and the recents/state record codec
in Rust directly. It owns prune, add, remove, finished-book path replacement,
and save ordering. Native primitives provide storage operations, row/string
access, the save mutex and ArduinoJson document operations. Each primitive
returns before Rust continues; none calls a Rust record or serialization sink.
Progress keeps the existing open/write/flush/close/remove/rename sequence and
error messages, including the inherited remove-before-rename power-loss window.

Recents/state containers and ArduinoJson document operations stay C++-owned;
the record codec and save orchestration are Rust-owned. Native access uses
one-way primitives. Moving the containers is not required for the reader-path
goal; owner: none planned (SET-OWN family). Settings-save still runs synchronously
through its existing boundary under ruling 45. Recents UI and Library use their existing facades.

Reader time decisions call the Rust time service directly. Native callbacks
supply the clock, floor storage, SNTP setup and wait operations. State-path
reads return borrowed strings without a Rust sink. Document-ID sampling and its
result sink remain pending in the unit after R3b.

The [persistence checks](../../test/reader_persistence/README.md) compare actual
Rust orchestration with the existing native facades, including every serialized
write failure, and check progress I/O and time-service event order.

## XML events

`xp-core::xml::Parser` owns incremental XML state. Feed a byte slice, drain
`next()` until `NeedInput`, then offer the unconsumed suffix. `feed` copies at
most 1024 new bytes per call and reports the number consumed. Its `final` flag
applies when the entire offered slice was consumed. Events borrow parser
storage until the next operation. `xp-ffi` exposes the same driver contract
through `xp_xml_*`; the header documents span and handle lifetimes.

Ordinary native `Epub` consumers use the C++ `PackageParser` Print adapter for
container, encryption, OPF, nav and NCX, feeding `xp_pkg_pass_*` document handles.
The reader's `ReaderBook` owns its package passes directly and uses the same core
parsers and cache algorithms. Rust `epub::facade` drives the
parsers, manifest lookup and CSS paths; `epub::cache` owns the cache build state,
spine/TOC record serialization, book.bin v10 layout, LUTs, cumulative sizes and
ZIP size preparation. `xp-ffi::book_cache` owns cache reads and size windows.
`BookMetadataCache` owns the Rust cache handle; document passes borrow it and
must be destroyed first. `PackagePlatform.cpp` implements the shared native
facades and resource adapter, retaining HalFile ownership, buffered I/O, ZIP
access, string/deque storage and the size-window lock for those native owners.
The [reader book owner](#reader-book-ownership) has its own scoped native file and
cache-lock leaves; the reader book owner's facade exists only for host fixtures.
See the [package oracle](../../test/package_golden/README.md) for parity checks
and their limits.

The reader's `section_owner::driver` owns the Chapter and input buffer directly.
The retained dictionary driver in `rust/crates/xp-ffi/src/book_parser.rs` borrows
its native facade's chapter handle and read buffer through `xp_book_parse`. It calls the Rust XML
parser in 1,024-byte steps, owns finalization and stop checks, and writes the
packed status/error/line result through `result`.
`rust/crates/xp-core/src/chapter/table.rs` opens, seeks and measures through a
synchronous borrowed native reader; only Rust accesses the active semantic state.
Anchor membership and commit decisions, entity-buffer growth, image-cache
suffix/counter selection and byte-versus-Page output selection also live in Rust.
It restores `&name;` for the existing HTML entity lookup in text only, reusing
the checked read buffer.

`ChapterPlatform.cpp` implements the parser facade and native leaves: HalFile,
renderer and framebuffer loans, buffer allocation, native string/vector/deque
storage, Page decoding and diagnostic formatting. The private resource base and
reserved semantic-pointer slot preserve the 300-byte parser allocation on C3
and S3; they do not own a second allocation. This native wrapper remains for
`DictHtmlPages`; the reader uses its Rust build services instead. Direct table
measurement helpers are declared only in
`test/chapter_html_slim_parser/ChapterTestAbi.h` and linked with `chapter-test`;
shipping code uses the internal table driver. The separate `xp_book_jpeg` path
retains its existing boundary.
KOReader XPath resolution uses `xp_sync_xpath_*`. Its Rust state consumes XML
events directly in a local pull loop so it can stop dispatch
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

The C++ Print adapter retains one `XpSyncXPath` handle per forward scan and
releases it on normal completion, parse failure or an early match. Rust owns
the XML parser, path and sibling containers, visible-text counters and result.
Parser and XPath state share one checked allocation; growing containers use
fallible reservations. The XML retry state consists of a byte count and a
boolean; no event queue is allocated. Rust allocation failure returns an empty
XPath, and the host resolver test injects failures at successive Rust allocation
sites to verify cleanup. The C++ facade still constructs the returned strings;
their allocation behavior is not covered by that injection test.

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

## HTTP transfer boundary

`xp-ffi/src/http_transfer` owns HTTP transfer/resume, URL/header policy and WiFi guard ordering; `HttpDownloader` and `OpdsHttpTransport` retain the native consumer interfaces.

The [HTTP ABI](../../rust/crates/xp-ffi/include/xp_http_transfer.h) lends one request whose text, body and callbacks expire before the transfer returns; its five descriptors provide SDK socket/TLS/parser operations, HalFile operations, radio get/set, native `std::string` storage and diagnostics. Log strings stay in `src/network/HttpTransfer.cpp`; Rust selects the diagnostic events and their order.

The `retire_` suffix names the migration cards that must release the last native consumer before removing a facade: N5 OTA, N6 fonts, C2 OPDS, C4 plugin catalog actions, B2 protection/LCP, N7 plugin services, S1 KOReader sync, S2 KOReader UI, W3b web relay/jobs and C1 shared catalog/settings; Z1 marks compatibility cleanup and P19 the final physical-leaf audit.

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

## Paragraph layout boundary

Rust owns paragraph words, token metadata, links, ruby annotations, size slots,
style, line breaking and line extraction. The C++ paragraph adapter embeds the
Rust state and supplies a native font provider, preparation callback and
completed-line sink. Host synthetic renderers retain synchronous metric callbacks.
Words retain the existing 2 KiB arena chunks; token source ranges retain NFC and
hyphenation provenance. Allocation failure latches dropped text so the section
builder rejects incomplete output.

A line borrows packed NUL-separated text, word offsets and numeric arrays until
the sink returns. One fallible allocation holds the line metadata, ruby spans,
order indices and text; it is released immediately after the sink returns. The
settings preview copies these values through the Rust page-byte builder. Plain
dictionary selection uses the same builder with its existing word and position
vectors. Both load the finished bytes through `Page::fromBytes`; chapter assembly
also writes Rust-built page records. Loaded pages lend immutable TextBlock views
from their arena. Owning TextBlocks remain only in host codec/layout fixtures.
See [chapter-pipeline.md](chapter-pipeline.md) for
those ownership and cache boundaries. Rust computes direction and visual word
order using the existing C++-owned shared workspace and mutex. Host callback adapters retain the
borrowed-span contract without reconstructing temporary word-string vectors.

The sink may query paragraph scalars, including the number of remaining lines,
but must not mutate or destroy the paragraph. Renderer callbacks cannot reenter
it. The layout implementation releases its mutable paragraph borrow before
calling the sink. No line span survives that callback. The `advance` callback
receives nonempty text with a readable NUL at `data[len]`, excluded from `len`;
other span callbacks are length-bounded and have no such guarantee. Returned
word/ruby spans remain borrowed until paragraph mutation or destruction.

Native `chapter-trace` builds record the original paragraph layout fields from
Rust through a host-only sink. Firmware supplies no trace sink. The chapter
operation instrumenter excludes the explicit style writes that replaced C++
reference mutations, preserving the original observations. The allocation
failure test first counts a successful parse and then checks allocation budgets
until the first successful parse; it no longer assumes all Rust owners fit within
128 allocation requests. Dedicated ruby tests fail each chunk allocation and
check cleanup, and link tests force the C++ destination allocation to fail.

Chapter semantics and assembly access the current Rust paragraph directly;
the page sink only crosses into C++ for the operations described in the
[chapter boundary](chapter-pipeline.md).

## Native text measurement

Production paragraph/chapter layout and `GfxRenderer` measurements use
[`textm::native::Engine`](../../rust/crates/xp-core/src/textm/native.rs).
Rust executes measurement rules, resident font-table reads, bidi shaping and
word ordering. The Rust loaded-font registry owns family/style slots and fallback
IDs. C++ font facades query this owner. One Rust `FontCacheOwner` holds the
built-in bitmap cache, decoder and scan/prewarm state. Rust owns SD faces,
per-style storage, loading, registration, eviction and catalog/system policy.
Rust also owns vector source storage, face lifetimes, registration and cache policy;
C++ supplies the non-owning FreeType/HAL operations.
[`GfxRenderer::textProvider`](../../lib/GfxRenderer/GfxRenderer.cpp) obtains the
Rust registry provider, which supplies style descriptors and dynamic handlers.
SD glyphs that are not resident and TTF glyph/coverage/kerning work still use
those handlers.
The [paragraph binding](../../lib/Epub/Epub/Paragraph.cpp) and
[chapter sink](../../lib/Epub/Epub/parsers/ChapterPlatform.cpp) leave the
production per-word metric callbacks null. Host synthetic renderers and
differential tests retain the callback backend.

Each synchronous engine keeps a temporary, stack-owned cache of two family
descriptors (272 bytes on C3), borrowing tables rather than copying or owning
them. A generation change invalidates the cache before the next table loan.
Registration, fallback changes, SD preparation/rebuild/eviction/release/reload
and dynamic TTF operations must invalidate descriptors when tables can change.
Every table loan ends before a handler that can mutate storage; handlers return
copied metrics. The longer built-in table walk excludes SD and dynamic handlers.
See the engine's safety contract and
[`FontTableGeneration.h`](../../lib/EpdFont/FontTableGeneration.h).

This generation is an invalidation signal under the existing serialized font
owner, not thread safety for font access. Owners must remain live and serialized
through layout/measurement; callbacks cannot reenter the engine. On the
single-core ESP32-C3, the linked `__atomic_fetch_add_4` helper from ESP-IDF
`components/newlib/src/stdatomic.c` saves the interrupt mask, disables interrupts
for the increment and restores the mask (`esp_stdatomic.h`); Rust reads the
aligned word with a relaxed atomic load. This does not serialize table loans
against a concurrent free. The bidi mutex protects only bidi storage.

[`BidiUtils`](../../lib/MiniBidi/RustBidi.cpp) owns the existing lazily allocated
3,072-byte workspace and mutex. Rust borrows it exclusively through
`xp_native_bidi_acquire/release`, holding the lease while visual text is used.
Rust supplies the workspace size and alignment to the native allocation leaf;
value initialization zeros the scratch bytes without calling back into Rust.
An allocation or contract failure releases the lease and retries on the next use.
Reordering scratch, shaping and anchors overlap only when their lifetimes do
not; visual UTF-8 reuses dead logical-character storage. Measurement and drawing
share this workspace and add no visual-string allocation. The small C++ bidi
wrapper also serves dictionary, selection and keyboard callers.
Those native wrappers use the shared compile-time workspace constants in
`xp_ffi.h`; Rust checks them against its actual layout during compilation.
They do not call Rust for workspace metadata, including on warm reuse.

The EPUB section builder reads line and codepoint metrics directly in Rust.
On catalog firmware, builtin variant selection and readiness checks use the Rust
loaded-font registry; SD fonts use the Rust SD catalog. Non-catalog builds retain
the native variant/readiness callbacks. Clipping advance and selection ascent
use the Rust font owner. The build script derives builtin IDs from
`src/fontIds.h`. Host oracle tables keep their scalar metric callbacks behind
test guards. Rust selects the publication hyphenation language and mirrors only
the resulting ID into the existing native cache for chapter/Definition clients
(R6); this one-way write neither allocates nor calls back into Rust.

Catalog firmware reads raw reader settings from native state, then selects the
loaded SD/vector family or nearest builtin size in Rust (lower size on a tie).
A snapshot requested without font resolution leaves its font ID zero. The shipping
render-spec path uses the same Rust resolver before reading compression and the
remaining raw native fields. Settings serialization stays native.
Native font-ID consumers remain in `CrossPointSettings::readerRenderSpec`,
`DefinitionPayload`, `ClipSelectionActivity`, `DictHtmlPages`, `DetailsActivity`
and `TextSettingsPreview`; their retained paths still use `getReaderFontId`.

Reader paragraph allocation uses a static Rust allocator table. It borrows the
existing native table for zeroed, null-on-failure allocation, paired deletion
and diagnostics. Rust retries after evicting render, SD mini and (on vector
boards) vector caches, in SDK sink order, stopping at the same reported-byte
target. Persistent
advance tables remain live. Legacy native paragraph callers retain the SDK
allocator. Adding or changing a renderer memory sink requires updating both
paths until the legacy native allocator is retired.

The production paragraph metric table points directly to the Rust font provider.
The legacy chapter/definition table in `ChapterPlatform.cpp` still uses a native
provider adapter; its removal belongs to R6.
Shared application translation resolves strings in Rust after querying the current
native `language` field; this is a one-way native state read, not a physical leaf.
The reader-path goal does not require moving language ownership. Width and line
height read the borrowed Rust font owner. Their
legacy callbacks remain only in host oracle tables. TOC row prewarming also stays
in Rust, including the virtual ellipsis row used by truncation. SDK component
measurement and painting still use the synchronous native target interface.

The [bidi differential](../../test/bidi_diff/README.md),
[font-table differential](../../test/font_diff/README.md),
[measurement differential](../../test/textm_diff/README.md) and
[real-font text/drawing oracle](../../test/text_golden/README.md) cover these
boundaries. The section oracle uses a synthetic renderer; its semantic metric
records are not counts of production cross-language calls.

## Native glyph drawing

`xp_native_draw_pixels` consumes placements from `textm::draw::walk`, using
the same ligature, scaling, fixed-point and font-table rules as measurement.
Horizontal drawing shapes bidi text; rotated drawing preserves its existing
unshaped, unscaled behavior. `xp-core::glyph` resolves borrowed bitmap views and
paints pixels, including clipping, orientation, gray planes and coverage scaling.
Standalone scaled glyphs use `xp_native_draw_scaled_pixels` for their advance and
bilinear scaling. Cached pages use the same Rust glyph painter through the
page render entry below; loaded text data belongs to the page arena.

C++ font facades dispatch preparation and cache work to Rust. SD and vector
containers belong to the Rust owner; native HAL and FreeType objects occupy
pinned slots whose lifetimes Rust controls. The renderer lends the selected
`EpdFont::data` pointer slot, bitmap-source view and exclusive
framebuffer or strip target for one synchronous run. Rust resolves resident
built-in/SD and decompressed page-buffer bitmap hits directly. Built-in misses
use the inline cache and decoder in `font::runtime::FontCacheOwner`, with the
existing Rust SmallTables inflater. Rust supplies SD views, bitmaps and miss
recovery, including absent-ID SD loading. Vector callbacks enter the Rust family
owner, which invokes native FreeType leaves for physical work. The source view
is refreshed after miss recovery.
Generation changes invalidate borrowed sources; no table or bitmap loan survives
a callback that can mutate its owner. The EpdFont owner stays live and serialized;
callbacks cannot unregister it, reenter drawing or change the target geometry.

There is no glyph-run allocation or new permanent buffer. Each placement is
painted before the next query. The
[glyph differential](../../test/glyph_diff/README.md) documents bitmap-view and
target contracts. `NativeCache` includes a drawing test that frees
the consumed glyph array and replaces the data slot inside emission, then checks
subsequent placements and pointers. The original public/private text and drawing
archives cover rendering; run `test/text_golden/mutation_check.py` with a dedicated
CMake build directory to verify independent rejection of pixel and advance changes.

## Reader Page ownership

`xp-ffi::app::epub::page_owner` owns one fallible DEFAULT-domain allocation per
reader Page: an eight-byte target header followed by the immutable decoded arena.
It uses the shared `xp-core::page` probe and in-place decoder. Rust queries borrow
arena records for each call; they do not retain resolved native views.

Session metadata outlives the rendered Page, so `page_owner::Metadata` copies
only its footnote and link records into two exact-size buffers (288 bytes per
footnote and 264 per link, at most 13,056 bytes). Replacement clears old targets
before allocation; failure leaves both counts zero. ResourceAccess guards these
buffers. Shutdown closes the Rust render resources, then releases links before
footnotes. The historical C++ metadata class exists only in host tests.

The session's inline `render_resources::PageResources` owns the frame, plane and
scratch buffers, optional strip storage and idle-prewarm scope. `ResourceAccess`
keeps each resource borrow within the existing render-lock scope. Storage is
initialized member by member at its final address; callbacks never borrow a
complete mutable session or frame.

`FrameLease` borrows the Page until rendering finishes. Cleanup frees owned
scratch, MSB and LSB buffers, detaches the source, clears the plan and image-window
loans, closes the prewarm scan and releases the image cache. Rust then drops
the Page. Session shutdown releases reusable strips and idle prewarm before Page
metadata. The typed `FontScan` loan carries its explicit font owner through begin,
finish and end. Shipping Rust calls these cache operations directly; host physical
leaves use `XpEpubFontScan` with `xp_font_frame_begin/finish/end`. Native physical
leaves perform drawing/display operations. The native `PrewarmScope` remains only
for shipping ClipSelection and Details callers, and for retained host oracles.
Dictionary and settings-preview Pages retain native
ownership. See the
[render-resource checks](../../test/reader_render_resources/README.md) for
production compositions and the explicitly retained parent oracles.

Only the retained ClipSelection child adopts Rust Pages through small native
facades. Rust footnote and lookup owners hold their single reader Page directly:
footnote copies its href while that Page is live, and lookup releases the Page
before opening Definition. Pending retains every transferred owner until all fallible
native construction succeeds; adoption then commits all owners without another
allocation or callback. A borrowed facade never frees the arena; an adopted one
calls the Rust destructor exactly once. Each transferred facade is 40 bytes on
C3/S3. The native Page header is also 40 bytes, eight more than before this
boundary, including dictionary/preview Pages. No second decoded arena is copied.
See the [Page owner checks](../../test/reader_page_owner/README.md) for the real
codec/consumer compositions and their host-only limits.

## Page rendering

The Rust EPUB owner calls `page_render::direct::paint` as a Rust function,
using its decoded arena and a synchronous physical target loan. Footnote and
lookup page views use the same direct path. Native Page clients (rich dictionary,
selection, settings preview and library Details HTML descriptions) enter that
painter through `xp_page_paint`.
Each physical strip is a separate pass. The pinned Rust page-resource owner
holds plan recording/replay state and detaches all page/plan/source loans before
releasing their storage; no native page-service table or plan-routing callback
is involved.
[`page::render`](../../rust/crates/xp-core/src/page/render.rs) walks the records
and draws text lines, rules, borders, backgrounds and drop caps. It controls word
offsets, focus splits, ruby and SUP/SUB shifts. Rust owns image cache,
placeholder, extraction and polarity policy. Typed native file operations and
the JPEGDEC physical leaf receive
paths and dimensions rather than a Page or image index. Inline native file
storage is pinned before placement construction and stays at that address
through its destructor. The image extractor borrows the Rust reader book directly.

Prewarm scanning batches visual codepoints in a fixed 192-byte Rust buffer,
flushing at line, font or style boundaries and when full. Rust resolves the
canonical style and records scan text directly in its font-cache owner.
Redirected SD residency borrows UTF-8 spans in at most 191-byte batches and
calls the Rust SD owner directly, without a native string copy. The synchronous render borrows existing font and framebuffer
storage; page traversal adds no heap allocation. Prewarm and bitmap misses use
the cache operations described above.

Clip selection reads positions and source ranges through borrowed page views.
`TextBlock::render` and `PageLine::render` no longer exist. Use the
[page oracle](../../test/page_golden/README.md) for page records and pixels and the
[preview and selection oracle](../../test/line_render_golden/README.md) for those
consumers' pixels and selection geometry.

## SD font and XTC parsers

`xp-core::sdfont` parses `.cpfont` headers, style directories and intervals,
validates glyph extents, decodes kerning/ligature fields, and parses filename
sizes. These parser calls borrow bytes and allocate no storage. `sdfont::load`
owns loading and shared-interval reference counts. `sdfont::{parser,owner,io}`
own stable faces, per-style storage and scoped physical handles; `control` owns
the loaded list and family name. The renderer's explicit font owner holds one
shared-interval registry and invalidates generation before table mutations.
`sdfont::catalog` owns discovery records; `control::reader_font`,
`control::update` and `control::variant` own reader font IDs, selection and
variants. Reader/chapter callbacks
use the explicit Rust font owner. Web font and settings routes derive ordered
family/size choices in Rust; native consumers copy and label borrowed results.
The native chapter owner-address leaf remains with P18-D2 until that context moves.

Firmware builds enable `sd-font-catalog` and `XP_SD_FONT_CATALOG` together.
PSRAM builds also enable `vector-fonts`; Rust owns vector sources, face lifetimes,
registration, the five-entry variant LRU and caches. Native startup/UI drivers
and Rust EPUB/XTC callers execute typed font effects synchronously, preserving
save/unload/load order. Under ruling 45,
the inherited settings-save callback re-enters settings serialization, whose
native settings and JSON loans are disjoint from reader state; SET-OWN owns its
retirement. Vector selection, loading and cache policy call Rust directly;
the native vector provider is retired from firmware. One Rust policy derives
selectable sizes; native settings consumers only copy and label the returned
choices.
No SD catalog/control/load/variant policy re-enters through a native effect.
The `7bab9148` native face/cache/manager/catalog implementations are preserved in
`test/sdfont_legacy`; older oracle snapshots remain in other test directories.
Their obsolete C declarations and callback descriptors are isolated in the
host-only `test/sdfont_legacy/abi` headers. Rust oracle exports require the
existing `app-test` feature and are absent from firmware builds.

C++ supplies the physical operations in
[`xp_sd_physical.h`](../../rust/crates/xp-ffi/include/xp_sd_physical.h), paired
allocation domains and diagnostic formatting. Native chapter prepare also
borrows the explicit owner address through `xp_native_chapter_font_owner`;
P18-D2 retires that accessor with the chapter/paragraph payload callers.
`xp_native_sd_font_view`, `xp_native_sd_font_resident_slice` and
`xp_native_sd_font_bitmap` remain only in the named host oracle adapter
`test/sdfont_legacy/gfx_sd_oracle.inc`. The
`xp_native_sd_font_load` provider is absent from firmware on both C3 and PSRAM
profiles; Rust loads absent SD and vector font IDs directly. Historical host
fixtures retain the provider behind test guards. The native allocation/free/diagnostic
leaves remain in `lib/EpdFont/SdCardFont.cpp`. The non-owning vector stream read
adapter is in `lib/hal/VectorFontPhysical.cpp`; it borrows Rust-selected prefix
bytes and a pinned HAL file and never calls Rust. See the
[SD font differential](../../test/sdfont_diff/README.md) for compatibility limits.

`sdfont::pool` owns resident glyph/bitmap buffers, prewarm and retention policy,
advance tables, mini kerning/ligature tables, coverage, overflow and statistics.
The Rust face lends exclusive slots and a synchronous service descriptor for each
call; callbacks cannot reenter the pool. Rust chooses allocation sizes and release
points through paired C++ callbacks: resident buffers use the existing
PSRAM-preferring allocator, scratch uses ordinary arrays. Cache operations can
allocate fallibly; unlike parsing, they retain buffers across calls. Views expire
when their cache owner mutates or releases storage. The
[historical pool differential](../../test/sdfont_pool_diff/README.md) checks ordered I/O,
allocation lifetimes, retention under heap pressure and failure recovery.

`font::cache` keeps the existing eight built-in page slots, hot decompression
buffer and glyph scratch buffer. Its allocation failures and release order are
covered by `test/font_cache`. `xp-ffi::font_vector` owns source storage, registered
families and variant selection; `xp-core::font::vector_owned` controls lazy face
initialization, glyph quantization, kerning, cache growth and eviction. Its
`registration` module publishes stable registry slots. Rust owns the cache buffers
through the existing font allocator and preserves its abort after a successful
preflight. C++ supplies eight FreeType physical leaves in
`lib/hal/VectorFontPhysical.cpp`. Sources and pinned HAL slots outlive every
borrowing face; registry entries are invalidated before family destruction.
`test/vector_font` compares the Rust owner against frozen native oracles using
real FreeType bytes, allocation traces and manager behavior; its lifetime tests
cover registration, shared sources and pressure loans.

`xp-core::xtc` interprets XTC/XTCH headers, page tables, metadata and chapter
records, and XTG/XTH page headers. C++ retains HalFile I/O, buffers, owned strings,
lazy chapter storage, streaming callbacks and bitmap rendering. XTC parser calls
borrow input bytes for one call and allocate no storage. Their compatibility
rules preserve existing permissive cases; the
[XTC differential](../../test/xtc_diff/README.md) describes those cases and checks.

## Image pixel pipeline

`xp-core::image` owns image scale/crop planning, row selection, grayscale and
alpha conversion, Bayer dithering, framebuffer writes, streaming 2-bit pixel
cache packing, and header dimension probes. `xp-core::png` owns PNG decoding and
BMP conversion; `xp-core::gif` exposes the local `xp-gif` decoder and BMP converter.
Both feed rows directly into the Rust pipeline. JPEGDEC remains the C/C++ decoder.
Native consumers own HalFile input/output and renderer targets. The reader owns
its pinned file lifetimes and cache I/O callbacks in Rust. `xp-core::assets`
owns image render/cache policy, JPEG-to-BMP conversion, screenshot packing,
and QR layout. The Rust page-image path in `page_render/images.rs` draws
placeholders and preserves polarity. Native code supplies typed file operations,
the extraction write sink, JPEGDEC operations and logging. Rust
selects PNG, GIF and JPEG by extension and calls the existing Rust operations directly. Two
pinned inline file cells lend source and cache handles; callback tables point to
Rust functions and retain no byte spans. Cache byte formats are unchanged.

PNG uses synchronous `xp_png_dimensions`, `xp_png_render`, `xp_png_bmp` and
`xp_png_sleep_header` calls. GIF uses `xp_gif_inspect`, `xp_gif_frame` and
`xp_gif_bmp`. Callbacks and contexts stay live for the call and cannot reenter;
input reads and output writes retain no byte spans. Target callbacks lend
exclusive framebuffer memory, and I/O callbacks preserve that loan. Decoder
rows stay within Rust. On the reader path, PNG/GIF callbacks also stay in Rust;
storage, clock, yield, heap, logging and the JPEGDEC operations below call native leaves. Other
consumers retain their native callback tables. PNG BMP conversion can borrow exclusive aligned scratch through the
C++ BuildScratch facade; Rust neither frees nor retains that loan.

The reader owns its JPEG pipeline inline and supplies Rust callbacks to the
unchanged `xp_book_jpeg` sequence. JPEGDEC and its file callbacks stay native;
its block callback is the one retained native-to-Rust continuation. Native facts
and Rust row state use disjoint storage during that callback. Cleanup closes the
JPEG file, aborts/drops the pipeline, then frees JPEGDEC, including decode failure.

Native consumers use the `xp_image_*` lifecycle in `xp_ffi.h` with caller-owned opaque storage. After a
successful init, call prepare once, optionally begin the cache, submit rows,
finish once, then destroy exactly once. Destroy also aborts an unfinished cache.
JPEG prepare is a no-op but follows the same lifecycle. The C++ adapter keeps
its RenderConfig, renderer and callback context alive until destruction. Row
and palette spans borrow decoder/context memory for one call. A target callback
lends exclusive framebuffer memory until the next target callback or the ABI
call returns; cache I/O callbacks must preserve that loan and cannot reenter.
Target geometry is physical, byte stride is explicit, and placement is logical;
compile-time assertions tie renderer modes and orientations to the ABI values.

Rust allocates PNG/GIF decoder state, reusable rows and conversion buffers
fallibly and releases them on success or failure. GIF keeps its 256-byte gray
palette with decoder storage. There is no per-row allocation. JPEG framebuffer
decoding needs no additional row buffer. JPEG-to-BMP conversion
owns an MCU buffer, output row and optional smoothing/scaling/dither buffers;
all are released at the end of the call. The shared Rust image owner retains
cache chunks and its path/read window until `xp_assets_image_reset(true)`. Clearing
render failures permits retry on the next page render without releasing that
window. Reader PNG and JPEG continue without caching when cache creation fails; GIF
fails as before. The reader no longer allocates a GIF converter object at decode
dispatch, so that allocation can no longer report an unsupported format. The
remaining native build-time probe still uses its factory until that probe moves.
QR bitmap callers own the output storage and must supply a non-null
pointer with the capacity returned by the plan call.

JPEG conversion buffers and the page cache window/chunks retain their former
default C++ allocation domain.
On X4 Pro, the narrow `xp_assets_platform_*` leaves use paired malloc/realloc/free
with DEFAULT caps (3), so the configured 4 KiB PSRAM preference applies. Rust
chooses allocation sizes, fallible growth and release points; failed growth
preserves the old allocation. C3 keeps its accepted internal allocation path.
The global Rust allocator is unchanged. Event numbers and diagnostic payloads
are documented at the asset ABI in `xp_ffi.h`. The
[PNG compatibility checks](../../test/image_golden/PNG.md) and
[GIF compatibility checks](../../test/gif_support/README.md) document consumer
limits, malformed-input rejections, corrected decodes and allocation-failure tests.

Framebuffer writes deliberately retain the former 16-bit byte-offset wrap.
Full targets above 65,535 bytes can therefore place pixels incorrectly; this
port does not establish larger-panel correctness. Changing that behavior needs
separate compatibility review. The [image oracle](../../test/image_golden/README.md)
records decoder rejection as well as successful output and verifies framebuffer,
cache, and target-query parity against the pre-port C++ implementation. A
successful header probe alone does not establish that the decoder accepts an image.

## Native Rust port gate (default X3)

The port check rejects new native references back into Rust and new unreviewed
Rust-callable imports. Run it on matching default-X3 build outputs:

```sh
python3 scripts/check_rust_ports.py
```

Every default-X3 PlatformIO firmware build runs an independent validation action
after its object/archive inputs, before any link or cached ELF restoration. It
also runs on an up-to-date build that does not link. `custom_rust_ports = enforce`
stops the build on violations; `report` prints them and allows the build to
continue for an explicitly requested audit. Running a toolchain directly or a
build that does not execute PlatformIO's post script bypasses this hook. Other
boards are not covered.

Roots are the Rust archive's undefined symbols defined by native objects and
all slots in six tables: EPUB, App, Legacy, Wi-Fi, network modes and Runtime.
`XpAppRuntime` has 27 static relocations and seven explicitly declared patches;
the size, header field count, relocations and patches must agree. Native
function/data-section relocations form a reference graph. Rust is only an archive
symbol table: no Rust code or linked instructions are analysed. Symbol names
such as `xp_native_*` do not determine language.

[`policy.txt`](../../scripts/rust_ports/policy.txt) holds these reviewed contracts:

- `import`: every direct root, including clean ones, with a signature/ownership
  review reason. A new native helper taking a Rust callback cannot enter silently.
- `callback`: function-pointer or callback-struct parameters of table slots and
  direct imports, detected from the preprocessed ABI headers. Opaque signatures
  and pointer retention still need manual review when an import changes.
- `dispatch`: a named native caller and a stored function or specific vtable
  method, including startup-created theme/panel objects and eviction sinks.
- `rustfill`: the native callers of each field in a Rust-filled table. Stable
  `Struct.field` targets represent Rust-private functions without compiler hashes.
- `physical`, `inherited`, and `deferred`: exact root/target permissions and task
  owners. NONREADER is a deferred owner; R5/R6/R7 remain deferred too.

`ports.lock` records sorted root/target pairs. `dynamic.lock` records each named
native table/vtable slot that reaches Rust, including slots outside the root
closure, plus the declared Rust-filled tables. Both reject additions and stale
entries. Declared port tables and Rust-filled tables use header field names, so
retiring a field does not rename survivors. Other tables, including vtables,
use indices relative to their own symbol; virtual-method reordering still needs
a reviewed mapping. File-local symbols include their object path; addresses and
internal lambda names are absent. A new root/target pair cannot reuse a physical exception;
a different Rust caller using the same root remains invisible. The summary
counts pairs by class and owner. “Common path clean” means no inherited/unowned
pairs remain, not proof of execution reachability. The initial baseline was `5aee6afa`,
after R1 and R3a; later units retire entries through reviewed lock diffs. Follow the [update and review rules](../../scripts/rust_ports/README.md).

This is a **reference check, not an execution proof**. Taking a vtable address,
infeasible branches, weak definitions and unlinked native objects can over-report.
The runtime bindings remain manual: project objects have no RTTI hierarchy, so the checker cannot infer
which stored pointer a particular function invokes. A new use of an existing hot
slot can still escape when its caller has no dispatch contract. Changes inside
Rust, callback lifetime, opaque pointers and previously unseen startup stores
remain R8b review work. The “any indirect call may reach any hot target” bound is
for the final audit only.

Prebuilt archives and ROM bodies are not walked. Weak `compiler_builtins`
fallbacks are excluded only when target `nm` finds a strong native runtime
binding that the default link resolves first. Native LTO sections and non-ELF32
RISC-V objects fail explicitly. x4pro/Xtensa remains **not determined**.

`scripts/reader_chain_guard.py` remains an on-demand linked ELF/map audit for R8b,
with unresolved indirect sites, direct/tail calls and object provenance. It has
14 physical notes, no large generated uncertainty baseline, and no CTest/build
gate registration. A passing port check does not complete that linked-image and
runtime-callback proof.

### Reader forwarding ports

Shipping EPUB code calls the BLE runtime owner directly for status, chapter-build
release, settling, heap occupancy, paint completion, switch-off and connection requests. The
native `radio_peer` slot only copies the linked device name and address. Host
oracles retain their original callbacks under `app-test`; the production adapter
is tested by `reader_direct_relays`, which compiles the shipping `ports.rs`
against stub owners.

Plugin subscription queries borrow the native eight-entry table only for the
synchronous Rust predicate. Refresh and queries remain on the app task; the
reader neither retains the pointer nor caches subscription results. Render specs
resolve their font through the existing settings snapshot, compute compression
in Rust, then read the remaining raw settings. R1c still owns the snapshot's
native font resolver; this change does not claim that inherited chain is gone.
No new allocation, cache or persistent state is introduced.

### App-loop HAL calls

The shipping app loop calls the existing Rust power, tilt and frontlight policy
directly. Native context getters borrow the existing HAL state and copy its
physical service callbacks for one synchronous call. They do not allocate, retain
a context or create a second state object. Power changes keep the existing mutex
and Wi-Fi check; tilt updates consume the activity flag while preserving pending
forward/back events. Frontlight output reaches the SDK only when the on state
changes, including the zero-brightness case.

The idle-slice loop runs in Rust with wrapping millisecond arithmetic. Its raw
native input sample reads the physical power button first and skips ADC reads
when it is pressed. Sampling does not poll or consume the debounced input frame.
Runtime keeps the same 34 pointer-sized slots: `idle_wait` → `raw_input_sample`,
`power_saving` → `power_context`, `tilt_update` → `tilt_context`,
`frontlight_state` → `light_state`, and `frontlight_set` → `light_output`.
`runtime_direct_hal` compiles the shipping adapter with the actual Rust HAL
modules and recorded physical callbacks with exact call-order assertions. Existing injected host callbacks remain
under `app-test` as HOST-RETIRE debt; no hardware tilt/frontlight result is inferred
from these host tests. Input mapping, BLE loop delivery, sleep orchestration and
settings serialization retain their separate owners. Native
`HalPowerManager::setPowerSaving` and `HalFrontlight::setOn`/`setBrightness`
entries still reach the same Rust HAL policy.

### App-loop input calls

The shipping Rust driver polls input in five ordered steps: native SDK poll,
direct Rust HAL edge update, native haptic feedback, direct Rust `input_polled`,
and native long-press reset. The mapped snapshot reads the resulting frame
without polling again. Native transfer loops keep `MappedInputManager::update`
with the same order and deferred Home-action delivery.

Mapped queries receive the current Rust Home action explicitly. The reader
caches only the native input-source table address. Its Rust service adapters
create a short-lived query context, call the native mapping/gesture primitives,
and consume pending tilt events directly in Rust, only when reader policy asks
for them. No frame, Home result, or tilt result is cached. The native query must
not retain that context or drain navigation. Existing native screens retain
their compatibility queries; they still call the shared Rust Home/tilt policy.

Runtime replaces `input_mapped_snapshot` with `input_mapped_query` and
`input_shared_power_mode` with `input_poll`, without changing its slot count.
EPUB replaces `input_services` with `input_sources`; facts and snapshot queries
take an explicit Home argument in their existing slots. The raw source table
reuses `XpUiReaderInputServices` with `XpEpubInputQuery` as the synchronous callback
context. `runtime_input_native` and `runtime_input_shipping` test those boundaries.
The old injected host signatures remain HOST-RETIRE debt, not shipping policy.

The qualified X3 and x4pro profiles disable haptics. Metalio retains the existing
native-to-Rust haptic policy continuation; this unit does not establish a clean
input boundary for that profile. SDK internals, stored hooks and ROM still need
the final R8b audit. This change does not resolve the inherited concurrent
frontlight read/write problem or move BLE delivery, sleep or screenshot policy.

## Reader protected-book boundary

The Rust reader probes every book for an encryption manifest before checking its
file type. It calls the Rust ZIP, package, wrapped-key and trusted-time code
directly, and owns the loan decision and error mapping. Missing ZIP end records
and manifests without a supported AES method retain their existing plain-book
classification. The manifest existence check uses an inline HAL file and does
not retain its name, so neither wrapper allocation nor name reservation can
misclassify a protected book as plain. Once a manifest is found, failure to read
or parse it returns `cannot read encryption manifest`. The inherited native ZIP
scan failures (SD open/seek/read, missing or invalid directory, and EOCD buffer
allocation failure) still classify as plain; they remain a separate hardening
item, not evidence that the book is unprotected.

Native primitives hold the SDK container index, protected stream, AES transaction,
and existing device-secret mutex/cache. Caller-owned temporary storage keeps the
index SD handle open through key reading and the loan check. AES cleanup still
happens when key reading returns. Rust clears its temporary content-key copy on
every normal return (aborting panics reset the device without running Drop).
The SDK keeps the session key for the existing decryptor lifetime.

Cache reclaim calls the shared Rust pressure sequence in native sink-priority
order: 50 render glyphs, 60 SD mini-cache, then 70 vector glyph arenas when
compiled in. `CROSSPOINT_VECTOR_FONTS` and Rust `vector-fonts` both follow
`BOARD_HAS_PSRAM`.
The stream opens its SD handle before reclaim. Persistent SD advances are retained.
The named physical continuation is the SDK protected byte stream, including
`lib/miniz/src/MinizCompat.cpp` calls to `xp_inflate_*` and its synchronous byte sink.

`openProtectedBook` remains for native EPUB callers such as sync, sleep covers,
home progress and thumbnails. The [protection tests](../../test/reader_protection/README.md)
compare this gate with the shipping Rust gate until native EPUB ownership moves.
No key format, protected-book error text or persisted book data changes here.
