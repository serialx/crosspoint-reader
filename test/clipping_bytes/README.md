# Production clipping byte oracle

`clipping_bytes_oracle` compares the native clipping store (formats, index, rewrite
transactions, deletion journal, file moves) and the `My Clippings.txt` export with
frozen synthetic fixtures from firmware source `deafbe10`. The owner port must
consume `golden.json.gz`; it must not regenerate expected bytes from the new
implementation. The archive is written with `gzip.compress(..., mtime=0)` and is
compared decompressed, so re-capturing it is byte-reproducible.

Configure and build the normal host suite, then run:

```sh
cmake -S test -B build/test
cmake --build build/test
ctest --test-dir build/test -R clipping_bytes_oracle --output-on-failure
```

ArduinoJson 7.4.2 must be installed by PlatformIO. An existing installation can be
selected with `-DCLIPPING_TEST_ARDUINOJSON=/path/to/ArduinoJson/src`. The runner
links the host `xp-ffi` archive built by CMake (`Serialization::tryReadString` and
`Utf8.cpp` are production code), compiles the complete `ClippingStore.cpp` and
`ClippingsManager.cpp`, and substitutes only physical storage, logging and the
clock. No store serializer, index rule or transaction path is stubbed.

The native store source is frozen from `8389157f` as named `.txt` inputs in
`native_oracle/`; `prepare_native.py` verifies their hashes before materializing
them in the host build directory. The production Rust store consumes the same
141-case corpus and unchanged golden.

The related `reader_clipping_parent` trace maps the native fixture's `clip`
event to each actual Rust `clipping_row` lookup. Metadata seeding reads fixture
entries silently; an `app-test` observer emits the historical event after the
Rust record copy. The first adaptation accidentally observed 5,282 seeding
accesses instead of the 1,686 logical lookups. Its failed comparison is retained
in the O5b evidence; assertions and goldens remain unchanged. The corrected
debug trace must match all 4,151,887 bytes, and the observer must be absent from
the default firmware ELF.

The SDK is pinned to `6a573acd`. `run.py` extracts the exact `openFileForRead`,
`open`, `mkdir`, `exists`, `remove` and `rename` bodies from that checkout and
records each file hash, byte range and body hash in `manifest.json`. Fake physical
file and volume operations inject failure, record ordering and retain the bytes
left behind; the volume models FAT's need for an existing parent directory, dot
entries in directory listings, and non-replacing renames.

The v1 and v3 stores are handcrafted in `run.py` from the field order of
`ClippingStore::readFromFile`, because production can only write v4; the cases
assert that production accepts them and rewrites v4 on the next change. No writer
is stubbed or reimplemented.

The fixture covers v1, v3 and v4 reads (including migration on the next write),
v4 truncation and field mutation, header and text bounds, a 4,096-byte UTF-8
selection, non-ASCII and invalid bytes, 16-record block growth and the per-book
limit, `.bak`/`.tmp`/`.migrate.bak` sidecars, the `.deleted` journal with tombstone
skip and tail recovery, replacement text, file and folder moves with rollback,
and the Amazon/Kindle-style text export with its 2,000-byte truncation. Every
physical operation can be failed or shortened. Raw file and path bytes are hex
strings; paths inside a recorded I/O list are indices into that case's `paths`
table.

Each record is dumped with its fields, its raw 65-byte id, its raw 48-byte
chapter title, and `pendingByte`, the byte the loaded `bool` actually holds.
Production stores `pendingUpload` as one raw byte and accepts any value into the
boolean; a byte other than 0 or 1 survives a load and rewrite unchanged, which is
why the oracle records both the boolean view and the stored byte.

ArduinoJson is only the case I/O format, never the store's serializer, so its
host configuration (notably a 4-byte string length, because one frozen store's hex
image exceeds 65,535 characters) carries no target-fidelity claim. The build
directory contains `cases.json`, `actual.json`, `manifest.json`, `stores.json`,
`canonical.bin`, `synced.bin` and `raw/<case>/<index>.bin` with matching `.path`
files. Those are diagnostics, not goldens. Baseline capture is restricted to the
approved native HEAD with unchanged production source; an existing golden permits
only additional cases and rejects any change to its frozen results. Four negative
controls alter a byte, a field order, a journal tombstone and a stored text length,
and must all be rejected.

Host-only substitutions and limits worth knowing before reading the golden:

- `std::hash<std::string>` builds the store path, so the frozen paths are host
  libc++ hashes; they do not prove the C3/S3 libstdc++ hash. Persisted header
  bytes and every transaction outcome are independent of it.
- Directory listings are returned in sorted order with `.`/`..` first. Real FAT
  directory order is not modelled, so the order of a folder move's renames is the
  fixture's order.
- `esp_fill_random` is a fixed xorshift32 stream, so `prepareSync` ids repeat.
- `time()` and `HalClock::localTime` come from the case, not a real clock.
- Real host allocation: allocation failure, block growth failure and host pointer
  layout are not modelled. `reserveClippings` growth is exercised by record count
  only.
- These host checks do not model physical FAT behaviour, power loss, task
  concurrency, controller caches, or C3/S3 target execution. In particular the
  fake volume records the remove-then-rename backup window: a failed rename after
  the old target was removed can leave no store, and failed cleanup can leave a
  temporary file. The test preserves those outcomes rather than asserting atomic
  replacement.

## Rust owner comparison

`clipping_bytes_rust` runs the same operation driver against the Rust store in
`xp-ffi/src/app/clippings`. `RustStore.cpp` substitutes only the physical file
operations, clock, randomness and native path hash. Its one-record copy and text
buffer are test scaffolding, not the production facade or a heap measurement.
This target retains the native `ClippingsManager` export. The separate
`clipping_bytes_rust_export` target adds `--rust-export` and uses O5a's dormant
Rust writer through physical file and calendar callbacks. It runs all 141 cases
against the same frozen inputs and golden; no production export is activated.

The candidate reads `inputs.json.gz`, captured from the verified native run at
`8389157f`, rather than building migration inputs with its own writer. This
synthetic archive contains the original cases, checks, paths and v4 input stores;
its SHA-256 is
`3430b1f6607e5e718c40ebf013e89f1b3346fead49b9c2eee2af5f896d5e64f2`.
Expected results remain the original `golden.json.gz`. `--rust-owner` refuses
`--capture-baseline`. No private book data is included.

Run the candidate with the existing `run.py` command plus `--rust-owner`, using
an `app-test` xp-ffi archive. All three CTests retain all 141 cases and the four
store negative controls, plus two export controls that corrupt output text and
the formatted date. `clipping_owner_rust` separately tests owner/slab allocation
failure, partial-load cleanup, offset allocation before filesystem changes,
cross-block deletion failure and drop-once ownership. Run that crate under
strict-provenance Miri for the Rust allocation/lifetime checks. These checks do
not establish production synchronization, target stack/heap floors, physical
FAT or power-loss behavior.

The historical child-parent oracle retains its logical transaction lock events.
An app-test-only observer maps the actual prepare-lock, unlocked persistence and
publish-lock to that trace without selecting the operation result. Its delete
scope includes the original failure notice, and two synthetic rows ensure hit
index 1 reaches physical write-failure injection. The full traces, lock-depth
annotations, borrowed text assertions and goldens remain unchanged. The separate
`reader_clipping_store_interleaving` test installs no observer: it checks the
actual mutex/I/O order, old metadata during paused writes, rejected concurrent
storage access, atomic publication and clean teardown. Three compiled Rust
mutants prove that prepare/publish guards and unlocked persistence are required.

## Snapshot maintenance

These four clipping snapshots remain independent references after the owner port.
Keep their bytes and goldens unchanged; delete a snapshot only when its last
consumer has equivalent independent coverage.

| Snapshot | Bytes | Consumers and deletion condition |
| --- | ---: | --- |
| `native_oracle/src/ClippingStore.cpp.txt` | 27,237 | `prepare_native.py`, `run.py`, and the native target in `test/clipping_store/CMakeLists.txt`; retain while the independent native byte/failure reference is used |
| `native_oracle/src/ClippingStore.h.txt` | 3,854 | The same materialization and native-store targets; retire with their last native-store reference, preserving frozen expected bytes and controls |
| `../reader_children/native_oracle/src/activities/reader/EpubReaderClippingListActivity.cpp.txt` | 6,094 | `prepare_clipping_drawing.py` supplies native drawing; retain until independent equivalent coverage replaces it |
| `../reader_children/native_oracle/src/activities/reader/EpubReaderClippingListActivity.h.txt` | 1,465 | Drawing ROW_WINDOW plus historical declarations in `test_child_handle_composition.py`, imported by `test_owned_app.py`; both drawing and historical consumers must retire before deletion |

Total: 38,650 B. O5a owns historical factory-adapter retirement after equivalent
direct result, borrowing, ordered-log and storage assertions exist. Its text
export migration alone does not retire the native store-byte reference. See the
[reader snapshot ledger](../reader_children/native_oracle/README.md) for the
other retained reader inputs and their provenance.

### P16-O5a export freeze

`native_oracle/manifest.json` also records the two Kindle export snapshots from
`dff2b732`: `ClippingsManager.cpp.txt` (2,980 B) and
`ClippingsManager.h.txt` (334 B). Together with the existing store pair, this
folder retains four snapshots and 34,405 B. The export entries use `snapshots`;
the existing store materializer continues to consume only `files`.

The export oracle still compiles the live manager during this freeze checkpoint.
Before its retirement, `run.py` and `Oracle.cpp` will consume these hash-checked
sources through a generated translation unit, without adding a frozen header
include path. The 141-case frozen corpus and its failure expectations remain
unchanged. Retire the pair only after equivalent independent export byte and
failure coverage replaces these consumers.
