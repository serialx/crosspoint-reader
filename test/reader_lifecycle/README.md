# Reader lifecycle oracle

The compressed golden records 8d534a99e53f3bc98d842740de606b0396d38d56 before
porting reader policy. `extract.py` compiles selected production activity
methods against deterministic clock, lock, heap, storage and display leaves.
It does not keep a second implementation of those methods.

The trace covers session dwell thresholds and clock wrap, chapter/page turns,
queued turns during rendering and the 200 ms manual-turn gap, chapter skipping,
radio/build admission, background build failure and framebuffer loans,
progress bytes and failure ordering, link-stack persistence, refresh cadence,
link-note repaint, gesture settings and language detection. Progress cases
include old four-byte files, six- and ten-byte files, truncated/oversized reads,
stale last-page sentinels, invalid write values and temporary-file failures.

Run the `reader_lifecycle_oracle` CTest after building `ReaderLifecycleOracle`.
The extractor follows the production source at build time; candidate output
must match the frozen golden. Never regenerate the golden from a port.
Changing the extracted `section->currentPage++` to `+= 2` fails the comparison.

These host tests supply captured input at method boundaries. They do not model
GPIO debounce or prove that a physical tap wholly inside a blocking build is
sampled. Emulator input scripts and paired screen/heap/timing captures remain
necessary for the complete reader migration. Storage leaves record ordering and
bytes; they do not simulate FAT power-loss recovery. The display leaves record
requested refresh policy, not controller waveforms.

The render-sequence oracles compile the production `renderContents` body with
logging platform leaves, both with and without `XP_STRIP_RENDER`. Their 5,120
cases cover five board/controller combinations, text/image/placeholder pages,
the strip element limit, grayscale capabilities, refresh counters, failed
allocations and driver operations. They freeze ordered operations, cadence,
allocation attempts and diagnostics; dimensions use an 83-row panel to exercise
a partial final strip. They do not validate pixels or actual panel waveforms;
paired emulator captures provide that evidence.

The input oracle extracts the original ReaderUtils policies and records lazy
input probes as well as results. Repeated calls on one frame exercise release
suppression after LONG and tap-held-time overrides. The matrix covers button
combinations, all persisted short-power values, long-press boundaries, tilt,
swapped navigation, touch zones, RTL, swipe/menu gestures, Back and exact float
bits for chapter fractions. Renderer dimension reads are pure fixture values
and are not logged. No GPIO polling or real input scheduling is simulated.

Render generation 2 additionally records heap/capability probes, timestamps and
array allocation/release order. It uses the production 80-row strip size and
8-byte row bounds; generation 1 used smaller synthetic equivalents. Both were
captured from the original `8d534a99` method bodies before the render port.
The viewport oracle freezes 7,680 cases from the pinned SDK ListNav, including
stale row counts, partial pages, empty lists, clamps and no-op scrolls.

The full-loop oracle adds 4,608 two-frame scenarios from the unchanged original
EPUB loop. It records lazy input probes and resource calls for overlays, end-book
menus, automatic turns, queued turns, build gates, radio state and timed messages.
A busy render lock suppresses background work but leaves input sampling active;
the original pending-turn behavior is preserved. Inverting the final session-turn
direction is detected. The fixture uses host clock values within the uint32 range;
clock wrap remains covered by the session oracle rather than this host loop.

Progress lifetime cases distinguish closing a file from destroying its allocated
HAL implementation. The original destroys the temporary file before remove and
rename, including release of the implementation; an explicit close alone fails
the frozen trace. The prior 6,789-line lifecycle trace remains an exact prefix.

Full-loop generation 2 also extracts the production timing constants. The first
fixture supplied a 2,000 ms message duration; the source uses 2,500 ms. The corrected
capture includes 2,499/2,500/2,501 ms cases and was frozen before the activity port.

Full-loop generation 3 evaluates diagnostic arguments just as Logging.h does,
including the clock read inside the idle-prewarm debug log. A separate release
capture omits debug logs and their argument evaluation. Both captures come from
the original loop before its port; generation 2 only recorded the log event.

The shared ReaderActivity oracle covers 576 lifecycle/input configurations from
8d534a99: entry/exit ordering, remembered-book publication, end-book allocation
and actions, format/Back precedence, touch/buttons, skips and session flushes.
Its 50,580 trace lines include failed allocation and load, absent subscribers,
invalid document IDs and two loop frames around a render. The page renderer and
child activities are deterministic leaves; their own oracles cover their policy.
An inverted session direction is rejected at line 475.
