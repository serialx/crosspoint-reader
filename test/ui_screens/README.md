# Emulator UI screen oracle

This harness captures a declared set of CrossPoint UI states and compares every
pixel and recorded refresh transition. It uses eink-qemu button injection,
synthetic SD-card contents, and a fixed RTC. It never opens a serial device,
flashes firmware, or contacts a real access point.

The baseline is firmware `539ebafcadd233bf11c68cdeb05c24454bdab22a`, with FreeInk
SDK `37d272d42732c31065ebed4d9cb90cef7bfa7416`. The Phase 9 evidence lives outside
the repository in `crosspoint-evidence/riir-round-2026-10/p9-o2/`.
The original schema-1 manifests are `complete-a/manifest.json` and
`complete-b/manifest.json`. The validated schema-2 generation is under
`p9-o2b/goldens-a/` and `p9-o2b/goldens-b/`: all 1,044 screenshots,
refresh sequences, and 430 startup intervals match. See `p9-o2b/comparison-final.json`.
Original provenance, timings, and raw recordings remain in `p9-o2/`.
Earlier pilot and interrupted recordings are retained as diagnostic evidence.

The settled-sleep generation is `p9-o2c/goldens-a/`. It retains every non-Sleep
record and PNG byte from `p9-o2b/goldens-a/`; only Sleep cases are recaptured
using the same archived golden firmware. `p9-o2c/frozen-a/` retains the previous
generation, and `p9-o2c/golden-diff.json` lists every regenerated case and changed
pixel/refresh digest. Use the settled generation with the current harness:
the older sleep scripts do not satisfy its completion contract.

The Library initialization generation is `p9-o2d/library-goldens-a/`. It
validates the same 430 cases and replaces only the four `library-actions`
recordings with fresh captures from the original golden firmware. The new script
waits for the fixture's first Library row and a settled panel before row input.
Both independent captures retain the original eight images and all refresh and
startup values. Source manifests and recording provenance remain intact.

The additional fixture goldens are `p9-o2d/fixtures-final-a/` and
`p9-o2d/fixtures-final-b/`, recorded from `p9-o2d/golden-final-build/`.
They use the original firmware/SDK revisions above plus the test-only entry
sources. Their 88 images, refresh records and 28 startup intervals match.
Separate Details title and Frontlight margin mutations fail pixels on both
boards and polarities. These are a separate generation from the original
430 cases; the two instrumented firmware builds are not interchangeable.

## Coverage

`screens.json` declares each group, settings, fixture, ordered button sequence,
expected activity, and screenshot name. Groups start on independent fresh cards;
screens within a group share navigation state. The original 66 groups and 168
named states expand into 430 runs and 1,044 images. Seven fixture-entry groups
add 28 runs and 88 images; they require the local build described below.

Both X3 and X4 run in normal and inverted mode. EPUB reader groups run with all
four orientation settings: portrait, clockwise landscape, inverted portrait, and
counterclockwise landscape. Other activities use portrait. This follows
`ReaderActivity.cpp:48,118` and `XtcReaderActivity.cpp:62`; XTC forces portrait.
Sleep intentionally ignores the normal inversion setting, so some polarity
pairs have identical pixels.

The matrix includes settings and their popups, language/layout lists, keyboards,
file and library actions, EPUB menus and toolbar panels, chapters, footnotes,
bookmarks, clippings, dictionary definitions, percent selection, QR display,
end-of-book actions, XTC pages, image display, five home themes, all nine sleep
choices, custom and cover sleep images, boot, SD failure, and crash display.

`coverage.json` accounts for every `.cpp` under `src/activities`, including shared
implementation files and activities whose offline entry displays Wi-Fi selection.
The host test fails when an activity source is added without an inventory entry.
This is a finite state-family matrix, not proof of every data-dependent screen or
every combination of settings. In particular:

- No access points are configured. Wi-Fi captures the empty list, hidden SSID and
  password keyboards, and failed connection. OPDS, Calibre, OTA, font downloads,
  clock sync, KOReader auth/sync, and plugin installation capture their offline
  gates. Successful connections, AP/save/forget lists, service responses,
  downloads, and online details require further fixtures.
- BLE captures the menu, enable, scan with no peers, and empty paired list.
  Pairing, mapping/learning, connected peers, and their errors need a BLE peer.
- X3/X4 do not expose USB mass storage, Home-button settings, or a detected
  frontlight panel. A test-only facade now supplies Frontlight pixels, and
  synthetic `DetailsSpec` values supply Details pixels without a service.
  Neither fixture qualifies the missing hardware or network behavior.
  X4 does not expose the RTC settings entry.
- Dictionary lookup/index busy paints are traversed but are not separate settled
  goldens. Misses, allocation failures, corrupted caches, failed writes, every
  intermediate loading paint, and all touch/remapping paths are not exhaustively
  injected. English built-in fonts and synthetic content define the text scope.

Use the inventory before treating a passing comparison as coverage of a new UI
path. Add a named state and repeat both complete captures when extending it.
`FIXTURE-GAPS.md` expands every declared gap and proposes the smallest relevant
service, capability, timing or fault fixture for later work.

## Prerequisites and build

Use Python 3.9+ and Pillow (`12.1.0` was used for the recorded comparison). Capture
itself uses only the standard library. Use the pinned eink-qemu 0.9.1 identity file
from the evidence harness; `capture.py` verifies both executable hashes before
running. The identity JSON must contain `cli`, `cli_sha256`, `qemu`, and
`qemu_sha256`, `cli_version`, and `qemu_version`. Use absolute executable paths
for capture. Digests and versions define executable identity; locations are
provenance only and may differ between captures. A relocated QEMU installation
also needs its unchanged ROM data: for this C3 build, copy `esp32c3-rom.bin`
into `../share/qemu/` relative to the QEMU executable directory. The recorded executables are:

| Executable | SHA-256 |
| --- | --- |
| eink-qemu 0.9.1, source `133cc53021735c5b48558f72a0a3fc2b4ab92887` | `af12104e0fcd1c9ee22fd7e1db5c9085cde212f50843975cbcb5973a15fa91f7` |
| QEMU 11.1.2, `v11.1.2-304-gcae7e68ef0` | `7b3d85928d792b1a64adcd550f378a34297c2b30516fb7b50586872ee058bad9` |

The `default` firmware detects X3/X4 at runtime. For refresh instrumentation, use
the existing SDK `SSD1677_PROBE_DEBUG` flag in an **ignored local** environment.
No firmware source or checked-in build configuration changes are needed:

```ini
[env:ui_oracle]
extends = env:default
build_flags =
  ${env:default.build_flags}
  -DSSD1677_PROBE_DEBUG=1
  -DCROSSPOINT_VERSION=\"2.0.0-dev-serialx/riir-p9-o2-ui-539ebafc\"
```

The fixed version string is deliberate: Settings, About, and Boot include it in
their pixels. Preserve it when comparing a port against these goldens. Build
through the shared lock when working in the Phase 9 environment:

```sh
EVIDENCE=/path/to/crosspoint-evidence/riir-round-2026-10
PATH="$HOME/.platformio/penv/bin:$PATH" "$EVIDENCE/harness/tools/pio-lock.sh" pio run -e ui_oracle
```

The build directory must contain `firmware.bin`, `firmware.elf`, `bootloader.bin`,
and `partitions.bin`. The crash fixture also needs the PlatformIO RISC-V GDB at
its standard package path; `capture.py --gdb` can select another installed GDB.
This logging build is for pixels and refresh policy, not performance equivalence
with a release build.

### Local activity-entry fixtures

To record the seven `fixture-*` groups, extend the ignored environment above:

```ini
build_src_filter = +<*> +<../test/ui_screens/firmware/UiOracleEntry.cpp>
build_flags =
  ${env:default.build_flags}
  -DSSD1677_PROBE_DEBUG=1
  -DCROSSPOINT_VERSION=\"2.0.0-dev-serialx/riir-p9-o2-ui-539ebafc\"
  -DCROSSPOINT_UI_ORACLE=1
  -include $PROJECT_DIR/test/ui_screens/firmware/UiOracleOverride.h
```

Keep this in `platformio.local.ini`; do not add it to shipped environments.
The one guarded call at the end of `setup()` invokes the test source. No selector
file means normal routing. The harness stages `/.crosspoint/ui-oracle-case.txt`
with an exact selector of at most 31 bytes, without a newline. Unknown, empty,
oversized or unreadable values log an error and preserve normal routing.

Details uses the real activity and its public `DetailsSpec`: plain and HTML
descriptions, placeholder and synthetic PNG covers, More, confirm and cancel.
Frontlight uses the real panel and physical-button handling. The forced-include
header replaces only the firmware `Frontlight` macro with an inert facade;
it supplies mono/warm capabilities and bounded brightness/on state without SDK
or bus calls. The fixture waits for the existing Home-painted predicate before
pushing the sheet, so its uncovered area retains the real Home screen. Touch
tiles, warmth gestures, slide switches, and real hardware remain outside this
fixture. `FixtureTest.cpp` is a host check and is excluded from the local firmware
source filter.

The additional goldens must come from the original firmware `539ebafc` and SDK
`37d272d4`, rebuilt with these same fixture sources and guarded setup call.
Keep that instrumented build's artifacts and hashes separate from the original
unmodified build. Do not combine unlike firmware artifacts into one manifest or
stamp old captures with new source hashes. Compare new fixture groups using
their own freshly recorded pair; use the retained original-generation harness
and golden manifest for existing captures until those are explicitly recaptured.

```sh
python3 test/ui_screens/capture.py --build FIXTURE_BUILD \
  --emulator-identity EMULATOR_JSON --out NEW_FIXTURE_CAPTURE --orientations 0 \
  --groups fixture-details-plain fixture-details-html fixture-details-cover \
  fixture-frontlight-mono fixture-frontlight-warm fixture-unknown fixture-oversized
```

All ordinary recording checks still apply: exact inputs, activity, every RGB
pixel, ordered API/controller refreshes, panel counts and startup intervals.
The rejection cases assert Home plus the rejection log. The cover case also
requires the PNG decode-completion log; Frontlight requires Home entry before
the panel is captured. To test sensitivity,
change one line in an activity's pixel-producing code in a separate scratch
build and require the corresponding fixture comparison to fail.

## Capture and compare

The full current matrix requires the activity-entry environment above; the
refresh-only build cannot reach the seven `fixture-*` groups.
Output directories must not already exist. Keep them outside the repository:

```sh
python3 test/ui_screens/matrix.py --build .pio/build/ui_oracle \
  --emulator-identity "$EVIDENCE/harness/tools/emulator.json" \
  --out "$EVIDENCE/p9-o2/full-a" --jobs 4
python3 test/ui_screens/matrix.py --build .pio/build/ui_oracle \
  --emulator-identity "$EVIDENCE/harness/tools/emulator.json" \
  --out "$EVIDENCE/p9-o2/full-b" --jobs 4
python3 test/ui_screens/compare.py "$EVIDENCE/p9-o2/full-a" \
  "$EVIDENCE/p9-o2/full-b" --json "$EVIDENCE/p9-o2/comparison.json"
python3 -m unittest discover -s test/ui_screens -v
```

`matrix.py` runs four independent board/polarity shards with one to four concurrent
emulators. Use `capture.py` directly for a focused run, for example
`--machines x3 --orientations 0 --dark 0 --groups reader toolbar`. It accepts the
same required build, identity, and output arguments. A focused run is not the
complete matrix. Compare it against the full baseline with the same explicit
selection applied to both manifests:

```sh
python3 test/ui_screens/capture.py --build CANDIDATE_BUILD \
  --emulator-identity EMULATOR_JSON --out CANDIDATE \
  --machines x3 --orientations 0 --dark 0 --groups reader toolbar
python3 test/ui_screens/compare.py BASELINE CANDIDATE \
  --machines x3 --orientations 0 --dark 0 --groups reader toolbar
```

Every declared screen in that selection must exist on both sides. Unknown groups,
missing selected cases, and missing selected screens fail; comparison never uses
the intersection of available screens. Omitted selectors cover all values on
that axis. Without any selectors, the original strict whole-manifest comparison
remains the default. The Python API accepts the same selection as a dictionary:
`compare(baseline, candidate, {"machines": ["x3"], "orientations": [0],
"dark": [0], "groups": ["reader", "toolbar"]})`.

Before porting an activity, look up its source path in `coverage.json` and take
the union of its `groups` with those of any touched shared implementation. Choose
board, orientation, and polarity coverage explicitly; a focused pass proves only
that selection. Read the inventory's gaps before claiming coverage of a new path.
For a new screen state, extend `screens.json` and `coverage.json`, then record two
new sets using the archived pinned baseline firmware and the new harness. Compare
them before recording the candidate. Give the new generation a fresh directory,
retain its suite/harness/build hashes and both accepted old sets, and never
replace baseline pixels with candidate output. If the pinned firmware cannot
reach the new state, it needs a separately reviewed oracle rather than a claimed
parity baseline.

Each case keeps its exact command, generated input hashes, button script, card,
console log, controller trace, screenshots, and emulator metrics. The root
manifest records executable/build/harness hashes, image byte and pixel hashes,
expected/observed activity, per-screen host and guest time, refresh counts,
controller busy time, and total host time. The matrix manifest also records the
sum of run times, which differs from wall time when shards run concurrently.

The comparison rejects failed captures, mismatched input or harness identities,
missing/extra screens, image tampering, unexpected activities, dimensions, any
changed RGB pixel, refresh-order changes, and refresh-count changes. It emits the
changed-pixel count and bounding box. There are no masks or tolerated pixels.
Firmware artifact hashes may differ so the same harness can check a candidate
port. Host duration and measured busy duration are reported, not equality gates.

## Refresh evidence

Each screenshot owns the interval from its `before-…` marker to its `after-…`
marker, including navigation transitions. The first screenshot also owns a
`startup` record from process boot (panel counter zero) through its `after-…`
marker. Its ordered API requests, controller signatures, and cumulative panel
count are compared independently. This covers the initial Boot-to-Home paint for
every board, polarity, and home theme, even when `ready` finishes after it.
For groups that navigate before their first shot, startup includes that navigation.
`refresh_requests` records the ordered
SDK calls (`full`, `half`, `fast`, `grayscale`, or `window`), not merely the last
paint. These come from `FreeInkDisplay.cpp:661,858,878` with the probe flag.

`controller_refreshes` independently records actual waveform evidence:

- X3: QEMU's `uc8279_refresh` trace is indexed by the marker's panel-refresh
  counters. The capture fails if the trace count does not match. It preserves all
  five LUT frame counts, PLL, and busy duration. This also covers asynchronous
  display calls that lack an SDK API-entry log. Despite the emulator trace name,
  this pinned machine's probe selects the firmware's **UC8253** driver. Its B/W
  full, half, and fast banks have 62, 25, and 19 frames respectively; first paints
  and resyncs may promote the requested mode (`Uc8253X3Driver.cpp:204–235`). Other
  frame counts belong to grayscale preparation/development. Retain both API
  requests and actual controller signatures; neither substitutes for the other.
- X4: SDK SSD1677 logs record full/half/fast and the controller `0x22` sequence.
  Host-dependent measured milliseconds are removed from the comparison value.
  Grayscale is recorded by the SDK grayscale entry log and panel-refresh count;
  not every grayscale subpass has an SSD1677 B/W mode log.
  The single boot paint includes its trailing completion log even when serial
  draining places it just after the screenshot's final marker; its log count
  must equal the measured panel count. No later navigation occurs in that case.

An empty API list can mean an asynchronous paint or no paint; inspect controller
records and counts. An empty controller list on an X4 grayscale interval does
not mean no refresh. The full raw evidence is retained for that distinction.

## Special fixtures

All books, dictionary definitions, plugin help, BMP patterns, and settings are
synthetic. EPUB ZIP timestamps are fixed. The XTC fixture reuses
`test/xtc_diff/generate.py`, whose source hash is recorded. Every case receives a
fresh card, fixed RTC `2026-01-01T09:00:00`, `--sleep off`, no turbo, no ghosting,
and no access points. Normal captures wait for serial quiet and a settled panel.
Sleep captures first wait for `Entering deep sleep`, emitted by
`enterDeepSleep()` after sleep rendering and display/storage shutdown, then use
the normal `wait-quiet:1` panel-settling step. A quiet gap during cover generation
is not completion. Extraction rejects a Sleep screenshot without that log line
inside its own capture interval and before the screenshot. The full ordered API
requests, controller waveforms, panel count, startup record, and pixels remain
equality gates; no trailing refresh is discarded or normalized.
Before each press, the emulator seeks 200 ms of guest log/panel/CPU idle. EPUB
reader groups additionally use `wait-quiet:1` for display stability before
each button. The first reader paint
uses `wait-quiet:2` for display stability so delayed font prewarming cannot swallow
Select. Bookmark/clipping-list navigation waits for the 2.5-second saved-message timeout
before opening the menu, keeping its redraw separate from button-driven paints.
The cache-warning group is an explicit exception: its waiting UI never satisfies
the emulator's CPU-idle heuristic, even with the default 200 ms threshold. That
group disables the CPU input gate and instead waits for display stability after
every button; its route, pixels, and refresh sequence still have exact checks.
QEMU limits its CPU-idle heuristic to three host seconds, which can expire under
host load even when navigation succeeds. These events are preserved in
`input_idle_timeouts`; they are not a substitute for the activity, image, and
refresh checks. Explicit guest-time settling keeps reader input reproducible.

Each case is independent, so an interrupted matrix can be completed with focused
captures and `assemble.py`. Pass source directories in order; later sources
explicitly replace duplicate case IDs. Use a fresh output directory:

```sh
python3 test/ui_screens/assemble.py ORIGINAL FOCUSED_RETRY MISSING_CASES \
  --out COMPLETE --allow-partial
```

`--allow-partial` permits a failed source manifest, but every selected case must
pass and the complete declared matrix must be present. Each schema-2 case stores
its fixture kind, full settings/config inventory, deterministic asset digests,
normalized SD destinations, navigation, pacing, and screen expectations. Assembly
regenerates this contract from the current suite and verifies the recording,
including the emulator's actual staged-file and pacing records. A fixture-only
change, extra/removed config, changed destination, or dropped press requires a
recapture. Assembly cannot stamp an unvalidated recording with the current suite
hash. Image hashes and extracted pixel/refresh/startup records must still match. It records all original manifest hashes and source
code hashes, every replacement, and the current extraction-source hashes. It
never alters raw recordings or images. Case `artifact_dir` and image paths are
relative to the manifest. Keep their relative directory layout when copying
assembled evidence. Historical absolute commands, executable paths, and source
manifest paths remain unchanged as provenance; they are not live artifact locators. Assembled total time is the
elapsed source-recording interval including retries, derived from source manifest
completion timestamps; per-case and per-screen times remain the recorded values.

The Phase 9 pair completes one interrupted X4 dark shard and recaptures library
search after adding an explicit Library initialization wait. Assembly also
associates the late X4 boot completion log with its measured single paint. The
exact original recording source is archived as `p9-o2/capture-v5-recording.py`.

Boot uses two display events with zero settle/panel-idle delay, because normal
readiness has already replaced the splash with Home. SD error ejects only that
case's emulated `sd-card` before mount. Crash uses a per-process Unix GDB socket
and forces the **second** `HalSystem::isRebootFromPanic()` result to true, then
detaches. The second call is the boot routing decision in `src/main.cpp:588`;
the first is HAL initialization. This renders Crash without causing a real
panic, writing a panic record, opening a TCP port, or touching hardware. If that
call ordering or symbol changes, update and revalidate the fixture explicitly.
These two fault images validate display routes, not recovery behavior.


## Explicit schema-1 migration

Schema-1 evidence is rejected by capture comparison and assembly. Migrate each
accepted recording into a new directory before using it:

```sh
python3 test/ui_screens/migrate.py OLD_COMPLETE --out NEW_GENERATION
python3 test/ui_screens/compare.py NEW_GENERATION_A NEW_GENERATION_B
```

Migration validates every case before creating its output. It regenerates the
expected deterministic inputs, checks the exact config inventory and staging
against retained emulator logs, validates navigation and pacing, then re-extracts
startup from raw logs. All previous image, activity, and refresh fields must agree.
The byte-exact original manifest is retained as `original-manifest.json`; the
migration record names its digest, source, tool digest, and validation performed.
Raw logs, inputs, traces, and images are copied with relative case paths. Disposable
`card.img` files are omitted. Original recordings are never rewritten.

A moved schema-1 tree can also be migrated: retain the manifest's relative image
paths and associated case directories. Migration locates cases from these paths;
it uses old absolute commands only to interpret case-relative input names.
Insufficient raw evidence or a mismatched deterministic fixture fails validation
and requires recapture from the pinned baseline.
