# Vector owner qualification

Firmware uses the Rust source, family and cache owner on vector-enabled boards.
This target compares that owner with the frozen native implementation. The
`vector-owner-test` and `XP_VECTOR_OWNER_TEST` gates also expose the owner and
physical leaves to standalone host tests.

`frozen/TtfEpdFont.cpp` and `frozen/vector.rs` are byte copies from `79dec412`.
The frozen header adds eight name aliases to keep the oracle independent of the
candidate. `physical.h` and `native.rs` are host adapters that reuse the
production physical contract.
The physical leaf borrows its inline slot, source bytes and pinned HAL file;
FreeType's stream callback never enters Rust. Rust owns the four face lifetimes,
six buffers per face, style selection, quantization, caps and pressure order.
Pinned family storage and glyph loans prevent cache addresses escaping mutation.

Run `cmake -S test/vector_font -B /tmp/vector-host`, build that directory, then
run CTest there. The differential uses the four existing public NotoSans fonts
and their OFL license, never private fonts. It compares exact output bytes and
metrics over sizes 6–72, both bitmap depths, all eight source masks with regular,
explicit/synthesized styles, ligatures, coverage, kerning, streamed reads and
prefix boundaries, corrupt/truncated/missing sources, failed reads and retry,
cache/table growth, cap flush, reload, release, and pressure retry. Allocation
requests and peak/live bytes are asserted equal as well as reported. The extended
differential also compares complete seek/read offset, length and return traces,
negative and late intermittent reads, and corrupt explicit bold/italic sources.
Host allocation counts do not measure ESP32 PSRAM/internal domains or stack use.

`VectorFontOwnedAbort` preserves the inherited abort if cache allocation fails
after a successful heap preflight. This hazard remains owned by **human decision**.
The frozen pressure stub preserves SDK sink order and early exit. The native
active-family guard is exercised; the Rust context borrows only disjoint owners.
No new sink is permitted at activation without resolving the ownership route.

Run owner tests with `cargo test --manifest-path rust/Cargo.toml -p xp-core --lib
font::vector_owned --features vector-owner-test`; repeat with `cargo +nightly miri`
and `MIRIFLAGS=-Zmiri-strict-provenance`. The compile-fail doctest checks the glyph
loan against cache mutation. Miri uses fake physical leaves, not FreeType.

The manager differential adds fourteen cases for source allocation, the five-entry
variant LRU, UI fallback IDs, registration/scanning, pressure, size changes and
unload. It compares the frozen native manager with production Rust using real
FreeType, and asserts allocation and HAL handle lifetime parity. The separate
`lifetime` crate runs the production manager with strict fake physical leaves;
run it under strict-provenance Miri to check stale handles, relocated tables,
scan loans, shared streamed sources and cleanup ordering.

Host parity does not establish target heap domains or stack bounds. Firmware
builds, linked route audits and runtime measurements belong in the qualification
packet. Variable/CJK/TTC fonts and stream reads beyond a 1 MiB prefix are not
qualified by these fixtures.
