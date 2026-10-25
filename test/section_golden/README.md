# Complete section byte oracle

This harness detects changes in chapter pagination and serialized page data.
It runs every XHTML spine item in `test/epubs/*.epub` through the production
`ChapterHtmlSlimParser`, `ParsedText`, `TextBlock`, `Page`, and `Section` code.
`ImageBlock` serialization is also production code. The output contains the
complete Section header, pages, page offsets, anchor map, paragraph indices,
list-item indices, and visible-text offsets. No bytes are normalized or omitted.

The baseline was captured at `194016e7` before changing the C++ word store or
token metadata. It covers 18 EPUBs, 104 spine items, and three settings: 312
sections, 2,584,934 raw bytes, and 680,100 compressed bytes.
`goldens/inventory.json` records the full baseline revision, each EPUB's SHA-256,
and each section's source path, page count, byte count, and SHA-256.

## Run

From the repository root, with the existing host test dependencies installed:

```sh
cmake -S test -B build/test
cmake --build build/test --target SectionGoldenDump -j 6
ctest --test-dir build/test -R '^SectionGolden' --output-on-failure
```

`SectionGoldens` extracts each book to its own temporary directory and compares
every complete section against `goldens/sections.tar.gz`. A mismatch reports
the section and first differing byte offset. Missing or additional sections,
changed EPUB inputs, build errors, and unreadable pages fail the test.
`SectionGoldenComparator` checks binary round trips and rejects byte mutations,
truncation, trailing bytes, and missing or extra cases.

The host must use little-endian integers, 32-bit `int` and `float`, and 8-bit
`bool`, matching the Section wire format. The initial capture and verification
used Apple Clang on macOS ARM64. Other host platforms have not been verified.

## Fixed inputs

| Setting | Viewport | Font em | Hyphenation | Focus | Paragraph spacing | Indent | Line compression | Tracking | Word spacing |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 480 × 720 | 16 px | off | off | off | 2 spaces | 1.0 | 0 px | 100% |
| 1 | 240 × 320 | 20 px | on | on | on | 2 spaces | 1.0 | 0 px | 100% |
| 2 | 640 × 400 | 16 px | on | off | off | 0 spaces | 1.25 | 1 px | 125% |

All three use embedded styles, displayed images, and default paragraph alignment.
Font ids represent integer em sizes. CSS font variants round the requested em
size to an integer and clamp it to 8–80 px. Line height equals em size before
compression; ascender height is three quarters of em size, rounded down.

Glyph advances are zero for combining marks, one quarter em for spaces, NBSP,
`i`, `l`, period and comma, one em for remaining codepoints at or above U+2E80,
and half an em otherwise. Integer division rounds down. Small caps use the
existing uppercase conversion. Kerning is −1 px for AV, AW, Ta, To, Va and Vo,
and zero for other pairs. These synthetic metrics are independent of installed
fonts and intentionally fixed; they are not device font metrics.

## Production code and host adapters

- The target compiles the vendored Expat with `XML_GE=0` and
  `XML_CONTEXT_BYTES=0`, real HTML entities, CSS parser, UTF-8 helpers,
  MiniBidi, image dimension probes, and the existing M1 Rust hyphenation facade.
  It does not use `ParserLinkStubs.cpp` from the chapter unit tests.
- Python's ZIP/XML readers supply OPF spine order, language, manifest CSS order,
  and TOC paths to a host `Epub` adapter. NCX is used when the OPF spine names
  one; otherwise EPUB navigation supplies TOC links. This is not a test of the
  firmware's ZIP, OPF or navigation parsers, CSS discovery, or duplicate-CSS
  filtering. Chapter XHTML bytes are passed unchanged to the production parser.
- `HalFile` reads and writes real host files. Each subprocess uses a private
  working directory and the relative cache name `cache`, so serialized image
  paths contain no host-specific temporary prefix. Production `FsHelpers`
  resolves image paths.
- PNG, JPEG and GIF support is advertised to the real image dimension probe.
  Pixel decoding and framebuffer writes are outside the oracle; requests for
  the decoder or a writable framebuffer terminate the tool. Fonts, cache
  eviction, time and ESP heap availability use host stubs.
- Each generated section is reopened by production `Section`, and every page,
  paragraph LUT entry and visible-offset LUT entry is read before comparison.

These files prove repeatability for the specified host pipeline. They do not
prove device pixel equality, heap or allocation counts, OOM behavior, timing,
flash size, or partial-cache resume. A firmware migration still needs those
checks and its existing focused regression suites.

## Capture a baseline

Capture only from the reviewed implementation that a later change must preserve:

```sh
python3 test/section_golden/run.py \
  --dump build/test/section_golden/SectionGoldenDump \
  --capture-baseline --goldens /tmp/section-baseline
```

Capture refuses uncommitted changes under `lib`, `src` or `rust`, and refuses to
overwrite an existing archive. Use a freshly built executable from the same
checkout. The archive is a deterministic USTAR stream compressed with gzip and
zero timestamps; its compressed size must remain below 1 MB. Review the fixture
and implementation changes before replacing a committed baseline. Never
regenerate goldens merely to make a Rust migration pass.

## Paragraph layout oracle

`LayoutGoldens` compares a second, fixed archive (`goldens/layouts.tar.gz`).
It was captured before the layout migration from the same 18 EPUBs, all 104
spine items and all three settings. Run it with:

```sh
python3 test/section_golden/run.py --dump build/test/section_golden/SectionGoldenDump --layout
```

The host build instruments a generated copy of `ParsedText.cpp`; it does not
add firmware trace code. `instrument_layout.py` requires each source anchor
exactly once and fails the build if an anchor moves or becomes ambiguous.
Each section trace contains paragraph layout calls in execution order, original
tokens and source offsets, successful discretionary splits, cached widths,
break indices, logical and reordered justification values, visual order,
x positions, output source ranges, ruby text, and link spans. Strings use hex
bytes. Repeated bounded layout calls retain their continuation flags and source
offsets; a mismatch reports the layout-call ordinal and trace line within its
book/setting/spine. The Section oracle still checks the complete serialized
result, including focus suffix positions and page placement.

Capture uses `--layout --capture-baseline`, refuses an existing archive, and
requires unchanged production sources. Never recapture to hide a mismatch.
The compact archive must stay below 600 KB. Every layout call retains a SHA-256
of its complete trace. Full records are retained for every eighth call in each
section, the first successful split in each section, all reordered/ruby calls,
and calls with more than 100 input tokens. The full baseline trace archive is
kept as external evidence; passing its directory with `--goldens` compares full
traces directly. The manifest always records hashes of the complete traces.
