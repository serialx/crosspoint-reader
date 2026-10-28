# KOReader sync oracle

`golden.txt.gz` records the C++ public interfaces at `1d95ed1d`, before the Rust port.
It checks document hash sample offsets and short/failed reads, credential JSON and
migration resaves, HTTP request/header/body order and status handling, reverse
XPath mapping with cache fallbacks, forward anchors, comparison, and diagnostics.
Inputs are synthetic and contain no real credentials or books.

Configure the host suites with `-DSYNC_JSON_DIR=/path/to/ArduinoJson/src`, build
`SyncOracle`, then run `ctest --test-dir build/test -R SyncGoldens --output-on-failure`.
The MD5 leaf uses macOS CommonCrypto, so this target currently runs on macOS.
The storage, HTTP, heap, obfuscation MAC, and section cache leaves are deterministic
stubs. The JSON library and sync implementation are real. The oracle does not
qualify the network or SD hardware.

`python3 test/sync_golden/oracle.py build/test/sync_golden/SyncOracle capture`
regenerates the archive; use only on the original implementation, never to accept
a port mismatch. The independent XPath suite covers XML mutations and allocation
failures. A separate negative control changes the first document sample from
offset 0 to 1 in an external source copy and must fail this golden.
