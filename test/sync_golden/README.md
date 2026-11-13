# KOReader sync oracle

`golden.txt.gz` records the C++ public interfaces at `1d95ed1d`, before the Rust port.
It checks document hash sample offsets and short/failed reads, credential JSON and
migration resaves, HTTP request/header/body order and status handling, reverse
XPath mapping with cache fallbacks, forward anchors, comparison, and diagnostics.
Inputs are synthetic and contain no real credentials or books.

The host CMake configuration supplies ArduinoJson (pinned in
`test/cmake/xp_test_arduinojson.cmake`). Build `SyncOracle`, then run
`ctest --test-dir build/test -R SyncGoldens --output-on-failure`.
The MD5 leaf uses macOS CommonCrypto, so this target currently runs on macOS.
The storage, HTTP, heap, obfuscation MAC, and section cache leaves are deterministic
stubs. The JSON library and sync implementation are real. The oracle does not
qualify the network or SD hardware.

`python3 test/sync_golden/oracle.py build/test/sync_golden/SyncOracle capture`
regenerates `size-boundary-golden.txt.gz`; use only on the original implementation, never to accept
a port mismatch. The independent XPath suite covers XML mutations and allocation
failures. A separate negative control changes the first document sample from
offset 0 to 1 in an external source copy and must fail this golden.

`reverse-golden.txt.gz` adds 11,600 reverse-mapping cases captured from an archived
copy of the same original C++ files: truncated and byte-mutated XML, malformed
XPath, chunk sizes 1/7/1024, five cache states, and cross-spine cache fallback.
Repeated unindexed and zero-index matches check that an earlier failed match
cannot leave a stale anchor or trailing bytes after a shorter replacement ID.
The anchor-only cache case also checks the resulting page selection.
Build `SyncReverseOracle` and
run `SyncReverseGoldens` to compare those cases. Its capture mode is subject to
the same original-implementation-only rule.

`size-boundary-golden.txt.gz` is the active forward oracle. It uses the same
original C++ source at `1d95ed1d` with one corrected storage fake: a size exactly
equal to a nonempty book's total maps to its last spine, as the original
`BookMetadataCache.cpp:390-403` does. The previous fake returned -1 there.
Interior boundaries and all other inputs are unchanged. The original capture
remains in `golden.txt.gz`. Before the corrected capture, the archived C++ source
with the original fake reproduced that archive byte for byte (874,918 bytes).
The corrected original C++ trace has 874,628 bytes; it was not captured from the
Rust implementation. `size-boundary-capture.json` records its source and hashes.
