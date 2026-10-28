# Chapter diagnostics

`ChapterDiagnostics` exercises production C++ formatting and Rust event paths.
It forces paragraph, text-arena, NFC-offset, page-element, image-payload, table,
and final-layout allocation failures. It also checks image sizing, nested tables,
soft flushes, the page-link limit, cache-reclaim success, and a null chapter callback.
All diagnostic capture and allocator tracing are host-only.

```sh
cmake -S test -B build/test
cmake --build build/test --target ChapterDiagnostics -j 6
ctest --test-dir build/test -R '^ChapterDiagnostic' --output-on-failure
```

The frozen `reference.json.gz` was captured from the C++ chapter parser at
`116195b4`, linked to that revision's Expat with `XML_GE=0` and
`XML_CONTEXT_BYTES=0`. The 241 inputs contain all public EPUB HTML entries,
every truncation and one bit mutation per byte of a short document, and five
malformed tails. `run.py` reconstructs those inputs and compares each result and
log multiset, including duplicate messages, levels, origin tags, and formatted text.

One pre-existing XML diagnostic difference is explicit: after a complete HTML
document, trailing `<broken` produces `unclosed token` in the Rust scanner and
`junk after document element` in Expat. Both chapter parsers accept the document
and ignore the tail. The coordinator approved retaining this difference instead
of changing XML parsing in a diagnostics patch. The test requires exactly this
exception and rejects missing, additional, or changed exceptions.

To record allocation schedules, set `CHAPTER_ALLOCATION_TRACE` to a new output
file and run `ChapterDiagnostics --gtest_filter=ChapterDiagnostics.AllocationSchedules`.
The trace records allocation/reallocation requests, alignment, success, stable
allocation IDs, and free order. `CHAPTER_DIAGNOSTIC_TRACE` records each fixture's
fault budget, return value, and allocation-attempt count. Compare against a binary
built before restoration; C++ allocations used by the test's log collector are
outside the Rust allocator ABI and are not firmware allocations.
