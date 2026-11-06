# Native reader child oracle inputs

These 15 immutable snapshots contain 115,063 bytes of public source. The first
ten come from `8389157f`, verified again at `e209b1b5`; the five footnote,
lookup and Page-transfer inputs were frozen at `65849737` before their native
owners were deleted.
`manifest.json` records each original path, byte count and SHA-256, plus the
pinned SDK and supporting sources. `oracle_sources.py` checks the hash when
a test reads a snapshot. Maintaining these retired sources is a test cost;
keep a snapshot only while a test consumes it.

Paths below are under `src/activities/reader/`. Each consumer is a CTest name.

| Snapshot | Consumers and extracted contract |
| --- | --- |
| `EpubReaderMenuActivity.cpp.txt` | `reader_more_actions_session`: menu item construction |
| `EpubReaderMenuActivity.h.txt` | `reader_more_actions_session`: action enum and item limit; `reader_child_handle_composition`, `reader_owned_app_children`: child declaration |
| `EpubReaderChapterSelectionActivity.cpp.txt` | `reader_chapter_native_parity`: TOC window loading; `reader_child_handle_composition`, `reader_owned_app_children`: constructor |
| `EpubReaderChapterSelectionActivity.h.txt` | `reader_child_handle_composition`, `reader_owned_app_children`: child declaration |
| `EpubReaderPercentSelectionActivity.cpp.txt` | `reader_percent_native_parity`: input handlers; `reader_percent_native_drawing`: slider drawing and callbacks |
| `EpubReaderPercentSelectionActivity.h.txt` | `reader_child_handle_composition`, `reader_owned_app_children`: child declaration |
| `QrDisplayActivity.h.txt` | `reader_child_handle_composition`, `reader_owned_app_children`: child declaration |
| `ReaderToolbarUi.cpp.txt`, `ReaderToolbarUi.h.txt` | `reader_toolbar_resource`, `reader_toolbar_owned_parity`, `reader_toolbar_publication`, `reader_failed_scene`, `reader_book_text_consumers`: resource, drawing and publication fixtures; `reader_chrome_toolbar_resources` also reads the implementation |
| `RustEpubResources.cpp.txt` | `reader_child_handle_composition`, `reader_owned_app_children`, `reader_toolbar_resource`, `reader_toolbar_owned_parity`, `reader_toolbar_publication`, `reader_failed_scene`, `reader_book_text_consumers`: historical factory and Toolbar leaves |
| `EpubReaderFootnoteSelectActivity.cpp.txt`, `EpubReaderFootnoteSelectActivity.h.txt` | `PageFootnoteSelect`, `ReaderPageTransfer`: native footnote entry, selection and href return; `reader_child_handle_composition`, `ui_runtime_rejection_interleaving`, `ui_runtime_rejection_families`: native shell and input rejection |
| `DictionaryWordSelectActivity.cpp.txt`, `DictionaryWordSelectActivity.h.txt` | `recursive_dictionary`, `reader_child_handle_composition`, `ui_runtime_rejection_composites`: lookup sequence, Page release, definition launch and rejection |
| `RustEpubPage.h.txt` | `ReaderPageTransfer`: retired footnote and dictionary factories and their ownership transfer |

`reader_selection_ownership` separately compiles the current App and Rust owners
to check result delivery, nested native selection, destruction order and the
stack-allocation-failure handler. It uses an `Owner::Legacy` root and calls that
handler directly; it does not force a parent-vector reservation failure. It does
not regenerate or read a native snapshot; its native viewer boundary is a host
witness. Paired X3 emulator captures separately cover repeated real-EPUB
cancellation return and Home teardown, but not noncancelled EPUB-root return.
The Page-transfer and recursive dictionary fixtures provide the native
comparisons alongside that composition test.

Historical factory/declaration scaffolding retires when every named consumer
uses current owners with equivalent transfer, allocation-failure and destruction
assertions. This includes extracted constructors, child declarations and the
historical factory wiring in `RustEpubResources.cpp.txt`. Drawing and input
references remain until independent equivalent coverage replaces them. Enduring
Footnote/Lookup rejection, Definition drop/move, App result ordering and
ClipSelection transfer references do not retire merely because O5a/O5b lands.
Retire a snapshot only after its last consumer has an equivalent independent
reference; never regenerate an oracle from the candidate to make it pass.

The unused QR implementation and three retired ABI header snapshots were
removed after the switch, saving 120,896 bytes. Candidate compositions read
live production code and public headers. Retired callbacks and wire types
exist only in named host compatibility fixtures.

Snapshots are read as text, never compiled as production sources. Header
snapshots must never enter an include path or shadow candidate headers;
`reader_native_oracle_include_paths` enforces this for the CMake compile
commands. Do not regenerate snapshots or goldens from candidate output to
make a comparison pass. This directory adds no firmware source or ABI.
