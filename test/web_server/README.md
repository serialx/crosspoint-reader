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
`golden.jsonl.gz` contains 1666 ordered transcripts checked against the original C++.
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
all previous 730 records and adds 175 cases: exact lease boundaries, completion
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

The final status-ID fixture covers leading vertical tab, accepted by C `strtoul`
but excluded by Rust `is_ascii_whitespace`; it rejects that parser mismatch.

The relay/crypto extension was captured from unchanged C++ at `8f4db011`.
All earlier 905 records are preserved. New cases cover relay HTTP status mapping,
32 KiB response boundaries and timeouts, JSON types and embedded NUL, crypto
base64/field limits, random length coercion, each checked allocation position,
primitive/encoding failures and JSON allocator failures. Dedicated assertions
check that parsed JSON and transport argument arrays are released before TLS,
the PKCS#12 request document before extraction, and upstream response JSON before
restarting suspended services. Crypto argument traces use
synthetic data only. Relay transport outcomes and primitive results remain
controlled substitutes; these cases do not exercise real TLS or cryptography.

The fetch extension was recorded from unchanged C++ at `ffd48814`. It covers
resume offsets and interrupted segment recovery, ignored Range and rewinds,
short bodies, browser disconnect and upload cancellation before/during transfer,
5-second heartbeat boundaries and 60-second waits, storage failures/short writes,
status codes, JSON coercion including NUL and integer overflow, and JSON OOM.
The endpoint never requests cancellation: polls feed watchdog/heartbeat even
after disconnect. The SDK transport is scripted; actual socket retries, timeout
implementation and task watchdog expiry are outside this endpoint oracle.

The String substitute models the pinned ESP32 core's capacity retention on move
assignment, including moving from an empty string, and deallocation on null
C-string assignment. `--review-check` tracks live host String capacity at parse
completion, TLS/PKCS#12 entry and service restart, including a 32 KiB response.
The 65,536-byte encoded-field case is rejected by the pinned ArduinoJson string
limit; a 65,532-byte field reaches PKCS#12. The measurements use host capacity
and SSO sizes, not the target allocator's rounding. Fetch routes are also checked
with the Rust allocator disabled and must make zero Rust allocation attempts.

The lifecycle extension was captured from the original C++ at `ffd48814` and
also checked against slice 7 at `7722739c`. The initial lifecycle capture preserved all earlier 1,544 records; subsequent
completeness repairs are described below. It covers repeated begin/stop/restart, STA/AP combinations, service
suspension during HTTP/WS uploads, discovery contents and repeated replies,
status fields, plugin normalization and hash input, serial validation, HTTP
allocation and WS restart failures, JSON allocation failure, long upload names
and paths, and static-resource caching. Initial WS allocation remains unchecked
in the original; injecting its failure would crash rather than produce a
transcript. These cases do not claim recovery from that existing failure.

The clock store wraps at 32 bits. The extractor explicitly uses 32-bit arithmetic
for the original fetch heartbeat and elapsed-time expressions, so the six
rollover fixtures check the ESP32 behavior rather than host `unsigned long`.
The SDK timeout remains a scripted transport result: the oracle checks its
60-second configuration and the endpoint's polling decisions, not socket timeout
execution. Other native-width limitations described above still apply.

To prove the Rust segment-boundary and heartbeat predicates themselves are
observed, run:

```sh
python3 test/web_server/mutate_fetch.py --build build/web-mutations \
  --arduinojson .pio/libdeps/default/ArduinoJson/src
```

It builds isolated Rust copies with each comparison changed by one operator and
requires a transcript mismatch. The String substitute now also stops lowercasing
at the first NUL, matching the pinned ESP32 core.

Nine static-route fixtures compare exact ETags, embedded NUL followed by matching
or different suffixes, initial NUL, and ordinary mismatches. Their body bytes are
recorded in bounded hexadecimal chunks so large assets cannot hide later events.
The shared cache helper now follows Arduino C-string comparison for the earlier
settings, fonts, and runner pages too. The complete suite was replayed against
the unported server at `22517c67`; one existing runner transcript was corrected
from that source because slice 4 (`0798b4e8`) accidentally used `sizeof(pointer)`
as its ETag length. All other earlier records remain unchanged at this step.

A later completeness audit found six historical transcript overflows: `pages`,
`registered-routes`, and `life-static:/files` lost trailing events after large
asset strings; `ws-synchronous-disconnect:DONE`, `plugins-list-extra:2`, and
`plugins-fs-extra:3` lost large filesystem values. Those six rows were recaptured
from the complete original server at `22517c67`, using 2,048-byte body/file
chunks, then replayed against the candidate. Other records were preserved.
Files smaller than 32,000 bytes retain their original `hex` field; larger files
use an ordered `hex_chunks` array. Large static bodies use `response-bytes`
followed by ordered `static-chunk` events. Concatenating the chunks recovers every
byte without changing the simulated response or storage operations.

The harness now aborts on events of 65,000 bytes or more, any ArduinoJson
overflow, or a missing event/file entry. Every extractor run injects an
oversized event to verify that guard. `WEB_TRACE_AUDIT=1` emits per-case event
counts, maximum event length and overflow state to stderr; all 1,666 complete
rows have equal source/recorded event counts and no overflow.


Review fixtures also check that static ETag scratch is released before every
response-header/send callback. They cover all nine routes with exact matches,
NUL variants, mismatches, and 4,096-byte matching/nonmatching values. The extractor
tracks the request scratch itself, so outgoing ETag header temporaries are not
mistaken for retained request data.

`discovery_budget.py` extracts the production discovery reply and checks frozen
original-C++ results under a focused ESP32 Arduino 3.3.11 String model. It models
the target SSO threshold, allocation rounding, live requested bytes, failed
concat, invalidating StringSumHelper sums, and the final copy. Goldens come from
`7722739c`; each row contains packet bytes, allocation requests, peak requested bytes, and
requested bytes live at send. The 36,936
cases cover 513 live-byte budgets and eight allocation failure positions plus
no injected failure, ports 81/65535, empty/long hostnames, the actual 30-character
station hostname, and a 29-character variant. This models String requests, not
physical allocator fragmentation or socket allocations. Run it with:

```sh
python3 test/web_server/discovery_budget.py --build build/web-discovery-budget
```

Four HTTP-pump cases dispatch registered fetch/relay handlers from the transport
callback, re-enter suspend/resume, and then pump discovery again. AP and station
IP stubs differ (`192.0.2.2` and `192.0.2.1`); a deliberate AP-to-station source
swap must fail. These cases and affected IP transcripts were captured from the
original server at `22517c67`. The complete corpus now has 1,670 records.
