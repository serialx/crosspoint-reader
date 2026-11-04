# Reader Page owner checks

`ReaderPageOwner` compares the dormant Rust Page owner with the real native
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

`ImageScaleFormatting` compares the actual ImageScale log with 125,326 immutable
outputs from the pinned C3 and S3 `libc.a` implementations of
`snprintf("%.2f", (double)f)`. Both targets produced identical output in unmodified
QEMU using private Unix GDB sockets. The fixture includes ties and neighbouring
bit patterns, signed zero, subnormals, hundredth boundaries, finite extremes,
both signs, infinities, and quiet/signalling NaNs. macOS libc is deliberately not
the reference: it formats negative NaN differently. The raw target records,
probe sources, commands, ELFs, map-provider evidence, and runtime logs are in
`p16-o3c/checkpoint-1/scale-proof` in the RIIR evidence round. `scale-target.json`
pins the recorded data; the test never regenerates it.

Production ImageScale ratios use unsigned intrinsic dimensions and the unsigned
layout width/height. Paragraph inset subtraction preserves a nonnegative width.
A zero intrinsic dimension selects the constant 1.0 branch instead of division,
and the result is capped at 1.0. Negative and nonfinite scales are therefore
unreachable on this production path, but the formatter still matches their
target spellings.

Run the focused tests with:

```
cmake --build build/test --target ReaderPageOwner PageLoad -j 2
ctest --test-dir build/test -j 2 --output-on-failure \
  -R '^(ReaderPageOwner|ImageScaleFormatting|PageLoadMalformed|PageLoadOom|PageLoadPartial|PageAllocator|PageReaderMetadata)$'
```
