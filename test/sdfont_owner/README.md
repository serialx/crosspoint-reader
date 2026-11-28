# SD font owner compatibility tests

The standalone harness links the production `xp-core::sdfont` modules. Rust
owns stable faces, per-style tables, scoped pinned file holders, cache policy
and the loaded-manager list/name. Native code supplies HAL, allocation and
formatting leaves. Interval tables can be shared across styles and faces.
Rust also owns SD catalog discovery, variant/fallback and absent-ID policy.
Native code performs physical storage/allocation; vector sources, variants and
caches also belong to Rust and are covered by `test/vector_font`.

Firmware enables Rust `sd-font-catalog` and native `XP_SD_FONT_CATALOG` together.
The separate system-oracle archive exercises that binding against frozen native
policy; other historical host fixtures retain their compatibility ABI. The
historical native implementations live in `test/sdfont_legacy`, outside firmware.
Their compatibility ABI declarations and renderer adapters are also test-only.

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
ctest --test-dir build/sdfont-owner -R '^SdFontSystemSwitch$' --output-on-failure
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
Catalog storage, dirty/update/variant policy and reader/chapter/web/XTC bindings
have shipping callers. SDK directory and FreeType inspection are physical leaves;
the Rust vector owner controls TTF source and face lifetimes, variants and caches.

Run python3 test/sdfont_owner/catalog_check.py after the CMake and Cargo builds
for a selected-record differential against the frozen native registry. It
compares nine families including 64 discarded vector candidates; it is not a
whole-heap or C3 std::string oracle. Focused tests cover every catalog allocation
failure, truncation when 129 families are discovered, rediscovery, selected vector
owners, full diagnostic paths, fresh update decisions, registry-slot mutation,
variant threshold/reuse/eviction/reload, and all owned buffers being released.

`SdFontOwnerSwitch` exercises the historical `SdCardFontManager` adapter against the Rust
owner: load, additional-size loads, current-name/ID queries, unregister and
unload, cache preparation, I/O/allocation failures and corruption notices.
`runtime_tests.rs` checks stable slots and tagged callbacks across cache mutation;
`layout.rs` checks the C3 storage layout. The production-front-door retained-name
cases are in `rust/crates/xp-core/tests/font_owner_binding.rs`:

```sh
cargo test --manifest-path rust/Cargo.toml -p xp-core --test font_owner_binding --offline --locked
MIRIFLAGS='-Zmiri-strict-provenance' cargo +nightly miri test --manifest-path rust/Cargo.toml -p xp-core --test font_owner_binding --offline --locked
```

`SdFontSystemSwitch` compares the frozen native system with the active Rust
system using the same host fixture, including variant IDs, save count/order,
and allocation/file/save failures. Its JSON snapshots model the settings fields
used by this operation; they do not establish full persisted-file or device
heap parity. The comparator accounts for host string layouts and compares
contiguous free multisets, so C3 allocation order still needs activation-time
traces. The Miri cases also cover rejected continuation tokens and the typed
chapter receiver: an SD load failure must not invoke native fallback policy.
EPUB and XTC execute effects synchronously at the parent's call sites. The
inherited save callback re-enters settings serialization; SET-OWN owns that path.
This system fixture does not establish full EPUB composition or persisted-file parity.

`SdFontCatalogFrontDoor` enables the catalog feature for production-front-door
tests, including inline/heap name loans, selectable sizes, web response bytes and
upload root/path effects. `SdFontXtcOrder` checks entry command order on success
and failure. Run both through CTest:

```sh
ctest --test-dir build/sdfont-owner -R '^(SdFontCatalogFrontDoor|SdFontXtcOrder)$' --output-on-failure
MIRIFLAGS='-Zmiri-strict-provenance' cargo +nightly miri test --manifest-path rust/Cargo.toml -p xp-core --features sd-font-catalog --test font_owner_binding --offline --locked
```

`SdFontResidentGrowth` exercises bitmap growth through `FaceOwner` with real
parsed glyphs, advances, kerning and ligatures. It checks each allocation failure,
all combinations of failed table moves, retained tables, republished views,
initial allocation, cache hits and teardown. The optional relocation adds no
resident request on a cache hit; existing prewarm codepoint scratch remains.

```sh
ctest --test-dir build/host -R SdFontResidentGrowth --output-on-failure
MIRIFLAGS=-Zmiri-strict-provenance cargo +nightly miri test \
  --manifest-path test/sdfont_owner/Cargo.toml --bin sdfont-owner \
  runtime_tests::growth_tests --offline --locked
```
