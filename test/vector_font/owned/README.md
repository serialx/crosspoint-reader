# Vector owner qualification (CP1)

This target exercises Rust face and cache ownership before activation. Firmware
still uses the parent native path. `vector-owner-test` and `XP_VECTOR_OWNER_TEST`
are selected only by the host target; no shipping ABI table or field was added.

`frozen/TtfEpdFont.cpp` and `frozen/vector.rs` are byte copies from `79dec412`.
The frozen header adds eight name aliases to keep the oracle independent of the
candidate. `physical.h` and `native.rs` are host adapters, not shipping headers.
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
requests and peak/live bytes are reported separately, not used as glyph equality.
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

CP2 still owns integration of source allocation, the five-entry variant LRU,
UI fallback IDs, loaded-font registration/scanning and unload routes. The CP1
family tests do not claim those unimplemented integration paths. CP2 also owes
the complete S3 linked route audit, x4pro emulator boot/qualification and stack
watermarks. No emulator or hardware run is part of CP1.
