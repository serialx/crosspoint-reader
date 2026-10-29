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
  --archive /path/to/native/libxp_ffi.a --mutation-check --sanitize --fault-check --review-check
```

The parity suite and mutation check are registered with CTest when ArduinoJson
is available. Sanitizers and the allocation sweep are separate options. Set
`WEB_TEST_ARDUINOJSON` at CMake configure time to use another installed copy.
`golden.jsonl.gz` contains 904 ordered transcripts recorded before the port.
Each record has the case name, responses/chunks, diagnostics, service calls,
final in-memory filesystem bytes (hexadecimal), and settings/credential state.
Generated source and actual transcripts stay in the selected build directory.
`--mutation-check` changes download, settings, font, plugin, and job behavior,
then suppresses synchronous disconnect handling; each mutation must be rejected.
`--review-check` asserts completion consistency after allocation failure,
translated file/HTTP/WS allocation errors, system-name filtering, release of the
parsed book-key body before storage, and zero Rust allocations in job routes.
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
used by these methods, including 32-bit saturating `toInt`. A targeted String
allocation hook covers completed font-list, plugin-list, and plugin-upload JSON
responses; other String allocation failures are not simulated. A real ArduinoJson
allocator substitute injects allocate failures for job submit/complete, while
reallocate is passed through. The transcript allocator trace covers only checked
upload/download buffer requests, not total heap usage. `--fault-check` separately
injects Rust allocation failures at 128 budgets for every case, checking that
Rust allocations and the WebSocket upload owner are released after cleanup. It
also requires identical job transcripts with the Rust allocator disabled.
Native `size_t` and
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

The settings extension was recorded from the original C++ at `22517c67`, before
its handlers were ported. It covers JSON scalar and container coercion, embedded NUL and escaped control bytes, dynamic
setting getters/setters, hidden/action entries, numeric boundaries, oversized
JSON entries, omitted/empty/wrong-type secrets, header slot clearing, store
failures, and synchronous WS disconnects during READY, PROGRESS, and DONE, including stop/start recovery.
During the allocation sweep, ownership and completion/file consistency are
checked before teardown, in addition to retained allocations afterward.
The separate settings-store oracle checks persisted bytes through the unchanged
store implementations; the route oracle checks the values and call order sent
to those stores. Neither substitutes for real browser/network testing.

The font-route extension was recorded from the unchanged C++ handlers at
`9ab0ddb8`. It covers empty and multi-family lists, escaped and oversized paths,
missing file stats, deletion JSON types and embedded NUL, upload validation,
PSRAM gating and all three vector signatures, 0/1/7/8/4095/4096/4097/8200-byte
chunks, directory/create/write failures, partial writes, cancellation, abort,
page revalidation, and embedded-NUL family state. Font discovery and FontInstaller are substituted service
leaves; the separate font-registry suites cover their Rust-backed implementation.
The compatibility cases intentionally preserve unchecked short magic prefixes.

Five review fixtures, also recorded from `9ab0ddb8`, cover empty, plain, JSON,
and fields-only font POSTs with no multipart file, plus failure of the final
font-list String allocation followed by a successful retry. The transport mock
rejects `upload()` access when no file is present. The String hook fails one
allocation matching the completed response length, in either the original
checked `reserve()` or the port's length-aware constructor.

The plugin-route extension was recorded from unchanged C++ at `dc21f50f`.
It covers manifest fallbacks and string types, missing and directory assets,
MIME suffixes, no-file requests without upload access, invalid plugin names,
256 KiB boundaries, short writes, abort/retry/replacement failures, book-key
expiry coercion and storage failure, and final JSON response allocation failure.
PluginLocations remains a substituted service; its separate oracle tests the
Rust discovery policy. The web oracle checks the entries and operation order
consumed by the route, not physical SD scanning or crypto algorithms.

Six additional plugin fixtures cover embedded NUL in names, filenames and upload
paths. The String substitute's indexOf uses strchr/strstr, matching the pinned
Arduino implementation: emptiness uses the stored length while searches stop at
NUL. Expectations come from dc21f50f C++; all prior 724 transcripts are unchanged.

The job extension was recorded from unchanged C++ at `0798b4e8`. It preserves
all previous 730 records and adds 174 cases: exact lease boundaries, completion
before/after expiry scans, repeated claims/completions, oldest-finished recycling,
clock wrap, stop/start versus a fresh server, enabled gates, missing bodies,
no-file requests, JSON coercion and size/name limits, NUL strings, status parsing,
and JSON allocate failures with retries. The String substitute compares C strings
through `strcmp`, matching the pinned Arduino implementation (embedded NUL ends
the comparison). A deliberate `>` to `>=` lease mutation must fail.

Failed ArduinoJson shrink-reallocation is not swept: the original C++ crashes
inside the pinned third-party StringPool on that injected failure. The diagnostic
is retained in the slice evidence; this port does not change that dependency.

Twelve additional original-C++ fixtures submit valid jobs before testing result
sizes around the 192-byte limit; their claims combine a 23-byte action with
191-byte serialized arguments. This reaches the oversized-result branch even
when an oversized argument would have rejected submission.
