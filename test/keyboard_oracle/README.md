# Keyboard behavior oracle

`record.py` compiles the production keyboard state methods, URL tables,
layout-set policy and ButtonNavigator against the pinned SDK's real layouts.
Only Activity, input, time and result delivery are replaced by host leaves.
Text-point placement and rendering are excluded from this transcript runner.
`widgets.py` records the real SDK keyboard drawing operations and touch areas
with fixed host font metrics at four widths, ten languages, four pages and
button/touch configurations, plus the application's three URL layouts.
The emulator screen oracle supplies real-font pixel and caller coverage.

The split gzip transcript is one byte stream. It covers all 65,536 layout masks,
all layout keys and alternate outputs, four byte limits, UTF-8 editing, masking,
page cycling, cursor navigation, long-press thresholds and completion/cancel.
`test_oracle.py` also changes the production cursor increment to zero and checks
that the frozen transcript rejects it. The goldens were captured at ba6860d8
with SDK 61c49287 before the port.

Run `python3 test/keyboard_oracle/test_oracle.py`. For an archived source tree,
use `record.py --root PATH --out TRANSCRIPT`. Goldens contain synthetic text only.

After the port, `run_diff.py BUILD_DIRECTORY` links the production Rust modules
and compares both frozen byte streams. Original method bodies are read from
`git show ba6860d8`, so the firmware contains no legacy keyboard implementation.

CTest also checks wire layout, SDK label bounds, atomic capacity failures, and
a 10 KB zero-advance field. `KeyboardTarget` extracts the actual C++ resource
painters and the original field renderer. It compares normal, full-field and
long entries across four orientations, text/password/URL modes and cursor modes,
and compares full-frame rendering with guarded 16-row bands. `KeyboardAllocation`
extracts the production allocation, update/retry and view-build methods; injected
failures verify unchanged text/cursor, one log per failed held edit, no allocation
under the model lock, and no published partial scene when paint scratch fails.
It also checks that touch queries use the painted layout until the next frame
is published, and that ordinary password key taps allocate no cursor scratch.
Compact row resources use the same Rust geometry walker as the expanded golden
commands; the target test paints those rows through the actual facade.
Run all keyboard checks with `ctest --test-dir build/test -R keyboard_ --output-on-failure`.

`KeyboardTouch` compares 16,384 deterministic input frames directly with the
pinned SDK TouchHoldRouter, including press/release priority, alternate keys,
350/900 ms holds, slides, release slop, canceled swipes and timer wrap.

`keyboard_mapping` extracts the actual application table, SDK callbacks,
start/next wrappers and painter font choice. It checks both the pinned SDK IDs
and deliberately permuted IDs against the real layout tables.

`keyboard_routed_input` compares the original C++ with actual facade methods
through text bytes, cursor and returned results. Its 71 matching real-layout cases cover
pending shift/language/URL changes, masked multi-byte and RTL field taps, and
contacts spanning publication. It also checks the active key used for painting.
Glyph advances are fixed host leaves; bidi is real, and native-font pixels remain
covered by `KeyboardTarget`. The original stale-key fallback after a field tap
and a second release without a new press is preserved explicitly; a fresh
off-target press/release must clear it. No frozen golden is changed.

Two additional cases pin one deliberate safety deviation: suppress fallback
actions outside the published key count. The first renders URL letters into
both SDK generations, hold Enter at index 43, toggle URL snippets with hardware
Confirm, publish the 13-key page, then release off-target. The SDK fallback reads
the stale Enter slot beyond the published count and accepts the text. Rust drops
that release, keeps the keyboard open with unchanged bytes/cursor, and clears
the active contact. This slot is inside the SDK allocation but outside the valid
published table; the test records the old result without requiring Rust to copy it.
The second toggles URL with hardware Confirm while letters are still published,
presses the old URL key at (303,736), publishes snippets, then releases at the same
point, now a gap. The SDK reads stale URL slot 42 and toggles back to letters;
Rust clears the contact and stays on snippets. Both cases release while the
shorter page is published. The Rust shorter-page unit test returns to the larger
page before release, so it does not cover this fallback behavior.
