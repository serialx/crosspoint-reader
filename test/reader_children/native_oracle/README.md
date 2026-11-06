# Native reader child oracle inputs

These ten immutable snapshots contain 97,080 bytes of public source from
`8389157f`, verified again at `e209b1b5` before the native owners were deleted.
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

The unused QR implementation and three retired ABI header snapshots were
removed after the switch, saving 120,896 bytes. Candidate compositions read
live production code and public headers. Retired callbacks and wire types
exist only in named host compatibility fixtures.

Snapshots are read as text, never compiled as production sources. Header
snapshots must never enter an include path or shadow candidate headers;
`reader_native_oracle_include_paths` enforces this for the CMake compile
commands. Do not regenerate snapshots or goldens from candidate output to
make a comparison pass. This directory adds no firmware source or ABI.
