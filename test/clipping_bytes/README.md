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
