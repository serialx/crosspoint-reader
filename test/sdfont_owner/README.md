# SD font owner compatibility tests

The standalone harness links the production `xp-core::sdfont` modules. Rust
owns stable faces, per-style tables, scoped pinned file holders, cache policy
and the loaded-manager list/name. Native code supplies HAL, allocation and
formatting leaves. Interval tables can be shared across styles and faces.
Native SD catalog/system discovery, variant/fallback and absent-ID policy
remain until CP2b; the catalog store and update/variant policy are not wired yet.

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
ctest --test-dir build/sdfont-owner -R '^SdFontOwnerSwitch$' --output-on-failure
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
safe Pin extraction to fail. The no_std library compiles the production types for
C3 so their layout assertions do not depend on the host pointer width.

The catalog/control tests cover C3 small-string capacity15, native
vector growth and final shrinking, canonical filenames without a name allocation,
selected/discarded candidate lifetimes, reader-size state and a five-record variant
LRU. Catalog allocation callbacks are a separate internal-heap binding contract;
container allocation inherits its binding failure classification while filename
allocation is fallible. No new allocator or abort is added by the Rust owner.
Settings/dirty-flag synchronization, native-origin diagnostics and SDK directory/
FreeType bindings remain CP2b work. Loaded-list/name/load/unload operations
already have shipping callers; catalog storage and update/variant policy do not.

Run python3 test/sdfont_owner/catalog_check.py after the CMake and Cargo builds
for a selected-record differential against the behaviour-unchanged native registry. It
compares nine families including64discarded vector candidates; it is not a
whole-heap or C3 std::string oracle. Focused tests cover every catalog allocation
failure,129family truncation, rediscovery, selected vector owners, full diagnostic
paths, fresh update decisions, registry-slot mutation, variant threshold/reuse/
eviction/reload, and all owned buffers being released.

`SdFontOwnerSwitch` exercises the shipping `SdCardFontManager` against the Rust
owner: load, extra-size/variant loads, current-name/ID queries, unregister and
unload, cache preparation, I/O/allocation failures and corruption notices.
`runtime_tests.rs` checks stable slots and tagged callbacks across cache mutation;
`layout.rs` checks the C3 storage layout. The production-front-door retained-name
cases are in `rust/crates/xp-core/tests/font_owner_binding.rs`:

```sh
cargo test --manifest-path rust/Cargo.toml -p xp-core --test font_owner_binding --offline --locked
MIRIFLAGS='-Zmiri-strict-provenance' cargo +nightly miri test --manifest-path rust/Cargo.toml -p xp-core --test font_owner_binding --offline --locked
```
