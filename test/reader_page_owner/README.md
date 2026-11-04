# Reader Page owner checks

`ReaderPageOwner` compares the production Rust Page owner with the real native
`Page::fromBytes` implementation. It reads the existing Section golden corpus
through `test/page_codec_diff/run.py`, adds truncated and mutated records, and
checks decoded arena bytes and metadata. Native arena allocations start with
`0xa5`; Rust initializes its allocation before constructing slices. The complete
successful arenas must match, so initialization must not change decoded bytes.

The Rust fixture uses actual `Page`, `Metadata`, and `FileSlot` code. Only physical
allocation, file storage, and logging callbacks are substituted. Unexpected
callbacks abort. Allocation failure is injected at every Page and metadata
allocation, including metadata replacement; copied metadata is inspected after
Page destruction. The full metadata case checks the exact 13,056-byte live
allocation. The file fixture covers each seek/read failure, but does not establish
native file-operation or diagnostic ordering parity. That remains a production
composition check when the reader switches owners. These are host contracts,
not target heap, stack, rendering, or child timing measurements.

`ReaderPageOwner` also compares borrowed native facades with native Pages:
word text, source ranges, fonts, small caps, discretionary hyphens, focus data,
ruby, block style, metadata, image geometry, and visible offsets. The facades
read the same Rust-owned arena without a resolved-view allocation.

`ReaderPageTransfer` exercises the real Rust `Pending` adapter and native
footnote, dictionary, and clipping constructors/destructors. It includes the
production child declarations and copies both clipping constructors unchanged
at build time. Scheduling, activity-base behavior, popup UI, and font prewarm
are named physical substitutes; virtual UI methods abort if called. The test
does not construct a session or install a legacy Book, Section, or Page fixture.
Unused physical leaves abort. Native Pages in this test come from the real
codec and serve only as comparison inputs and the dictionary borrowed-page case.

For each child, faults cover every facade, wrapper, and child allocation, plus
every Rust Page allocation in one-, two-, and three-Page transfers. Failed
factories must leave all owners unchanged; successful factories must consume
all owners without releasing an arena during construction. Destroying the
actual native child releases each Rust allocation once. The dictionary viewer's
borrowed selection constructor must leave its native Pages alive. Existing
vector and string allocations are not covered by nothrow fault injection.

Physical drawing comparisons cover the existing renderer fixture's rectangle
trace in all three Page modes. Image admission uses the real ImageBlock code
with host storage; image paths and sources retain their NUL terminators. Font
ascent uses deterministic fixture metrics. These checks do not establish target
pixels, input behavior, child timing, heap fragmentation, or runtime stack use.
The adapters and transfer descriptors are active in firmware; the retired
Page/metadata service table remains only in named app-test legacy oracles.

`ReaderPageTransfer` also composes the current native frame with strips disabled.
`ReaderPageFrameStrip` enables `XP_STRIP_RENDER` and the Rust `reader-strips`
feature in a separate host build. It uses the actual NativeRenderFrame,
NativeResources, ReaderStripBuffers and ReaderStripPainter with a real Rust Page
borrow, and drives the production Rust render planner. Every corpus Page also
runs each strip-allocation failure and a rejected display-source installation.
The test asserts highlight descriptor identity, replay during display and detach,
loan-protected buffers, framebuffer fallback, prewarm teardown, and real
image-cache release after source/window cleanup. The allocator checks that the
Rust Page cannot be freed while a source or image window remains installed.

Display upload, font prewarm, session status/counter handling and drawing metrics
are physical host substitutes. Plan recording is a deterministic host substitute;
this test does not prove pixels, actual plan commands, DMA, panel timing or font
cache behaviour. Image cache/window observers forward to the real ImageBlock
services. The separate strip replay/pixel oracles retain their own coverage.
The C++ PageMetadata comparison is frozen from the former native reader owner in
`test/reader_lifecycle/legacy_page_metadata.h`; no shipping code includes it.

`ImageScaleFormatting` compares the actual ImageScale log with 125,326 immutable
outputs from the pinned C3 and S3 `libc.a` implementations of
`snprintf("%.2f", (double)f)`. Both targets produced identical output in unmodified
QEMU using private Unix GDB sockets. The fixture includes ties and neighbouring
bit patterns, signed zero, subnormals, hundredth boundaries, finite extremes,
both signs, infinities, and quiet/signalling NaNs. macOS libc is deliberately not
the reference: it formats negative NaN differently. The raw target records,
probe sources, commands, ELFs, map-provider evidence, and runtime logs are in
this work’s external review evidence store. `scale-target.json`
pins the recorded data; the test never regenerates it.

Production ImageScale ratios use unsigned intrinsic dimensions and the unsigned
layout width/height. Paragraph inset subtraction preserves a nonnegative width.
A zero intrinsic dimension selects the constant 1.0 branch instead of division,
and the result is capped at 1.0. Negative and nonfinite scales are therefore
unreachable on this production path, but the formatter still matches their
target spellings.

Run the focused tests with:

```
cmake --build build/test --target ReaderPageOwner ReaderPageTransfer PageLoad ChapterHtmlSlimParserTest -j 2
ctest --test-dir build/test -j 2 --output-on-failure \
  -R '^(ReaderPageOwner|ReaderPageTransfer|ImageScaleFormatting|PageLoadMalformed|PageLoadOom|PageLoadPartial|PageAllocator|PageReaderMetadata|DictionaryHtmlPagesTest\..*)$'
```

To add the optional frame composition, configure a separate host build with the
normal `XP_FFI_HOST_FEATURES` plus `reader-strips`, build `ReaderPageFrameStrip`,
and run `ctest -R '^ReaderPageFrameStrip$' --output-on-failure` in that build.
Keep the default non-strip composition enabled as well.
