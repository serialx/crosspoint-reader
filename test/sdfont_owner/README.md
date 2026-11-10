# Dormant SD font runtime

This standalone harness includes `xp-core/src/sdfont/parser.rs` by path. That
file is intentionally absent from the firmware module tree. It lends
Rust-owned stable style storage to the existing `sdfont::load` implementation;
it introduces no second decoding algorithm or firmware ABI.

The parser owns one file's style records, interval tables and optional kern
block indexes. Its borrowed interval registry shares identical tables between styles
and between files through a borrowed process registry. The dormant stable face
adds existing pool storage; pinned depth-two holders own operation-local handles.
These Rust modules remain outside the firmware graph. Six dormant HAL physical
exports are declared for native composition; no shipping caller uses them yet.
SD catalog/caller migration and shipping diagnostic binding remain later steps;
this checkpoint does not complete SD font ownership.
The allocator must preserve alignment, return null on failure and pair every
allocation with its matching free. No pointer into the movable result escapes;
style records and their stub pointers remain in the stable allocation.

Run from the repository root, sequentially:

```sh
cargo test --manifest-path test/sdfont_owner/Cargo.toml --offline --locked
MIRIFLAGS='-Zmiri-strict-provenance' cargo +nightly miri test --manifest-path test/sdfont_owner/Cargo.toml --offline --locked
cargo check --manifest-path test/sdfont_owner/Cargo.toml --offline --locked --lib --target riscv32imc-unknown-none-elf
python3 test/sdfont_owner/compile_fail.py
cargo clippy --manifest-path test/sdfont_owner/Cargo.toml --offline --locked --all-targets -- -D warnings
cmake -S test/sdfont_owner -B build/sdfont-owner
cmake --build build/sdfont-owner -j 2
build/sdfont-owner/sdfont_owner_physical
cargo build --manifest-path test/sdfont_owner/Cargo.toml --offline --locked
python3 test/sdfont_owner/run.py --oracle build/sdfont-owner/sdfont_owner_oracle --candidate test/sdfont_owner/target/debug/sdfont-owner
```

`oracle.cpp` serializes the frozen C++ parser already retained in
`test/sdfont_diff/oracle` from `9738db01f107a67f5ee392a45b26f97e90c65e06`.
It does not use the Rust loader. The comparator checks every output byte,
including acceptance, content hash, style presence, signed metrics, section
offsets, compact/full interval representation and every interval field.
Object padding, pointers and the not-yet-filled lazy kern index are excluded.
No golden is regenerated to accept differences.

The corpus uses the three public compressed Noto fixtures already under
`test/text_golden/fonts`, plus synthetic compact/shared, above-BMP, wrapping
and invalid interval cases. Header/TOC bit mutations, interval endpoint bit
mutations, count/offset limits and uint32 wrap, header truncations and every
whole-file truncation of small inputs are included. Private fonts are unused.

The Rust tests inject failure at every load-time allocation, read and seek
for compact, full-width and distinct interval tables. Required allocation
failures reject the load; optional kern-index failures retain the parent's
success behavior. Tests check partial cleanup, interval sharing, stable storage
across moving the result and final release. Host allocations and their sizes
are test infrastructure, not ESP heap or stack measurements.

The physical composition target uses the production HalFile declaration and the
production placement leaves, replacing SDK method bodies with an address-checking
host implementation. Passing an extra argument deliberately moves filled slot
bytes and must abort at the address guard before destruction. Rust tests cover
filled-holder relocation, nested slots, finish versus release, early returns and
read/seek/open/allocation failures under strict Miri. compile_fail.py requires
safe Pin extraction to fail. The no_std library compiles the dormant types for
C3 so their layout assertions do not depend on the host pointer width.
