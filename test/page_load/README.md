# Page load failures and ownership

These tests call the production `Section::loadPage` with a real section file
and the host HAL. The section-oracle renderer and image-decoder stubs satisfy
unrelated build-path dependencies; they do not replace the page loader.

```sh
cmake -S test -B build/test
cmake --build build/test --target PageLoad -j 6
ctest --test-dir build/test -R '^PageLoad' --output-on-failure
```

`PageLoadMalformed` tries every truncation of a one-line page, two bit flips at
every byte, and 16-/32-bit boundary values at every position. Acceptance must
match the Rust byte validator. It also rejects page LUT offsets outside the
bounded page range. The independent pre-integration decoder comparison and
frozen page records are covered by `page_codec_diff` and `page_golden`.

`PageLoadOom` fails the single checked page allocation. A successful load retains
one allocation containing the header and all page data; there is no temporary
input allocation. It also checks `Page::fromBytes` failure and overwrites the
source bytes to verify ownership.

`PageAllocator` forces realloc failure for ordinary and 64-byte-aligned blocks,
checks that failure preserves bytes and ownership, and verifies successful growth,
alignment and cleanup. The chapter failure sweep additionally exercises growth
failures through the real builder.

`PageLoadPartial` opens a partial section through `loadSectionFile`, loads its
page, and checks the visible offset and borrowed word fields. This verifies the
partial-cache read path; it does not simulate interrupted writes or hardware
storage failures. Pixel, clipping, dictionary, and whole-book behavior remain
covered by their existing suites and the paired emulator workloads.

`PageFootnoteSelect` compiles the production selector and reader metadata input
scopes with a test-only `RenderLock` ownership oracle. Metadata reads, scratch
record decoding, selection access and result construction require the lock;
navigation and child creation require it to be released. Recursive acquisition
fails instead of hanging. The generator rejects new reader metadata consumers
until they are included in the harness. It instruments generated host copies;
firmware has no test hooks.

The test clears metadata immediately after unlock to check that single-note
and touch-link navigation own their href, checks both menu availability paths,
and exercises selection, Confirm, Power and Back. It is a deterministic ownership
check, not a threaded scheduler or physical-device concurrency test.
