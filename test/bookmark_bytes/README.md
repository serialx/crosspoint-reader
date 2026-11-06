# Production bookmark byte oracle

`bookmark_bytes_codec` exercises the dormant P16-O5a Rust record codec directly.
It compares 43 save cases against raw D0 file bytes and 73 load cases against
exact record bytes, float bits, defaults and optional offsets. It also denies
each record/field allocation in turn and checks partial-record cleanup; the
writer has one checked allocation after its exact-size pass. The native oracle
below still owns full path, I/O ordering and storage-failure coverage until the
Rust file adapter is connected. Codec parity alone is not full persistence parity.

Run it with `ctest --test-dir build/test -R '^bookmark_bytes_codec$'
--output-on-failure`. The standalone crate uses the production codec sources,
without linking native storage. Its generic final name storage permits the R2
keyboard result block to become the record's name directly at the later switch;
the current dormant fixture uses a byte vector. No keyboard owner is activated.

`bookmark_bytes_oracle` compares the native bookmark serializer, parser, path rules,
and storage failure outcomes with frozen synthetic fixtures from firmware source
`2543f2a4`. Future bookmark implementations must consume `golden.json`; they must
not replace these expected bytes with output from the new implementation.

Configure and build the normal host suite, then run:

```sh
cmake -S test -B build/test
cmake --build build/test
ctest --test-dir build/test -R bookmark_bytes_oracle --output-on-failure
```

ArduinoJson 7.4.2 must be installed by PlatformIO. An existing installation can be
selected with `-DBOOKMARK_TEST_ARDUINOJSON=/path/to/ArduinoJson/src`. The runner
links the host `xp-ffi` archive built by CMake, including production
`xp_store_document`. It compiles the complete `BookmarkFile.cpp`,
`BookmarkUtil.cpp`, and `PersistableStore.cpp` translation units. Unused password
functions are discarded by the host linker; no serializer or store-policy stub
is supplied.

The SDK is pinned to `6a573acd`. `run.py` extracts the exact `readFile`, `writeFile`,
open helpers, `mkdir`, `exists`, and `replaceFile` bodies from that checkout.
`manifest.json` records each original file hash, selected byte range and body
hash. `HalStorage` only forwards these calls under a lock in firmware; the host
uses the extracted SDK methods directly. Fake physical file/volume operations
inject failure, record ordering, and retain target and temporary file contents.
The fixture also models successful Arduino String operations and disables logs
and the clock. ArduinoJson's actual Arduino String reader/writer adapters remain
in use.

The fixture covers optional fields, all initialized float encodings in the
corpus, string escaping and invalid bytes, name bounds, numeric bounds, malformed
and wrongly typed input, path flattening, and storage failures. Percentage values
are initialized from explicit IEEE-754 binary32 bits. Raw file and path bytes are
hex strings in `golden.json`; loaded floats are compared by their bits. The
comparator checks every record, boolean, physical operation and remaining file,
without parsing or normalizing persisted JSON. Four negative controls alter a
byte, field order, float spelling, and optional field, and must all be rejected.

The build directory contains `cases.json`, `actual.json`, `manifest.json`, and
`raw/<case>/<index>.bin` with matching `.path` files. Those are diagnostics, not
new goldens. Baseline capture is restricted to the approved native HEAD with
unchanged production source; an existing golden permits only additional cases
and rejects any change to its frozen results.

These host checks do not model allocator failure, task concurrency, physical FAT,
power loss, or device floating-point execution. In particular, the fake volume
records the SDK's remove/rename transaction: a failed rename after removing the
old target can leave no file, and failed cleanup can leave a temporary file.
The test preserves those outcomes rather than asserting atomic replacement.
The float model uses the target's double/long-long, slot-size, pool, and string
configuration; host pointer/allocator layout is not target memory evidence.
