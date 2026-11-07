# Reader children ownership tests

These tests exercise the active Rust owners and the frozen native reader child contracts.

- `reader_children_ownership` imports the production owner/storage/result modules. It checks fallible construction, bounded command and text storage, result movement, application destruction/publication order, percent input/publication and toolbar navigation. The shared percent view capacity test covers all four orientations, touch/buttons, every value 0–100, all control active states and tap-flash states. An overflow composition records a diagnostic, retains the first 18 commands and publishes the complete interaction table through the Percent owner.
- `reader_percent_native_parity` compiles the frozen native percent loop and handlers, `ButtonNavigator`, and `UiAppHost` routing with the pinned SDK. It compares 2,568 input outcomes against Rust, including input injected during interaction-table construction. The synthetic control rectangles isolate routing; this test does not establish native drawing parity.
- `reader_chapter_native_parity` composes the frozen native chapter-window method with the production Rust cache reader and physical file leaves. A generated 1,000-entry cache covers nine windows, two mid-window file-read failures, exact labels, prewarm and reuse. It compares those actual cache rows with the Rust bounded window. The complete child adapter/resource guard is covered by the App composition below.

Native definitions are named, hashed snapshots under `native_oracle/`; its README lists every consumer and 21 snapshots / 149,035 bytes of retained source. `oracle_sources.py` reads them as text. Candidate code uses the live public headers. Historical callback tables and retired wire declarations live in explicit host-only fixtures. `reader_native_oracle_include_paths` rejects any snapshot path in compiler include arguments.

Run the standalone CMake project from the repository root with CMake, a C/C++ compiler, Python and the pinned Rust toolchain installed. The child test Cargo invocations use `--offline`, so their dependencies must be available locally; the shared host archive build uses `--locked` without `--offline`.

```sh
cmake -S test/reader_children -B /tmp/crosspoint-reader-children
cmake --build /tmp/crosspoint-reader-children --parallel 4
ctest --test-dir /tmp/crosspoint-reader-children --output-on-failure -j 16
```

The main host CMake tree includes the same tests. Its separate `reader_lifecycle_abi` test uses `nm` to inspect the Rust archive; if the system tool cannot read it, set `NM` to an LLVM-compatible `llvm-nm` when running that host suite. The standalone reader-children project above does not use `nm` or the `NM` override.

Strict-provenance Miri runs the production ownership tests; the filters and limits are listed below.

Physical and App compositions:

- `reader_percent_native_drawing` uses extracted native Percent, slider, theme header/footer and utility-resource methods with the real renderer, fonts and pinned SDK. It compares 2,160 drawing/interaction cases, 1,080 real-owner cases and 144 header/footer/Back-target cases across themes, orientations, touch/buttons and both display polarities. Physical hardware facts are fakes; this is not device qualification.
- `reader_chapter_resource_guard` uses the real EpubSession, ReaderBook and resource guard for Chapters and Toolbar. Storage, prewarm and painting must occur under that guard; mid-window read failure remains covered.
- `reader_owned_app_children` opens the Rust reader children through actual App and EpubSession, drives input, and observes allocation release, parent publication, unlock and synchronous result delivery. It also covers retained native TextSettings confirm/cancel from the list and owned Toolbar preview. Physical leaves and file I/O use the existing native facade fixtures.
- `reader_selection_ownership` composes the current App, Footnote, Lookup and typed Definition owners with native viewer/ClipSelection witnesses. It checks Page release before Definition, depth-three return, cancellation values and native-before-owner destruction. Its App root is a native `Owner::Legacy` witness; a separate stable `EpubSession` is the reader-child parent. Its stack-growth failure case calls `stack_allocation_failed` directly; it does not force a failed parent-vector reservation. Paired X3 emulator captures separately cover repeated real-EPUB cancellation return and Home teardown. Noncancelled EPUB-root return and an actual failed reservation remain explicit coverage limits.
- `reader_toolbar_owned_parity` compares the real native Toolbar class/leaves with the Rust owner across 360 model combinations, including scene commands, resolved text, publication, navigation, touch routing, haptics and failed-paint rejection.

The standalone CTest inventory includes the shared text/scene and Library header renderer tests, the include-path guard, C/C++ leaf-contract checks and both bookmark/clipping drawing negative controls. Use `ctest -N` for the configured inventory. The full host tree includes the same child tests. No existing golden is regenerated.

The production `StableOwner` wrapper consumes each existing Box before a target is exposed. The Miri compositions cover a target retained across owner moves, parent and child moves through the real Application container, exact result movement, teardown order and drop-once after rejected stack allocation. The Miri parent/child lifecycle payloads are named host witnesses; the actual EpubSession integration is the separate native/Rust App composition above.

The exhaustive host command bounds are Menu 55, Chapters 56, Percent 18, QR 4 and Toolbar at most 78 (the matrix reaches 72). These are host bounds, not runtime command telemetry. Under Miri, skip only the five exhaustive drawing matrices: `shared_percent_drawing_fits_eighteen_inline_commands`, `menu_host_command_bound_for_all_values_and_selection_styles`, `exhaustive_chapter_command_branches_fit_fifty_six_commands`, `toolbar_command_branches_stay_within_panel_bound` and `exhaustive_menu_command_branches_fit_fifty_five_commands`. They run in normal CTest; allocation, overflow, input and lifetime cases still run under strict-provenance Miri.

The clipping list drawing test extracts row-window methods from the hashed clipping-list
snapshots and drawing methods from the frozen native reader list. It compares
those pixels with the real Rust-owned child, real renderer, fonts and SDK at
792 × 528. Its 864 cases cover three themes, inversion, four orientations,
touch/button layouts, empty through 256-record lists, and beginning/middle/end
selections. The candidate help and bookmark leaves are extracted from
`RustEpubResources.cpp`; help offset, icon tint, icon offset and header-mode
negative controls must each produce a pixel mismatch. Snapshot `.txt` files are
extraction inputs only and never enter compiler include paths.

Clipping window, allocation failure, scene publication, long gestures and stable
owner/result transfer tests run in the Rust ownership suite and under Miri.
The owned-App composition checks the real store and the clipping list's
plain-text Definition entry, retained-parent return and allocation failures.
The drawing test independently checks pixels; it does not exercise App routing.

The owned-App clipping allocation-failure case observes the production log at the
fixture's final installed logging leaf, after its storage adapter is initialized.
It requires severity 1, tag `CLIP`, exactly `OOM: clipping list`, one event, and no
render update before returning to the retained reader. The original ownership and
allocation-count assertions remain. This observer is compiled only into the host
composition; it is not an app-test export or shipping callback.

## Bookmark owner

`reader_bookmark_native_drawing` compares the Rust Bookmarks scene and final
record labels with hash-checked native bookmark/list definitions, using the real
renderer, fonts and SDK. Its 1,440 cases cover three themes, two polarities, four
orientations, touch/buttons, five list sizes, three selections and swipe reveal.
The four negative controls alter help placement, icon paint, icon placement and
the trash bitmap. Both bookmark and clipping tests use the production
kind-aware `child_list_icon` callback; Bookmark is kind 0 and Delete is kind 1.

The production owner allocates once in place, retains a 24-row subtitle window,
and borrows the reader's one record list only during a call. XPath and summary
use existing terminated Text storage; names use final storage supplied by their
type. It starts at 40 commands, grows by16 to128 and reuses that arena. C3 layout
is936 B for the child (native1516),300 B for the subtitle window and56 B per
record (native88); this is object layout, not a measured peak heap reduction.

The Rust tests cover owner and result allocation failure, incomplete subtitle
publication, terminated labels, empty footer, stale swipe rows, held-release
consumption and resource rejection. They compose the actual Bookmarks owner,
StableOwner and Application engine with a synthetic parent to test failed
parent-vector reservation, pending replacement, cancellation and moved-result
ordering. The separate `reader_owned_app_children` composition uses the real
App, EpubSession, ReaderBook and Rust keyboard. It checks open/select/jump/back,
rename confirm/cancel/empty/128-byte names, repeated visits, delete and bad-write
semantics, owner/stack/record/command allocation failure, long press, swipe-start
coordinates and teardown. Its negative control rejects using release coordinates
for the swipe-start hit test. The moved keyboard block becomes the final name
allocation; failed persistence restores the old name allocation.

Run strict-provenance Miri with the `bookmark` filter in this crate; the separate
`bookmark_host` filter in `test/home_ui/app_owner` checks the synchronous raw TOC
callback into final subtitle storage. The App reader-owner and keyboard filters
cover the real moved-result slot and loans. These host compositions use physical
fakes; they do not establish emulator or device behaviour.
