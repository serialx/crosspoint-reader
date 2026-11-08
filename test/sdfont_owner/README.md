# Dormant SD font owner parser

This standalone harness includes `xp-core/src/sdfont/parser.rs` by path. That
file is intentionally absent from the firmware module tree. It lends
Rust-owned stable style storage to the existing `sdfont::load` implementation;
it introduces no second decoding algorithm or firmware ABI.

The parser owns one file's style records, interval tables and optional kern
block indexes. Its interval registry shares identical tables between styles
of that file. Integrating the process-wide registry, cache and SDK file lifetime
is a later checkpoint; this parser alone does not complete SD font ownership.
The allocator must preserve alignment, return null on failure and pair every
allocation with its matching free. No pointer into the movable result escapes;
style records and their stub pointers remain in the stable allocation.

Run from the repository root, sequentially:

```sh
cargo test --manifest-path test/sdfont_owner/Cargo.toml --offline --locked
MIRIFLAGS='-Zmiri-strict-provenance' cargo +nightly miri test --manifest-path test/sdfont_owner/Cargo.toml --offline --locked
cargo clippy --manifest-path test/sdfont_owner/Cargo.toml --offline --locked --all-targets -- -D warnings
cmake -S test/sdfont_owner -B build/sdfont-owner
cmake --build build/sdfont-owner -j 2
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
