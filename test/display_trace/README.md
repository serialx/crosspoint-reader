# Display bus oracle

The default runners use the Rust panel drivers and preserve the C++ display contract at reader `539ebafc` and FreeInk SDK
`37d272d42732c31065ebed4d9cb90cef7bfa7416`. Use it to prove a replacement driver
sends the same panel bytes in the same order. No firmware source or SDK file is
changed. The oracle compiles the real facade, Rust adapter, board
profiles and bus code against host SPI/GPIO/time/semaphore adapters.

## Run

```sh
cmake -S test/display_trace -B build/display_trace
cmake --build build/display_trace -j 8
ctest --test-dir build/display_trace --output-on-failure
python3 test/display_trace/oracle.py list
python3 test/display_trace/oracle.py verify --bin-dir build/display_trace --case c3-uc8253-single
```

The full `test/` CMake project also registers these tests. The sleep sensitivity
test builds an isolated Rust workspace copy in its build directory, changes only
UC8253's sleep command from `07` to `06`, and
requires the comparison to fail at `step sleep` with those exact bytes. It never
changes the source workspace. The cancellation sensitivity test disables Paper
Mono's generation check in another copy and requires the overlap assertion to fail.
Build/check requirements: C++20, CMake, the repository Rust toolchain, Python 3
with standard-library gzip/lzma, and the initialized SDK; no device or PlatformIO.

## Controller and board coverage

Each row runs in single- and dual-buffer modes, in separate processes so driver
singletons and probe caches start fresh. All shipped environments use single
buffer mode; dual mode additionally protects the SDK's release/lend contract.
Release and RC profiles share the display flags of their named development profile.

| Runner profile | CrossPoint environments | Controllers / probe inputs |
| --- | --- | --- |
| c3 | default, slim, gh_release, gh_release_rc | X3 UC8253 and UC8279; X4 SSD1677, UC8179, UC8279 variants 02/03/67/68 |
| x4pro | x4pro and its release/RC profiles | SSD1677, UC8179, UC8279 variants 02/03/67/68 |
| x4c | x4c and its release/RC profiles | SSD1677, UC8179, UC8279 variants 02/03/67/68 |
| sticky | sticky and its release/RC profiles | SSD1677, SSD2677 (GPIO ID probe) |
| metalio | metalio_eink4 and its release/RC profiles | SSD1677, board-specific LUT configuration |
| papermono | papermono and its release/RC profiles | PaperMonoDriver / SSD1683 |
| a4 | eego_a4 and its release/RC profiles | UC8279C |
| picco | picco and its release/RC profiles | SSD1677 YRD0426, DEPG0397, OPM040B3, unknown fallback; SSD2677 SE0400NQW47 |

This is 60 facade cases plus one bus-only case. ED2208, M5Official, IT8951,
LgfxEpd and UC8253Murphy have no selectable CrossPoint environment in this
checkout and are excluded. Optional SDK tuning flags absent from CrossPoint's
`platformio.ini` are outside this matrix.

`scenarios.txt` is the ordered, stateful script. It exercises begin/clear,
full/half/fast updates, trigger/complete and trigger-async/finish in all three
modes, both framebuffer async APIs, polling, aligned and unaligned windows,
resync/conditioning, Overlay and Absolute grayscale using full planes and
19-row strips, cleanup, inversion, RED seeding, lending during a pending update,
secondary release/reallocation, full release/reallocation during a pending
update, maintenance, power-off, sleep and wake. The script intentionally calls
unsupported grayscale operations too; `gray.cap` and `gray.accepted` identify
rejection, and the golden records any fallback or no-op behavior. It does not
claim unsupported operations became available.

After initial conditioning, two shadowed async updates use distinct consecutive
frames, each followed by a wait. `frame 0` copies the current BW pattern;
`frame 1` complements the pattern before copying it. This sequence runs on every
profile and detects copying the new frame into the single-buffer shadow before
`displayStart` on all nine SSD1677 cases (X4, X4 Pro, X4C, Sticky, Metalio and four
Picco panel probes). UC8179 and UC8279-X4 also enter the shadowed path but ignore
the previous-frame argument; X3, A4, Paper Mono and SSD2677 use blocking fallbacks
for this API. Their traces preserve those behaviors.

The frame patterns are defined in `DisplayTrace.cpp`. Grayscale bytes mix the
full byte index, row and byte column with separate LSB/MSB seeds. At all three
geometries, every pair of rows separated by 8, 64 or 256 rows differs in each
plane. This distinguishes full-plane row-addressing faults such as repeating
the A4 source rows modulo eight; the first full-plane `gray-show` detects that
fault before strip uploads run.

A real 800x480 public page from `test/page_golden` is stored in
`fixtures/page-800x480.raw.gz`; provenance and hashes are in
`fixtures/provenance.json`. It uses the repository's table-edge
EPUB and public font. For X3 and A4, crop the rightmost columns and pad extra
bottom rows white; these are bus inputs, not a claim of native-layout rendering.
To use private page frames, pass `--page-dir DIR` containing `800x480.raw`,
`792x528.raw` and `768x552.raw`, and an evidence-only `--goldens DIR`. Keep private
frames and their traces outside the repository.

## Trace format and replacement drivers

`*.trace.xz` contains UTF-8 lines. `step` marks each facade operation; everything
until the next step is its ordered hardware activity. Integer pins are the real
SDK board pins. `pin.write` preserves every DC/CS/RST write, including repeated
writes; `spi.txn.*` preserves transaction boundaries and SPI settings. GPIO
bit-banging during panel detection is captured too. `spi.bytes` preserves each
transfer/writeBytes call and length, with exact hex for up to 32 bytes and
FNV-1a-64 for larger payloads (offset `14695981039346656037`, prime
`1099511628211`, modulo 2^64). Adjacent identical lines have `repeat=N`; expand
that line N times to obtain the original operations. SHA-256 of the complete
uncompressed trace is stored in `goldens/manifest.json`.

The runners expose `display_trace_PROFILE_BUFFERS CONTROLLER SCRIPT --compact PAGE.raw`.
To compare saved traces or use a separately built runner:

```sh
python3 test/display_trace/oracle.py verify --bin-dir /path/to/rust/runners
python3 test/display_trace/oracle.py compare expected.trace.xz actual.trace
```

`verify` launches each runner twice, checks determinism, then checks the frozen
trace. `compare` accepts plain text, gzip or xz and reports the first differing
line, the current scenario step, and expected/actual operations. Transfer
chunking is part of this strict oracle; do not silently normalize different
chunks or CS/DC sequences.

The UltraChip extension compares direct driver calls, including Direct grayscale,
sparse overlay versus image LUT selection, missing planes, invalid strips,
allocation failures and recovery. Build a second runner directory with
`-DDISPLAY_TRACE_RUST=ON`, then run:

```sh
python3 test/display_trace/ultrachip_compare.py \
  --cpp-bin-dir build/display_trace --rust-bin-dir build/display_trace_rust \
  --output /tmp/ultrachip-comparison
```

It checks all 34 UltraChip board/controller/buffer cases with successful
allocations, all allocations failing, and PSRAM-only failures followed by the
original fallback. A4 also fails after each of its five retained allocations.
The JSON report records paired trace hashes and allocation calls. The staging
script instruments only the selected SDK allocators; host STL allocations are
outside that trace. Failures write both complete traces for comparison.

For byte-level debugging, record into a separate directory with `--full`:

```sh
python3 test/display_trace/oracle.py record --bin-dir build/display_trace \
  --case c3-uc8253-single --full --goldens /tmp/display-full
xz -dc /tmp/display-full/c3-uc8253-single.trace.xz > /tmp/display-full.trace
```

`record` is the explicit baseline-capture operation. Do not regenerate checked-in
goldens to accept a port's differences. Fixture files are capped at 1 MB compressed.

## Fake hardware limits

`prepare.py` stages exact SDK sources in the build directory. Only EpdBus gains
a `trace::Wait` scope at each wait entry to supply scripted BUSY and identify the
wait boundary/polarity. On macOS, its two weak external hooks use `weak_import`
and explicit undefined-symbol linker allowances, preserving absent hooks as null.
The SDK working tree is never patched. Metalio power boot and Paper Mono PMIC
power/reset calls are recorded at the board-hook boundary, not at I2C register level.

Polarity numbers follow the SDK: 0 active-high, 1 active-low, 2 X3 two-phase,
3 UC idle-high. Normal facade waits begin BUSY and complete after 4 simulated
milliseconds. Outside a wait scope, BUSY is idle, so `poll` exercises completion;
this does not simulate concurrent refresh progress. `bus-scenarios.txt` separately
exercises 4 ms, 40 ms, no assertion, bounded timeout, ISR/semaphore and power-hook
paths for each polarity. The uncapped UC idle-high path is never given a stuck
BUSY signal. Poll timeouts appear as elapsed simulated time; semaphore waits
also record their requested timeout in milliseconds.

Real BUSY latency, delayed assertion races, ISR scheduling, physical refresh
quality, rail/I2C behavior, SPI electrical timing, allocation failures, and
ESP power-management locks are not modeled. ISR callbacks run synchronously when
the fake semaphore advances time. The oracle proves software sequences for these
inputs; it supplies no hardware timing, power, heap or pixel-quality measurement.

## Rust drivers and frozen extended cases

Firmware selects Rust controller features from the SDK board flags in the
existing `xp-ffi` archive. The SDK bus, reset, SPI transactions and BUSY waits
remain the trace boundary. The host archive enables all controllers with
`display-panel`; firmware enables only each board's selected families. Missing
Rust linkage is a build error.

`display_trace` compares the original 61 frozen goldens. `display_trace_extended`
compares 138 additional traces frozen at reader `e39d25c2` / SDK `62a72e7c`:
eight X3 cases, 112 UltraChip cases and 18 retained-driver cases. Each compressed
trace has a SHA-256 entry in `extended-goldens/manifest.json`.

X3 covers both buffer modes, default/custom/zero-length LUTs, disabled Direct,
missing planes, cleanup, recovery, resync, async completion and sleep/wake.
UltraChip covers register overrides, mounts, gray modes, windows and allocation
failures. Retained cases cover Sticky/Picco SSD2677 and Paper Mono allocation
size/caps/order, every PSRAM failure position, persistent failure, diagnostics,
committed/aborted status, stale planes, windows and idle/sleep/wake.

`ssd1677_config` compares 72 C++ traces frozen before the source cut: 24 config
and mount variants, each with debug absent, zero and one. It covers every config
field, null optional LUTs, custom precedence over the fast-DU shortcut, synchronous
and asynchronous refreshes, both log branches and grayscale calibration fallback.
`ssd-config-goldens/manifest.json` records hashes and diagnostic text.

The live ABI tests exercise invalid storage/version/size/alignment, capabilities,
borrowed LUT bounds, allocation failures and cancellation. SSD debug-disabled
refreshes make no clock callback; enabled refreshes report elapsed time correctly
across wrapping milliseconds. Paper Mono tests abort during allocation and a
started refresh, then verify recovery in a new generation. These are software
contract tests, not real-panel or task-scheduling qualification.

For a historical differential, export the pinned pre-cut SDK outside the source
workspace and select it explicitly:

```sh
cmake -S test/display_trace -B build/display-cpp \
  -DDISPLAY_TRACE_CPP_SDK=/absolute/path/to/exported-sdk
cmake --build build/display-cpp -j 8
python3 test/display_trace/retained_oracle.py --cpp-dir build/display-cpp \
  --rust-dir build/display_trace --out /tmp/retained-driver-evidence
```

That mode requires the old controller sources. It cannot select a C++ production
fallback in the current SDK. Keep private captures outside the repository.

## Reader strip contract

Strip uploads intentionally split a full-plane transfer. Compare reconstructed
controller RAM and the allowed window sequence instead of raw transcript
identity:

```sh
cmake -S test/display_trace -B build/strip-reference
cmake --build build/strip-reference --target display_trace_c3_single
cmake -S test/display_trace -B build/strip-trace \
  -DDISPLAY_TRACE_STRIPS=ON
cmake --build build/strip-trace --target display_trace_c3_single
python3 test/display_trace/strip_check.py \
  --reference-bin build/strip-reference/display_trace_c3_single \
  --candidate-bin build/strip-trace/display_trace_c3_single
```

The independent decoder requires full payloads, legal per-band command order,
correct row/gate direction, complete plane coverage and released CS before a
source callback. It compares RAM at logical plane, activation and operation
boundaries, and keeps waveform, wait, power and reset policy exact. Negative
checks corrupt payloads, ordering, windows, activation, old-frame reseeding and
bus ownership. For saved full transcripts use `oracle.py compare-strip
--controller uc8253|ssd1677 EXPECTED ACTUAL`. Ordinary framebuffer operations
must still pass the existing byte-exact oracle.
