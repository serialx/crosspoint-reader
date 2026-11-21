# HAL-FAKE-STRICT inventory: host fakes of the HAL file contract

Production contract, from `lib/hal/HalStorage.cpp` and `lib/hal/HalStorage.h`:

| State | How it arises | Consequence |
| --- | --- | --- |
| **has impl** (`impl != nullptr`) | every `HalStorage::open`/`openFileForRead`/`openFileForWrite` attempt, including a failed open and an empty path; every `openNextFile()` result, including exhaustion | the operation is legal and may return its failure value |
| **no impl** | a default-constructed handle, a moved-from handle | `close/read/write/seek*/size/fileSize*/position/available/isDirectory/rewindDirectory/getName/rename/truncate/flush/openNextFile` assert **before** any argument check, fault injection or early return |
| **is open** (`impl->file.isOpen()`) | independent of the above: `close()` keeps the impl, so a closed handle still answers those calls with failure values | `isOpen()` and `operator bool()` are safe and only report this; `modificationTime()` is also safe without an impl |

Fixtures distinguish impl presence from any open-state data they expose. A file close
retains the impl; moving or destroying its owner removes it. `test/hal_fake_strict.h` supplies the shared abort
(`HalFile fake: <op>() on a handle with no impl …`) plus `hal_fake::Impl`, a move-aware impl
flag that also makes the fake non-copyable. Destructor-bearing classes need explicit moves;
queries on a moved-from object must account for impl presence as well as other open-state fields.

`test/hal_contract/Contract.h` runs the shared contract against 43 C++ fake variants,
including local classes extracted verbatim and the native fixture's implemented methods.
It rejects every available asserted overload on default and both forms of moved-from handle;
safe queries remain false, and repeated close, failed-open/empty-path cleanup and exhausted
entry cleanup remain legal. The seven library-builder tests also reuse the operation checks.
Header adapters use the fixture's opening API; extracted local-class adapters install the same
state as their surrounding scripted storage code, isolating move/guard/destructor behavior.
Five deliberately infallible families (`sd_font`, `xtc`, `xtc_diagnostic`, `settings`, `opf`)
exercise post-open/empty-path cleanup but do not invent an open-failure mechanism.
Two additional C++ slot-table tests and the Rust tests exercise the callback boundaries.

## C++ `HalFile` fakes

Impl presence is separate from open-state queries wherever those queries are implemented; "move" says what a move does to the source.

| Path | Operations guarded | impl installed by | Move leaves source without impl | Notes |
| --- | --- | --- | --- | --- |
| `test/web_server/Host.h` | flush isDirectory close seek size available getName openNextFile read write | successful and failed `open`, `openFileForWrite`, `openNextFile` including exhaustion | yes (move ctor + assign) | `isOpen`/`operator bool` stay safe |
| `test/book_io/epub-stubs/HalStorage.h` | available read write flush seek seekCur close position size | every `open()` call, before the fault hooks | yes (move ctor + assign) | 64-bit ABI padding holds impl/limit flags; checked uint32 budget, described below |
| `test/chapter_html_slim_parser/stubs/HalStorage.h` | same as above | every `open()` call | yes | |
| `test/section_io/stubs/HalStorage.h` | same as above | every `open()` call | yes | |
| `test/page_golden/stubs/HalStorage.h` | same as above | every `open()` call | yes | |
| `test/page_diagnostics/file_stubs/HalStorage.h` | available read write flush seek seekCur close position size | every `open()` call | yes | |
| `test/page_diagnostics/stubs/HalStorage.h`, `test/page_codec_diff/stubs/HalStorage.h` | read write(x2) position size seek seekCur available flush close | both constructors and both `openFileFor*` (even when they fail) | yes (`hal_fake::Impl`) | transport fakes: no storage to open |
| `test/css_parser/stubs/HalStorage.h` | available read write seekCur close | every `open()` call | yes | |
| `test/text_golden/stubs/HalStorage.h` | seekSet read fileSize close | every `open()` call | yes | |
| `test/book_io/stubs/HalStorage.h` | seek64 seek read write fileSize64 size available close | every `open()` call | yes; explicit moves where a destructor is declared | |
| `test/library_builder/stubs/HalStorage.h` (+ `test/clipping_store/reader_storage.h`, which renames the symbols) | close isDirectory rewindDirectory openNextFile getName fileSize64 fileSize position seekSet seek seekCur available size flush truncate read write | `open`, `openFileForRead/Write`, `openNextFile` incl. exhaustion | yes (`hal_fake::Impl`) | contract suite host |
| `test/clipping_bytes/stubs/HalStorage.h` | isDirectory close flush getName openNextFile rewindDirectory fileSize size fileSize64 position available seekSet seek seekCur truncate read write | `PhysicalVolume::open` incl. failure and exhaustion (`closed()` helper) | yes (`unique_ptr` member) | |
| `test/dictionary_engine/stubs/HalStorage.h` | close position fileSize seekSet read read() write isDirectory rewindDirectory getName openNextFile | every `HalFile(Node*, path)` construction, including the null `Node*` of a failed open | yes; explicit moves where a destructor is declared | |
| `test/plugin_policy/stubs/HalStorage.h` | isDirectory fileSize fileSize64 close flush truncate write getName openNextFile | `open()`, before the fault | yes (explicit move ctor/assign) | destructor and move keep the `opened` test so the oracle trace is unchanged |
| `test/web_jobs/stubs/HalStorage.h` | fileSize read write size isDirectory available getName rename openNextFile close | `open()`, before the fault | yes | same trace-preserving destructor |
| `test/reader_lifecycle/stubs/HalStorage.h` (and `book_cache_fixture.h` via `HostFile`) | close write read flush | `openFileForRead/Write` incl. failure | yes; explicit moves where a destructor is declared | |
| `test/sd_card_font/stubs/HalStorage.h`, `test/sdfont_pool_diff/stubs/HalStorage.h` | seekSet read close fileSize | `open()`; `sdfont_pool_diff` installs it before `sdIoFails()` | yes | |
| `test/xtc_diff/stubs/HalStorage.h`, `test/h6_diagnostics/xtc-stubs/HalStorage.h` | seek64 seek read fileSize64 close | `open()` | yes | |
| `test/font_registry/stubs/HalStorage.h`, `test/sdfont_owner/system-stubs/HalStorage.h` | isDirectory close seek read size openNextFile getName | every open/`entryHandle`, including an unknown path | yes; queries check impl presence | |
| `test/sdfont_owner/native.cpp` | read fileSize seekSet close isDirectory getName openNextFile write(x2) | `openFileForRead`, `open`, `openNextFile` | yes (`live[&other] = {}`) | implements the production class declaration using a scripted handle map |
| `test/inflate_small/stubs/HalStorage.h` | close position fileSize seekSet read read() write | `HalStorage::open`, `openFileForRead` incl. failure, `markOpen()` | yes | `DictProbe` marks its opened output sink |
| `test/settings_store/stubs/HalStorage.h` | position available read write seek | `open()` (the stub has no volume) | yes | `Oracle.cpp` prepares the buffered sink |
| `test/sync_golden/stubs/HalStorage.h` | fileSize seekSet read | `openFileForRead` always | yes | |
| `test/zip_file/stubs/HalStorage.h` | size position available seek seekSet seekCur read write(x2) close | `openFileForRead` incl. failure | yes | `seekCur` guards before the target check |
| `test/library_index_file/stubs/HalStorage.h` | close read fileSize64 seekSet | `openFileForRead` always | yes | |
| `test/content_opf_parser/stubs/Epub.h` | close seek position available size write read | `markOpen()` from both `openFileFor*` | yes | |
| `test/reader_lifecycle/book_open_oracle.cpp` | close | native open leaves assign unique_ptr Impl, including failure | yes, explicit ctor/assignment | object counter is separate from handle ownership |
| `test/reader_lifecycle/book_resource_oracle.cpp` | close seek size read | local openFileForRead before failure | yes, explicit ctor/assignment | transfers open flag and cursor |
| `test/reader_lifecycle/book_scratch_oracle.cpp` | close seek read | local openFileForRead | yes, explicit ctor/assignment | destructor counts objects |
| `test/reader_lifecycle/book_txt_stream.cpp` | close size read | local openFileForRead | yes, explicit ctor/assignment | constructor/destructor count objects |
| `test/reader_lifecycle/book_txt_validate.cpp` | close read size | local openFileForRead before failure | yes, explicit ctor/assignment | transfers path/cursor; destructor checks impl |
| `test/reader_lifecycle/book_lifetime_oracle.cpp` | read | local openFileForRead before failure | yes, explicit ctor/assignment | transfers open flag; destructor records owned-file cleanup |
| `test/home_ui/cache_leaves.cpp`, `test/home_ui/theme_leaves.cpp` | close | local openFileForRead before failure | yes, explicit ctor/assignment | local abort helper, no bool/isOpen query |
| `test/ui_updates/effects.cpp` | isDirectory fileSize close openNextFile getName | open/openFileForRead; every openNextFile result | yes, explicit ctor/assignment | move resets valid/root/index/next/impl |
| `test/hal_policy/check.cpp` | real `lib/hal/HalStorage.cpp` over a fake `SDCardManager` | production | production | unchanged |
| `test/vector_font/owned/HalStorage.h` | PT-VEC fixture | out of scope by task | | untouched |

## Physical adapters underneath the HAL (own callback contracts)

These are not raw `HalFile` operations: each replaces a native leaf whose C++ body decides what
the HAL sees. They are guarded to the contract of the leaf they replace.

| Path | Leaf contract | Guard | Why not more |
| --- | --- | --- | --- |
| `test/sdfont_owner/switch.cpp` | `xp_native_sd_file_*` → `HalFile` ops (`lib/hal/SdFontPhysical.cpp:19-30`) | slot word 2 is the impl: `open` installs it before the injected fault, `read`/`seek`/`close` require it, `handle_destroy` clears it | matches `HalFile` exactly |
| `test/sdfont_owner/system.cpp` | same leaves, scripted byte source | `Slot::file` (the shared stub) carries the impl; `open` assigns `Storage.open(path)` before the fault, `read`/`seek`/`close` require it | matches `HalFile` exactly |
| `test/xtc_owner/rust/boundary.rs` (`parent::Services` **and** `XpXtcLeaves`) | app/xtc file leaves → `HalFile` ops | `File::handle` set by `open`; `close`/`read`/`seek`/`size` assert it; `is_open` stays safe | matches `HalFile` exactly |
| `test/sdfont_owner/catalog_fixture.rs` (`catalog::Directory`) | `directory_open` assigns a `HalFile`, `directory_next` calls `openNextFile`, `directory_close` destroys+reinitializes the slot (`font_catalog.rs:103-114`) | `open` installs the handle per root/child slot (unknown path included), `next` asserts it, `close` clears it | close stays safe, as the leaf's destroy/init does |
| `test/sdfont_owner/catalog_tests.rs` (`LongPath`) | same directory trait | same | same |
| `test/sdfont_owner/growth_tests.rs` (`pool::Services` open/close/read) | the pool's table callbacks are `io::Files` over a slot (`font_sd.rs:162-182`), whose `close` returns at depth zero and `read` returns zero (`io.rs:92-112`) | keeps that outer contract: a depth counter (two slots), depth-zero read returns `true` only for zero bytes without seeking, depth-zero close returns, a third open is refused; the HAL rule is enforced in `Physical` below | the HAL assertion does not belong in this callback layer |
| `test/sdfont_owner/runtime_tests.rs` (`io::Physical` slot table; three pool tables use `closed`/`finish`/`no_read`) | `init/destroy/open/read/seek/finish` per slot; file close keeps the impl, directory close destroys+initializes | `State.handles` records slots whose open installed a handle: `read`/`seek`/`finish` assert it, `init`/`destroy` clear it, `finish` keeps it; `open` installs it even when the injected I/O fails | the `closed/finish/no_read` pool tables never open, so their safe no-op close matches the outer callback contract above |
| `test/cmake/xp_ffi_host.cpp:230` | weak fallback for `xp_native_sd_file_close` | aborts unconditionally | not a permissive implementation |

## Rust file-I/O tables and File traits

| Path | Table | Guard |
| --- | --- | --- |
| `rust/crates/xp-core/tests/support/font_web.rs` (`io`, listing route) | `XpWebIo` `OpenRead`(17) `Size`(5) `Close`(2), unknown ops panic; three-slot `impls`/`open` | `Size`/`Close` require the slot's impl, `OpenRead` installs it; repeated close and size after close stay valid (size is zero); indices >=3 return zero |
| `rust/crates/xp-core/tests/support/font_web.rs` (`upload_io`) | `IsOpen`(3) `Create`(15) `Close`(2) | `Create` installs the impl, `IsOpen` reports the open flag, `Close` requires the impl and keeps it; all three slots and out-of-range indices are tested |
| `rust/crates/xp-core/tests/support/font_catalog.rs` (`directory_open`/`directory_next`) | slot open + iterate | `directory_open` installs the handle for every attempt (unknown/disabled included); `directory_next` asserts it; each attempt initializes node/index, and failed-open iteration returns false |
| `rust/crates/xp-core/tests/font_owner_binding.rs` (`xp_native_sd_file_open/read/seek/close`) | slot position + read/seek/close, handle word 2 | `open` installs the handle before the injected failure; `read`/`seek`/`close` assert it before fault injection; `close` keeps it |
| `test/bookmark_bytes/rust/export_tests.rs:35-109`, `test/clipping_store/rust/volume.rs:217-255` | Rust `File` traits: a successful open returns an owned handle | boundary is the owned handle; Rust move semantics expose no moved-from value, so there is no default-handle path to guard — classification only |
| `test/reader_page_owner/PageOwner.cpp:99-128` | file leaf table with close validation but unvalidated read/seek/size/position | returns the stable address of `filePosition`; this is a scripted cursor token, not an allocated HalFile or default HAL slot |
| `test/reader_lifecycle/app_owned_epub.cpp:360-376` | file callbacks that already validate acquired handles through the fixture's handle/release mechanism | already strict; listed for completeness |
| `test/reader_lifecycle/child_native_faults.cpp:21-29` | `ImageIo` OpenCache/Close callback model | OpenCache installs impl presence even on failure; Close requires it and keeps it; direct negative and failed-open cleanup tests run in the native-child suite |
| `test/reader_lifecycle/book_{writer,package,zip,css}_oracle.cpp` | derived `HalFile : HostFile` wrappers that add lifetime instrumentation | inherit `test/chapter_html_slim_parser/stubs/HalStorage.h`; explicit moves retain the wrapper lifetime counters |
| `test/reader_lifecycle/book_text_adapters.cpp`, `test/test_recursive_dictionary.py:661` | empty `HalFile` shells with no operations | nothing to guard |

## Fixtures that must prepare a handle before driving a HAL lifecycle

| Path | What it prepares | Why |
| --- | --- | --- |
| `test/chapter_html_slim_parser/ChapterHtmlSlimParserTest.cpp` (`ChapterTestAccess::finish`), `test/chapter_diagnostics/Diagnostics.cpp` | a closeable parse handle via `Storage.openFileForRead(parser.filepath)` | these isolated final-layout tests drive `finishParse()` without the documented `beginParse()`; the handle is *prepared*, not a full successful parse lifecycle, and `Diagnostics` opens it after enabling allocation failure |
| `test/gif_support/GifBmpTest.cpp` | reopens only when the previous handle is open | a default member handle must not be closed in setup |
| `test/inflate_small/DictProbe.cpp` | `markOpen()` on the output sink | the decoder writes into an opened file on device |
| `test/settings_store/Oracle.cpp` | `file.open()` on the buffered sink | same |
| `test/home_ui/cache_leaves.cpp`, `test/home_ui/theme_leaves.cpp`, `test/ui_updates/effects.cpp` | local `unopenedFile()` guard with the same diagnostic | their golden harnesses copy them out of the tree; local guards and explicit source-clearing moves are contract-tested from the extracted class |

## Bounded stdio write budgets and ABI

The four budget-bearing stdio fakes (`book_io/epub-stubs`, `chapter_html_slim_parser/stubs`,
`section_io/stubs`, `page_golden/stubs`) explicitly support limits from 0 through
UINT32_MAX. `limitWritesTo` aborts for larger size_t values instead of truncating them.
An unlimited/default stream also aborts if a write request exceeds its remaining
UINT32_MAX lifetime allowance; it cannot silently report an exhausted narrowed budget.
This is an explicitly bounded test API, not an assertion of equivalence over all size_t values.
Existing oracle workloads do not set a write limit; the new contract tests exercise that API.
The small fixture files remain far below the lifetime allowance.

The contract checks pin a 64-bit size_t ABI and sizeof(HalFile): 48 bytes for the
book/section Print-derived fakes and 40 bytes for chapter/page. The flags reuse padding;
there is no claim of the same size on a 32-bit host. Boundary checks exercise UINT32_MAX,
UINT32_MAX+1, SIZE_MAX, short writes at a one-byte budget, and the default-stream bound.

## Other file-like fixtures

`test/bookmark_bytes/stubs/HalStorage.h` models SDK FsFile/SDCardManager and locks, not a
HalFile wrapper; unopened FsFile is valid under its own SDK contract. The `hal_policy`
suite tests the real HalFile implementation. `test/vector_font/owned/` remains explicitly
outside this task. Empty HalFile shells have no file operation contract to assert.
