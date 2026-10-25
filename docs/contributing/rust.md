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
The corpus includes every distinct corpus word, all registry languages and
aliases, unknown tags, separator and punctuation cases, long words, CJK,
combining marks, embedded NULs, and malformed UTF-8. Both Cargo and the host
C++ suite compare every output exactly. Do not regenerate goldens from Rust
to make a failing differential test pass.

The host CMake build compiles the same `xp-ffi` static library and links it
into the hyphenation and line-breaking suites. The existing F1 thresholds
also run through the C++ facade. `HyphenationCommon` retains only the explicit
and soft-hyphen predicates needed by `TokenBoundary`; its interface is unchanged.
