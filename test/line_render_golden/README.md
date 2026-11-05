# Preview and selection line oracle

This archive freezes production `da609bbf` with SDK `37d272d4`, before deleting
C++ line rendering. It contains 8,187 framebuffer SHA-256 values and 614 ordered
selection geometry records. The compressed archive is below 1.5 MB.

```sh
cmake -S test/line_render_golden -B build/line-golden
cmake --build build/line-golden --target LineRenderGolden -j 6
python3 test/line_render_golden/run.py check --binary build/line-golden/LineRenderGolden --work-dir /tmp/line-check
```

The driver uses the real renderer, built-in fonts, decompressor, paragraph layout,
page codec and Rust archive from `page_golden`. It compiles the production preview
implementation. `extract.py` copies the production dictionary selection builder,
its line tokenizer and span measurement, and clipping extraction/highlight methods
into host activity stubs. Anchor checks reject missing or duplicate methods.
The stubs supply definition line spans, settings, ample heap and an 800×480 display;
they do not model input scheduling, dictionary lookup, wrapping, touch controls or
physical panels. Plain dictionary display outside selection remains a separate
span-drawing path. No private fixtures are included.

The preview matrix crosses the one built-in family (Noto Sans), all four sizes,
all four line-spacing values, all five alignments, both hyphenation, focus and
paragraph-spacing values, all four orientations and BW/LSB/MSB planes. Additional
Latin, RTL and CJK/combining samples exercise short panes and clipping. Built-in
fonts' missing-glyph behavior is preserved; this is not CJK font coverage.

Six mixed definitions each span three selection pages, including CJK, RTL,
combining marks, long words, whitespace and the headword marker. The actual plain
selection builder preserves its current regular-style words. Four book-style
pages exercise bold/italic, decorations, SUP/SUB, focus, ruby and source ranges;
they pass through serialized page loading before clipping. Dictionary and book
selection cover all orientations and three planes, with highlighted words and
interword gaps. Geometry records retain source offsets, word text, positions,
sizes, row/page indices, font and paragraph/direction/hyphen metadata.

`run.py` hashes every complete framebuffer, compares every frame and geometry
record, and keeps the candidate observations and stderr in the work directory.
`capture` refuses changed production sources or an SDK other than the pinned
revision. Never recapture to accept changed behavior. Public/private page and
text oracles and firmware emulator workloads remain separate required gates.

`LineRenderGolden --bench` reports cold-construction allocation requests, peak,
largest request and retained bytes, followed by warm draw time (median of seven
batches of 100 draws). On macOS it also reports retired instructions per draw
from process counters; zero means unavailable. It measures preview with/without focus and a 30-line,
three-page dictionary window. The host allocator adapter counts C++ allocations
and Rust allocations during those scopes; fonts and the framebuffer are initialized
before measurement. Host `Page` objects retain test-only codec vectors, so these
are comparative host measurements, not exact target heap sizes. The fixed recorder
aborts if its 32,768-entry capacity is exceeded. Run both revisions serially on the
same host and retain the source/build identities with the numbers.
