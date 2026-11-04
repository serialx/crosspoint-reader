# Section I/O oracle

This suite preserves a historical logical Section trace captured at `fc688443`
before its Rust orchestration port. Its synthetic book had no metadata/ZIP I/O.
The producer now uses the actual Rust ReaderBook and ReaderSection, through the
app-test-only typed fixture in `test/reader_section`. It exercises full builds, partial suspension,
resumption, abandonment, lookup results, diagnostics, ordered file operations,
and every resulting cache byte. Each filesystem operation reached by each
successful scenario is then failed individually, including rename and remove.

Configure and build through the main `test/` CMake project. Run
`ctest --test-dir build/test -R 'SectionIoGolden|BookPolicyGolden|SectionLutBoundaries|SectionBuildLifecycle|SectionReadBuildInterleaving|SectionEveryShortWrite' --output-on-failure`.
`SECTION_IO_SOURCE` selects the retained native cache adapter used by the other
behavior tests. The reader's build/page-count policy now lives in Rust's
`section_owner`; a mutation of its completed-page increment must fail the oracle.

The repository golden stores SHA-256 hashes of complete traces and file bytes
to keep the fixture small. Full baseline records are in the round evidence at
`p9-n-book-b/section-io-full.json.gz`. Hashes are compared independently for
each trace and each file; the file-name set and process result must also match.
The historical golden is immutable. `--capture` writes actual-owner physical and
logical JSONL records; by default these go beside the binary as
`section-io-physical.jsonl.gz`.

`actual_owner.py` makes the mapping explicit. Book construction, Book metadata
cursor operations and ZIP-envelope reads/seeks stay in the raw physical capture.
The ZIP's actual payload open/read/close map to the old `book/chapter.xhtml`
stream, using its validated payload offset. This fixed 2851-byte chapter used one
8192-byte request in the synthetic book; the real ZIP reader requests the remaining
2851 bytes. The mapping changes only the recorded logical path, offset and request,
never a physical operation or return value. All other Section operations retain
their order. Additional Book/ZIP diagnostics are retained in the raw capture.

Each old logical fault index selects the corresponding physical operation from
that mode's unfaulted trace. The prefix is unchanged before the injected failure;
the full resulting trace, status and Section bytes must match the old case. The
5771 names and order remain unchanged. Preloaded `cache/book.bin` is checked against
the fixture input before being excluded from Section outputs. Raw physical traces
and both fault indices are saved for every case; baseline maps are included and
fault-case maps are reproducible from those raw records.

The CP3 mapping packet records 5256 additional Book/ZIP operations across the four
unfaulted modes. `SectionExtraPhysicalFaults` covers these separately; the legacy
logical oracle alone does not establish their failure behavior. The cached-book fixture has no
CSS input, so ReaderBook's admission attempts CSS rediscovery from the single-entry
ZIP before using the valid Book metadata. These construction operations are
explicitly listed in that added coverage, not treated as old Section operations.

The policy oracle also captures layout signatures, link hit boundaries, image
dimension limits and diagnostics, visible tags, and all named HTML entities.

The storage double uses host stdio and a single failed operation, not SD timing
or mutex contention. Rendering uses the existing Section synthetic font stub;
real-font page goldens and emulator screenshots remain separate requirements.
Allocation failures and persistent/repeated filesystem failure require the
existing page/chapter allocation suites and additional targeted tests.

The four behavior tests above run the same host binary. `SectionLutBoundaries`
loads empty, one-entry and 400-entry LUTs. `SectionBuildLifecycle` covers
yielding builds, duplicate starts, suspended builds, and partial rebuilds on
either side of the saved page count. `SectionReadBuildInterleaving` reads pages
and anchors between build steps and checks that the resulting cache matches an
uninterrupted build. `SectionEveryShortWrite` writes one byte fewer at each
reached write point. `behavior.py --capture` and `--compare` store and replay
those full outcomes in external evidence without changing the historical golden.
Set `SECTION_ALLOCATION_TRACE` to a file path to record native allocation sizes
and free order, including deque growth; this host trace does not measure ESP heap
placement or Rust allocations.

Buffered TOC and direct anchor reads over truncated and version-mutated sections
are checked by `SectionAnchorBounds` in `test/section_golden`.

`SectionTestCodec.h` and `.cpp` retain the old record helpers solely for host
fixtures. Their four retired exports require the Rust `section-test` feature,
which the host CMake project enables. Firmware uses Rust's codec directly.

## Additional physical fault coverage

`SectionExtraPhysicalFaults` individually faults the 5,256 additional physical
operation occurrences in the accepted mapping: 416 Book-construction operations,
4,808 metadata-cursor operations, and 32 ZIP-envelope operations. Its four normal
runs must match the frozen physical trace hashes and full operation inventory in
`physical_baselines.json.gz`, captured from the mapping accepted on 2026-11-04.
The metadata-cursor extras are relative to the old synthetic in-memory book; the
separate real-parent production comparison found only the additional nine-read
TOC count pass per build.

The app-test admission bridge returns null on failed Book/Section admission and
closes an admitted Book if Section allocation fails. The physical producer reports
first-open rejection (10), second-open rejection (11), or completed execution (0).
It checks Rust and native C++ allocation counts against their pre-admission values,
plus zero owned physical buffers, zero open files, an unlocked metadata cache, and
a restored framebuffer. The unchanged physical fault mechanism must hit exactly
the accepted operation after an identical I/O prefix. No fault trace is projected
or removed. Each run retains the full trace and all persisted cache bytes,
including Book metadata, in a unique evidence directory beside the binary.

These checks verify execution and cleanup under every added fault; the captured
post-failure state is evidence for review, not a claim that the historical
synthetic-book oracle specified those extra operations. The native allocation
counter and resource observers are host-test code only.


## Native hash and TOC lookup

`SectionNativeHash` calls the extracted production `native_path_hash` leaf with
empty, short, long, UTF-8 and embedded-zero spans. It requires parity with
`std::hash<std::string>` and zero native allocations during each borrowed hash.
An actual-owner fixture has 144 TOC rows (143 nonempty anchors), including an
8,192-byte anchor and a real unequal-text hash collision for the installed C++
library. It checks active-build, disk partial, reopened and complete lookups,
including the missing colliding anchor and traversal across three 64-row batches.
Target C3/S3 hash parity and complete reader heap/timing remain separate evidence.
