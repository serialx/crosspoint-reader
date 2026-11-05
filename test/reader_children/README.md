# Reader children ownership tests

These tests exercise the dormant Rust owners and the native reader child contracts during the ownership migration.

- `reader_children_ownership` imports the production owner/storage/result modules. It checks fallible construction, bounded command and text storage, result movement, application destruction/publication order, percent input/publication and toolbar navigation. The shared percent view capacity test covers all four orientations, touch/buttons, every value 0–100, all control active states and tap-flash states. An overflow composition records a diagnostic, retains the first 18 commands and publishes the complete interaction table through the Percent owner.
- `reader_percent_native_parity` compiles the current native percent loop and handlers, `ButtonNavigator`, and `UiAppHost` routing with the pinned SDK. It compares 2,568 input outcomes against Rust, including input injected during interaction-table construction. The synthetic control rectangles isolate routing; this test does not establish native drawing parity.
- `reader_chapter_native_parity` composes the current native chapter-window method with the production Rust cache reader and physical file leaves. A generated 1,000-entry cache covers nine windows, two mid-window file-read failures, exact labels, prewarm and reuse. It compares those actual cache rows with the Rust bounded window. The complete child adapter/resource guard is covered at the following integration checkpoint.

The native source definitions must become named, frozen oracle fixtures before their production classes are removed. Keep this comparison rather than replacing its expected output with the new implementation.

The directory is also a standalone CMake project. Run it through the qualification toolkit's `ctest-parallel.py` with an inventory containing the three names above, `--jobs 16`, and `NM=/opt/homebrew/opt/llvm/bin/llvm-nm`. The main host CMake tree includes the same tests.

Strict-provenance Miri runs the production ownership tests. Skip only `shared_percent_drawing_fits_eighteen_inline_commands` under Miri: that exhaustive pure drawing matrix runs in normal CTest, while the overflow and publication cases still run under Miri.
