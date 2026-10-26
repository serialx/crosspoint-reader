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

- The target links the Rust XML core, real HTML entities, CSS parser, UTF-8
  helpers, MiniBidi, image dimension probes, and the Rust hyphenation engine.
  The imported baseline used vendored Expat with `XML_GE=0` and
  `XML_CONTEXT_BYTES=0`; all 312 sections also matched on this branch before
  the chapter XML driver migrated to Rust. The target does not use
  `ParserLinkStubs.cpp` from the chapter unit tests.
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

The dump uses Rust paragraph layout. To regenerate a baseline after an approved
behavior or fixture change, build the reviewed revision and capture to a new
directory:

```sh
cmake -S test -B build/test
cmake --build build/test --target SectionGoldenDump -j 6
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
requires unchanged production sources. Use the same freshly built Rust-backed
dump and a new `--goldens` directory as above. Never recapture to hide a mismatch.
The compact archive must stay below 600 KB. Every layout call retains a SHA-256
of its complete trace. Full records are retained for every eighth call in each
section, the first successful split in each section, all reordered/ruby calls,
and calls with more than 100 input tokens. The full baseline trace archive is
kept as external evidence; passing its directory with `--goldens` compares full
traces directly. The manifest always records hashes of the complete traces.

## Chapter operation oracle

`ChapterOperationGoldens` compares `goldens/ops.tar.gz`, captured from the
unsplit C++ chapter parser at `e621694b`. All 104 spine items in the 18 EPUBs
run under all three settings. No private book content is included.

```sh
python3 test/section_golden/run.py --dump build/test/section_golden/SectionGoldenDump --ops
```

The build generates `ChapterTrace.cpp` using `instrument_chapter.py` and links
it only into `SectionGoldenDump`. Firmware compiles the original source and
contains no trace hooks. Required source anchors must occur exactly once;
missing or duplicated anchors fail generation. The instrumentation test also
checks that changed replies and missing operations fail comparison.

The ordered stream records block starts/ends, text tokens, paragraph service
calls and replies, line breaks, rules, page breaks, box geometry, drop-cap
capture, table column measurements, list markers, ruby groups, footnotes,
image requests/dimensions/placement sizes, anchors, font queries/replies,
page callbacks, and parse-step input positions. `Paragraph.*` records preserve
the existing paragraph methods used by chapter semantics. Repeated queries
are retained. `Reply` immediately follows a non-void paragraph call.

Fields are tab-separated. Byte strings use lowercase hex, integers use decimal,
and floats use the decimal value of their exact IEEE-754 binary32 bits.
BlockStyle fields are comma-separated in declaration order (excluding its
static constant); CssLength is float bits followed by the unit enum, and a
border side is width followed by style. These records contain field values,
never C++ object padding or pointer addresses. Visible text offsets count
Unicode codepoints; `Step` positions count original input bytes.

This is an observation of the current parser, not a public ABI. It supplements
the complete Section and layout oracles; it does not replace them. Image
fallback extraction and stop/OOM paths require their focused tests, since the
successful corpus does not exercise every failure path. Capture with `--ops
--capture-baseline` only before a production change, using a new output directory.

## Cache read oracle

`SectionReadGoldens` loads all 312 original section files through production
`Section::loadSectionFile` and `Section::loadPage`. Each loaded page is serialized
again and expanded into named fields by `read_oracle.py`. The canonical JSON
includes every serialized element field, the complete fixed footnote/link arrays,
the visible-offset LUT value, and the result of `getTextFromSectionFile` for each
page. Strings and byte arrays use hex to preserve embedded NULs and arbitrary
UTF-8 bytes. The original archive remains the independent write-path proof.

```sh
python3 test/section_golden/read_oracle.py \
  --dump build/test/section_golden/SectionGoldenDump \
  --output test/section_golden/goldens/reads.json.gz --verify
python3 test/section_golden/read_oracle.py \
  --dump build/test/section_golden/SectionGoldenDump \
  --output test/section_golden/goldens/mutations.json.gz --mutations --verify
```

The mutation table records header acceptance and which original page indices
still load after each 64-byte truncation, a flipped version byte, each page LUT
entry replaced by zero/out-of-file/UINT32_MAX, each element count replaced by
UINT16_MAX, and an oversized section page count. Each case runs in its own
process with a two-second timeout and a 128 MiB data-segment resource limit;
process failures remain distinct from ordinary rejection. The resource limit
is diagnostic containment, not a model of firmware heap availability.

To capture before a codec migration, omit `--verify` and select a new output
path. Capture refuses to overwrite a file. Freeze the executable before running
the mutation capture: concurrent relinking invalidates a running subprocess
oracle. Capture and verify use the same compiler and host adapters as the write
oracle; undefined behavior in the original decoder is not a portable contract.

`partials.json.gz` adds 936 checks captured from the same frozen C++ binary:
valid partial headers with a watermark, a short watermark, and zero-page
partials. Run with `--partial --verify --output
 test/section_golden/goldens/partials.json.gz` and the same `--dump` executable.
