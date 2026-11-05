# Reader children ownership tests

These tests exercise the dormant Rust owners and the native reader child contracts during the ownership migration.

- `reader_children_ownership` imports the production owner/storage/result modules. It checks fallible construction, bounded command and text storage, result movement, application destruction/publication order, percent input/publication and toolbar navigation. The shared percent view capacity test covers all four orientations, touch/buttons, every value 0–100, all control active states and tap-flash states. An overflow composition records a diagnostic, retains the first 18 commands and publishes the complete interaction table through the Percent owner.
- `reader_percent_native_parity` compiles the current native percent loop and handlers, `ButtonNavigator`, and `UiAppHost` routing with the pinned SDK. It compares 2,568 input outcomes against Rust, including input injected during interaction-table construction. The synthetic control rectangles isolate routing; this test does not establish native drawing parity.
- `reader_chapter_native_parity` composes the current native chapter-window method with the production Rust cache reader and physical file leaves. A generated 1,000-entry cache covers nine windows, two mid-window file-read failures, exact labels, prewarm and reuse. It compares those actual cache rows with the Rust bounded window. The complete child adapter/resource guard is covered by the CP2 composition below.

The native source definitions must become named, frozen oracle fixtures before their production classes are removed. Keep this comparison rather than replacing its expected output with the new implementation.

The directory is also a standalone CMake project. Run it through the qualification toolkit's `ctest-parallel.py` with the current ten-test inventory, `--jobs 16`, and `NM=/opt/homebrew/opt/llvm/bin/llvm-nm`. The main host CMake tree includes the same tests.

Strict-provenance Miri runs the production ownership tests; the CP2 filters and limits are listed below.

CP2 adds the physical and App compositions:

- `reader_percent_native_drawing` uses extracted native Percent, slider, theme header/footer and utility-resource methods with the real renderer, fonts and pinned SDK. It compares 2,160 drawing/interaction cases, 1,080 real-owner cases and 144 header/footer/Back-target cases across themes, orientations, touch/buttons and both display polarities. Physical hardware facts are fakes; this is not device qualification.
- `reader_chapter_resource_guard` uses the real EpubSession, ReaderBook and resource guard for Chapters and Toolbar. Storage, prewarm and painting must occur under that guard; mid-window read failure remains covered.
- `reader_owned_app_children` opens all four children through actual App and EpubSession, drives input, and observes allocation release, parent publication, unlock and synchronous result delivery. It also covers retained native TextSettings confirm/cancel from the list and owned Toolbar preview. Physical leaves and file I/O use the existing native facade fixtures.
- `reader_toolbar_owned_parity` compares the real native Toolbar class/leaves with the Rust owner across 360 model combinations, including scene commands, resolved text, publication, navigation, touch routing, haptics and failed-paint rejection.

The standalone CTest inventory now has ten tests, including the three shared text/scene renderer tests. The full host tree includes the same child tests. No existing golden is regenerated.

The production `StableOwner` wrapper consumes each existing Box before a target is exposed. The Miri compositions cover a target retained across owner moves, parent and child moves through the real Application container, exact result movement, teardown order and drop-once after rejected stack allocation. The Miri parent/child lifecycle payloads are named host witnesses; the actual EpubSession integration is the separate native/Rust App composition above.

The exhaustive host command bounds are Menu 55, Chapters 56, Percent 18, QR 4 and Toolbar at most 78 (the matrix reaches 72). These are host bounds, not runtime command telemetry. Under Miri, skip only the five exhaustive drawing matrices: `shared_percent_drawing_fits_eighteen_inline_commands`, `menu_host_command_bound_for_all_values_and_selection_styles`, `exhaustive_chapter_command_branches_fit_fifty_six_commands`, `toolbar_command_branches_stay_within_panel_bound` and `exhaustive_menu_command_branches_fit_fifty_five_commands`. They run in normal CTest; allocation, overflow, input and lifetime cases still run under strict-provenance Miri.
