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

The release profile optimizes for size, enables fat LTO, and aborts on panic.
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
