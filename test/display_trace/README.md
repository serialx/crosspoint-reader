# Display bus oracle

This freezes the C++ display contract at reader `539ebafc` and FreeInk SDK
`37d272d42732c31065ebed4d9cb90cef7bfa7416`. Use it to prove a replacement driver
sends the same panel bytes in the same order. No firmware source or SDK file is
changed. The oracle compiles the real facade, selected drivers, LUTs, board
profiles and bus code against host SPI/GPIO/time/semaphore adapters.

## Run

```sh
cmake -S test/display_trace -B build/display_trace
cmake --build build/display_trace -j 8
ctest --test-dir build/display_trace --output-on-failure
python3 test/display_trace/oracle.py list
python3 test/display_trace/oracle.py verify --bin-dir build/display_trace --case c3-uc8253-single
```

The full `test/` CMake project also registers `display_trace` and
`display_trace_sensitivity`. The sensitivity test builds a separate SDK copy in
its build directory, changes only UC8253's sleep command from `07` to `06`, and
requires the comparison to fail at `step sleep` with those exact bytes. It never
changes the submodule. Build/check requirements: C++20, CMake, Python 3 with
standard-library gzip/lzma, and the initialized SDK; no device or PlatformIO.

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

A future Rust runner can implement the same script and adapter protocol, expose
`display_trace_PROFILE_BUFFERS CONTROLLER SCRIPT --compact PAGE.raw`, and use:

```sh
python3 test/display_trace/oracle.py verify --bin-dir /path/to/rust/runners
python3 test/display_trace/oracle.py compare expected.trace.xz actual.trace
```

`verify` launches each runner twice, checks determinism, then checks the frozen
trace. `compare` accepts plain text, gzip or xz and reports the first differing
line, the current scenario step, and expected/actual operations. Transfer
chunking is part of this strict oracle; do not silently normalize different
chunks or CS/DC sequences.

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

## Rust UC8253 adapter

The Rust controller is opt-in: firmware builds use `FREEINK_RUST_PANEL=1`,
which selects the `display-panel` feature in the existing `xp-ffi` archive.
The SDK bus, reset, SPI transactions, and BUSY waits remain the trace boundary.
The default build continues to select the C++ controller.

```sh
cmake -S test/display_trace -B build/display-rust -DDISPLAY_TRACE_RUST=ON
cmake --build build/display-rust -j 6
ctest --test-dir build/display-rust --output-on-failure
```

`display_trace` still compares the unmodified frozen goldens. The additional
`display_trace_x3_extended` test compares C++ and Rust directly in both buffer
modes, with default LUTs, Direct mode disabled, a changed LUT/length config, and zero-length LUT payloads.
It covers Direct/Overlay/Absolute grayscale, missing planes, cleanup, recovery,
resync conditioning, async completion, and sleep/wake. `display_abi` checks
storage validation, descriptor versions, unsupported controller IDs, default
LUT export, and concurrent cancellation-generation increments. No test in this
suite accesses a device.
