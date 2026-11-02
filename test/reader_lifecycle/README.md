# Reader lifecycle oracle

The suite retains the original 38 CTest declarations and 39 compressed golden
files. Those files record pre-port behavior and remain unchanged. Current EPUB
candidates call ordinary `EpubSession` methods in
`rust/crates/xp-ffi/src/app/epub/`; shared ReaderActivity/XTC and component checks
remain in the suite. Retiring a native EPUB owner or export must not remove its
original cases, shared callers or recorded expectations.

## Test boundaries

Historical fixtures use deterministic clocks, locks, heap, storage, display and
child values. Some also stub an inner policy method. Their host-only `app-test`
adapters preserve that boundary with named typed hooks. A matching outer trace
therefore proves the outer method under those supplied values, not execution of
the replaced inner method. Separate method tests cover those inner algorithms.
Observers record actual values before or after an operation and must not replace
them. Setup and read-only exports are distinct from policy hooks.

The historical adapters include supplied idle thresholds, repeated dirty link-note
repaints, a sync result of false, fixture-specific clear mutations and a fixed
clipping signature. These values belong to those fixtures; they are not additional
production behavior. The `app-test` feature and its `xp_app_test_*` and
`xp_epub_test_*` exports must remain absent from firmware.

`reader_app_epub_a1` separately exercises an App-owned session without policy
hooks: real entry, cached book rendering, page movement, toolbar display and
cancellation, forced refresh, final Update and exit. It runs both strip
configurations with asymmetric margins, a selected body font, text AA and nonempty
metadata/highlights. Native resource/frame and strip code execute over physical
fakes. This does not exercise the full parser, storage medium or panel.

`reader_app_epub_results` exercises native children returning to the actual App
owner. The native producer moves the result into callback-local storage. Child
exit and destruction occur under RenderLock, then the parent is restored and
published, the lock is released, and the ordinary EPUB result handler runs.
Borrowed text remains valid until delivery returns; retained values must be copied.
The matrix includes cancellation, repeated child returns, transferred Page destruction,
long text, stack allocation failure and pending shutdown. It does not replace the
native child widgets' own tests.

`reader_app_routing` uses actual App navigation for empty paths, bitmap formats,
XTC/XTCH, EPUB, TXT, Markdown and fallback paths, with case variants and a long
path. It checks native factory and resource counts, constructor refresh frequency,
Rust/native acquisition failures preserving the active owner, invalid path spans,
and rejection of a missing EPUB service table. These 57 cases use physical fakes.

The archive absence check in `reader_lifecycle_abi` honors `NM`. Set it to an
LLVM-compatible `llvm-nm` if the system tool cannot read the Rust toolchain's
bitcode archive members; symbol-reader errors fail the check.

ModelLock allows only bounded scalar reads and publication. Physical leaves run
outside it. ResourceAccess represents the existing nonrecursive RenderLock:
render methods borrow the caller's lock and loop operations acquire it where live
resources are accessed. Fixtures track these depths and preserve the distinction
between published scalar facts and live Section/Page reads.

Run the registered reader CTests in the configured host build. Candidate drivers
compare the original traces directly to their frozen compressed bytes, applying
only their explicit approved observation projections. Original hash/length checks
and projection counts remain active. Mutants change actual candidate source and
must reach the intended assertion; compile failures or arbitrary crashes do not
establish sensitivity. Never regenerate a golden from a port. Additive App and
resource tests use direct contract assertions and do not rewrite those goldens.

The composition tests extend the historical method coverage; they do not make
every historical hook path a composed App path. Parser/background progression,
connected radio, successful clock-sync restart, complete end-menu/Home workflows
and every Text/More choice retain their separate coverage limits. Host success
does not establish target stack, allocation budgets or device qualification.

## Original capture history

The initial compressed lifecycle golden records
`8d534a99e53f3bc98d842740de606b0396d38d56` before porting reader policy.
`extract.py` compiled selected original activity methods against deterministic
clock, lock, heap, storage and display leaves. It did not keep a second
implementation of those methods. The capture descriptions below preserve that
history; references to original method bodies do not require the retired EPUB
owner to remain in the current firmware or normal host build.

The trace covers session dwell thresholds and clock wrap, chapter/page turns,
queued turns during rendering and the 200 ms manual-turn gap, chapter skipping,
radio/build admission, background build failure and framebuffer loans,
progress bytes and failure ordering, link-stack persistence, refresh cadence,
link-note repaint, gesture settings and language detection. Progress cases
include old four-byte files, six- and ten-byte files, truncated/oversized reads,
stale last-page sentinels, invalid write values and temporary-file failures.

The original capture path built `ReaderLifecycleOracle` from the then-production
source. Changing its extracted `section->currentPage++` to `+= 2` failed the
comparison. Current candidates retain those expectations through the ordinary
owner and actual-source mutants; the old native cursor is not authoritative.

These host tests supply captured input at method boundaries. They do not model
GPIO debounce or prove that a physical tap wholly inside a blocking build is
sampled. Emulator input scripts and paired screen/heap/timing captures remain
necessary for the complete reader migration. Storage leaves record ordering and
bytes; they do not simulate FAT power-loss recovery. The display leaves record
requested refresh policy, not controller waveforms.

The render-sequence captures compiled the original `renderContents` body with
logging platform leaves, both with and without `XP_STRIP_RENDER`. Their 5,120
cases cover five board/controller combinations, text/image/placeholder pages,
the strip element limit, grayscale capabilities, refresh counters, failed
allocations and driver operations. They freeze ordered operations, cadence,
allocation attempts and diagnostics; dimensions use an 83-row panel to exercise
a partial final strip. They do not validate pixels or actual panel waveforms;
paired emulator captures provide that evidence.

The input capture extracted the original ReaderUtils policies and recorded lazy
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

The full-loop capture added 4,608 two-frame scenarios from the unchanged original
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
Its 53,786 trace lines include failed allocation and load, absent subscribers,
invalid document IDs and two loop frames around a render. The page renderer and
child activities are deterministic leaves; their own oracles cover their policy.
An inverted session direction is rejected at line 494.

Shared lifecycle generation 2 extracts the production end-book action enum as
well as method bodies. The first fixture listed all actions in a different
numeric order; generation 2 freezes the actual IDs before porting this code.

Shared lifecycle generation 3 records requested allocation and release sizes,
including nested menu paths and document IDs. Delaying the document release
until the outer onExit returns fails at line 64. Both helpers must release their
transient strings at the original boundary, before their caller continues.

Both lifecycle fixtures assert that every completed loop/render/lifecycle run
returns with zero render-lock depth; the full-loop fixture also tracks failed
try-lock scopes. These assertions stay enabled in both logging configurations.
