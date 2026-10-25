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
