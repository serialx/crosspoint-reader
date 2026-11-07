# Production bookmark byte oracle

`bookmark_bytes_codec` exercises the production Rust record codec directly.
It compares 43 save cases against raw D0 file bytes and 73 load cases against
exact record bytes, float bits, defaults and optional offsets. It also denies
each record/field allocation in turn and checks partial-record cleanup; the
writer has one checked allocation after its exact-size pass. `bookmark_bytes_rust`
also runs all 134 D0 cases through Rust path and persistence policy, using the
same extracted whole SDK transactions as the native oracle. Every path, byte,
operation, return value and remaining file must match the unchanged golden.
The reader binds the whole-file storage leaves through `RustEpubResources.cpp`.

Run it with `ctest --test-dir build/test -R '^bookmark_bytes_codec$'
--output-on-failure`. The standalone crate uses the production codec sources,
without linking native storage. The standalone fixture uses a byte vector for names; production adopts the moved
keyboard result block directly as the record name. Actual App/keyboard composition
coverage is documented in `test/reader_children/README.md`.

The standalone tests cover checked record growth, move-only rename rollback,
delete failure, read-buffer loans, export-handle teardown and ordered diagnostic
calls. Run the current test set with `cargo test --offline --manifest-path
test/bookmark_bytes/rust/Cargo.toml --features app-test --lib`; use the same
arguments with `cargo +nightly miri test` and
`MIRIFLAGS=-Zmiri-strict-provenance` for the Rust lifetime checks.

The read adapter borrows one SDK String directly into the decoder's final
record storage. The write adapter consumes prepared bytes after record loans
end. Native HAL must hold the existing StorageLock across each whole SDK
transaction. The host adapter's String copy and one-record dump view are fixture
scaffolding; they are neither production representations nor heap measurements.
The byte oracle disables logging; the standalone tests check Rust diagnostic
strings, severity and ordering separately. Physical SDK raw diagnostics and
lock scope are checked separately by the production adapter compositions.

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
`xp_store_document`. It compiles the hashed historical `BookmarkFile.cpp`
implementation under explicitly named oracle declarations, plus the live
`BookmarkUtil.cpp` and `PersistableStore.cpp` translation units. Unused password
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

The production reader progress adapter has two additional CTest checks:
`bookmark_progress_oracles` uses the unchanged KOReader XPath corpus (26,565
results) and the retained native PM/KOX format strings. The diagnostic matrix
compares 10,620 host messages byte for byte. Twenty negative-NaN cases use the
pinned C3 newlib contract: target printf preserves the minus sign, whereas
macOS printf drops it. Both raw transcripts and the exact exceptional inputs
are retained; no golden is rewritten. XPath offset/progress and diagnostic
severity mutations must be rejected.

`bookmark_progress_owner` loads a synthetic EPUB with the actual ReaderBook,
streams its chapter through the production progress adapter, and maps saved
paths through the session resource guard. Nine repeated visits exercise offset,
paragraph and two-pass percentage resolution with no section cache. The
observer reads the owned XPath after the guard, book and session are destroyed;
each visit checks balanced locks and zero retained Rust/physical allocations.
This complements the parser, callback-loan and allocation-denial Miri checks.
It does not replace the shipping bookmark child/Page/TOC or emulator checks.
