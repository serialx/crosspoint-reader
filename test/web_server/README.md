# Web server compatibility oracle

This is the application oracle recorded from `CrossPointWebServer` at
`72224c26fc02cc9e53df8cb9ed7dd932e8ee1809`. It compiles the current implementation
against deterministic transport, storage, clock, settings, and dependent-service
substitutes. It does not open a network connection or
access a device or real SD card.

Run through the host CMake build after PlatformIO has installed ArduinoJson.
For a standalone run, build the native `xp-ffi` archive and pass its path:

```sh
python3 test/web_server/extract.py --build build/web-server \
  --arduinojson .pio/libdeps/default/ArduinoJson/src \
  --archive /path/to/native/libxp_ffi.a --mutation-check --sanitize --fault-check
```

The parity suite and mutation check are registered with CTest when ArduinoJson
is available. Sanitizers and the allocation sweep are separate options. Set
`WEB_TEST_ARDUINOJSON` at CMake configure time to use another installed copy.
`golden.jsonl.gz` contains 346 ordered transcripts recorded before the port.
Each record has the case name, responses/chunks, diagnostics, service calls,
final in-memory filesystem bytes (hexadecimal), and settings/credential state.
Generated source and actual transcripts stay in the selected build directory.
`--mutation-check` changes the download handler in the generated translation unit
and requires rejection.
`--record` deliberately replaces the golden and is only for reviewed baseline
captures, never for accepting candidate differences.

Coverage includes download validation and failures; folder creation, rename,
move and delete validation; HTTP upload buffer boundaries, collisions,
allocation/create/write failure and cancellation; WebSocket START validation,
owner/non-owner events, overflow and failed writes; plugin-file size and
replacement failures; plugin-job leases, stale claims, queue capacity, recycling
and JSON size limits; hidden/oversized file listings; and AP/CORS fallback.

These are application-level transcripts, not HTTP wire captures: WebServer's
HTTP parser, header encoding and socket implementation are substituted.
Storage and the dependent book/cache/library services are also substituted;
the test records their calls without validating their internal behavior or FAT
alias resolution. ArduinoJson is the installed third-party implementation.
Path helpers are extracted from repository source, and protected-path checks
use the existing public header. The String substitute implements the operations
used by these methods, including 32-bit saturating `toInt`; it does not inject
String or JSON allocation failures. The transcript allocator trace covers only checked
upload/download buffer requests, not total heap usage. `--fault-check` separately
injects Rust allocation failures at 128 budgets for every case, checking that
Rust allocations and the WebSocket upload owner are released after cleanup.
It does not simulate C++ String allocation failures. Native `size_t` and
`unsigned long` remain host-sized, so this suite does not establish target-width
overflow parity. Synthetic fixtures contain no private books or credentials.

All registered routes are invoked through the callbacks installed by `begin()`.
Additional cases cover lifecycle/registration, all static page handlers and ETag
revalidation, status, settings and credential updates (including omitted versus
empty secrets), font upload/delete, plugin discovery/files, every crypto operation,
relay, resumable fetch and book-key validation. Static response bytes are recorded
as hexadecimal; process-dependent server pointer values in logs are normalized.
The mock crypto operations return deterministic bytes, and fetch scripts model
transport results and callback order. These tests check endpoint policy, not the
underlying cryptographic algorithms, TLS, persistent store implementations, or
complete sets of settings/font descriptors. Dependency return values and all
input sequences are visible in `Endpoints.h` and `Cases.h`.

Use `--sanitize` to compile the host executable with AddressSanitizer and
UndefinedBehaviorSanitizer. The complete suite and mutation check passed with
those instruments when this baseline was captured. New scenarios must extend
coverage without rewriting existing expectations from a ported implementation.
