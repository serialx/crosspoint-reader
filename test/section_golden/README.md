# Complete section byte oracle

This harness detects changes in chapter pagination and serialized page data.
It runs every XHTML spine item in `test/epubs/*.epub` through the production
`ChapterHtmlSlimParser`, `Paragraph`, `TextBlock`, `Page`, and `Section` code.
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

The host build instruments a generated copy of `Paragraph.cpp`; it does not
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

### Phase 3 assembly integration

Production uses Rust paragraph and chapter assembly with a synchronous C++ Page
sink. Mock assembly tests cover focused state/ownership cases; full pipeline
parity is checked by section, layout and operation oracles. `run.py` filters the
explicit migrated paragraph and assembly/scalar operation names and their direct
replies, preserving nested metric records and all unlisted operations. See
[the chapter boundary](../../docs/contributing/chapter-pipeline.md) for lifecycle,
ownership and multilingual coverage.

`goldens/assembly-images.txt` contains 420 image sizing results captured from
the unchanged C++ sizing branches at `b7fa3580`. It records the source hash and
tests every width/height-presence combination, five CSS units, three viewport
and container sizes, zero dimensions, extreme aspect ratios and fractional
rounding. The capture tool extracts the original branch body and uses the real
`CssLength` implementation; it does not duplicate the sizing algorithm.

Run the Rust comparison with `cargo test --manifest-path rust/Cargo.toml -p
xp-core chapter::assembly`. `capture_assembly_images.py --output <new-path>`
is only for a separately reviewed baseline capture and refuses to overwrite
an existing file. Never regenerate these expectations from Rust.

`goldens/assembly-carry.txt` adds 2,048 deterministic states evaluated by the
unchanged C++ `linesToCarry`, `keepWithNextCarry` and `canCarry` bodies. It checks
paragraph lookahead, heading carry, drop caps, recent-offset limits and trailing
non-text elements. `capture_assembly_carry.py` records the source hash and current
revision and refuses to overwrite an existing capture. The C++ assembler source
is identical to `b7fa3580` in both captures.

The mock assembler tests also exercise ownership on cancellation, every allocation
position in page/table/carry workloads, vertical style reuse, table placement,
drop caps, box slicing and zero-padded link/footnote storage. Host-only
`chapter-trace` observations preserve operation field encoding and order; the
full operation comparator exercises the production Rust path against frozen C++
expectations.

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
  --output test/section_golden/goldens/mutations.json.gz --mutations --verify \
  --exceptions test/section_golden/goldens/mutation-exceptions.json
```

The mutation table records **515,571 cases** from the frozen C++ reader at
`6dafb158`: header acceptance, original page-index acceptance, and process exit
status. Truncations cover every byte length 0–64, the existing 64-byte stride,
and the byte before, at, and after every fixed-record and string boundary.
`record_boundaries()` walks the version-68 definition in
`rust/crates/xp-core/src/section/mod.rs`: header, page records, text arenas,
ruby/image strings, optional font payloads, footnotes, links, and all five
index tables. It checks each page/table end against the stored offsets.
The sweep also retains flipped versions, invalid page LUT offsets, oversized
element counts, and oversized section page counts.

Cuts inside the 44-byte header use `--header`, which stops before page loading
and records whether the cache file remains. This isolates header behavior from
undefined old page-LUT reads. Other mutations probe every original page index.
Each case runs in a fresh child of a frozen `--serve` process, with a two-second
alarm and 128 MiB data-segment limit. This avoids repeated executable startup
while keeping decoder state isolated; crashes and timeouts remain distinct
from ordinary rejection. These limits are diagnostic containment, not a model
of firmware heap availability. The runner requires a POSIX host.

To capture before a codec migration, omit `--verify` and select a new output
path. Capture refuses to overwrite a file. Freeze the executable before running
the capture: concurrent relinking invalidates a running subprocess oracle.
Capture and verify use the same compiler and host adapters as the write oracle;
undefined behavior in the original decoder is not a portable contract.
The expanded baseline replaces the earlier 64-byte-only mutation fixture;
it contains actual C++ outcomes before applying any exceptions.

The mutation fixture uses lossless runs of repeated outcomes. Labels are derived
from the immutable section archive and sampler, with a SHA-256 of each section's
complete ordered labels. Verification rejects changed labels, invalid run
lengths, missing cases, and extra sections before applying exceptions. The
original full base/candidate JSON captures and frozen executables are retained
in the external M8 Round 2 evidence directory.

`mutation-exceptions.json` lists three classes:

- `truncated-header`: **6,864 cases**, one for each section and length in
  **[22, 44)**. The old reader accepts and retains these finalized files. At
  lengths 22–23, the settings are complete and the initialized page count is
  left zero or receives only its low byte; this is deterministic, not undefined.
  At lengths 24–43, the page count is complete but the table offsets are not.
  The verifier proves version 68, the cut interval, the original page count
  reconstructed from the available bytes, and original acceptance/retention.
  It then expects rejection, a zero page count, and cache removal. Each section
  explicitly lists its 22 cut lengths; every case is checked separately.
- `short-lut`: **96 cases** where an unchecked C++ read leaves `pagePos`
  partly uninitialized. The verifier proves that the named page's
  four-byte LUT entry is incomplete and that the C++ reader accepted that page.
  It changes only that page's expected acceptance to rejection. This extends
  the original short-LUT exception class to the denser sampling and rebuilt
  baseline. The 96 cases differ from the earlier 24: unchecked reads depend on
  stack contents and are not stable across host-driver builds.
- `checked-offset-arithmetic`: **3,108 cases** in the separate header-field
  sweep below. The original C++ wraps a table offset modulo 2^32 and reads
  bytes near the start of the file. Checked arithmetic now rejects that read.
  This is an accepted deterministic correction, not undefined old behavior.

No filename or mutation-specific behavior exists in the production decoder.
All remaining outcomes must match exactly. `SectionMutationRules` tests the
sampler and exception rejection rules.

### Header offset mutations

`goldens/header-mutations.json.gz` records **48,206 cases** from the frozen
C++ implementation at `cfc874ca`. The production sources were unchanged during
capture; only the current host driver was copied into a detached throwaway
worktree. The SDK was pinned at `37d272d4`. Frozen executables, full captures,
source/executable hashes and the standalone C++ guard patch are retained in
the external M8 Round 3 evidence directory.

```sh
python3 test/section_golden/read_oracle.py \
  --dump build/test/section_golden/SectionGoldenDump \
  --output test/section_golden/goldens/header-mutations.json.gz \
  --header-mutations --verify --jobs 12 \
  --exceptions test/section_golden/goldens/mutation-exceptions.json
```

For every original section, the sampler uses both finalized and valid partial
headers. It mutates each of the five 32-bit table offsets and the 16-bit page
count through 0, 1, original−1, original+1, 0x7fffffff, 0x80000000,
0xfffffffc through 0xffffffff, and file size−1, size, size+1. Count values are
reduced to 16 bits; duplicate values are sampled once. Partial file size includes
the eight-byte watermark. Four additional cases per section set partial count=1,
list-item offset=44 and visible offset=0xfffffffc through 0xffffffff.

The `--offsets` probe records acceptance, page count, direct visible-offset
lookup, page acceptance, loaded-page visible offset, and file retention.
It probes every **original page index**, including indices excluded by a
mutated smaller count; it does not loop over an artificially enlarged count.
Both captures exit normally in every case. Each case uses the same isolated
child process and resource limits as the truncation sweep. The fixture stores
actual base outcomes using the lossless run encoding and ordered-label hash.

All **3,108 differences** are in `checked-offset-arithmetic`: **2,496 partial
headers** reject and remove the cache; **612 finalized headers** retain their
page results but return zero for a loaded page's overflowing visible offset
and `nullopt` for the corresponding direct lookup. There are **zero unknown
differences** and no new short-header, short-LUT or undefined-behavior exceptions.
Equal results on other malformed inputs do not establish defined old behavior.

The exception file groups explicit section names with explicit mutation labels;
the verifier checks every pair. For partials, it proves that
`visibleOffset + pageCount * 4 > UINT32_MAX`, the wrapped trailer and its eight
bytes fit in the file, and the original loader accepts and retains the section
(`cfc874ca:lib/Epub/Epub/Section.cpp:247`). For finalized headers, it proves each
changed entry wraps and its four bytes fit, then derives the exact original
value from those bytes (base lines 892 and 1149). It verifies the entire base
record, including all page and lookup results, before changing only the proven
effects. Non-overflow cases, incorrect base values, page-result changes, and
duplicate exceptions fail verification.

Equivalent standalone C++ overflow checks match the candidate on all 48,206
cases. The candidate's C++ adapter also checks these bounds explicitly, before
calling the already checked Rust functions. No wire-format or valid-cache
behavior changes. The original 515,571-case truncation fixture is unchanged;
both fixtures use the same exception file, selecting only their own classes.

`partials.json.gz` retains 936 checks: valid partial headers with a watermark,
a short watermark, and zero-page partials. Run with `--partial --verify --output
 test/section_golden/goldens/partials.json.gz` and the same `--dump` executable.

`SectionCodecCoverage` compares focused adapter results with
`goldens/codec-coverage.json`, captured from `6dafb158` using the same host driver:

- A drop cap containing UTF-8 text, negative coordinates and a negative font ID
  is loaded and serialized back to its exact bytes.
- A header with character spacing −3 loads with matching settings and rejects
  mismatched settings.
- Finalized and partial synthetic sections exercise anchor/TOC, paragraph,
  list-item and visible-offset lookups in both directions. Duplicate offsets,
  missing anchors, out-of-range pages, and the partial-cache boundary are
  compared directly with C++ results. Explicit assertions also check first/last
  duplicate selection and rejection beyond the partial watermark.

Run these focused checks with:

```sh
ctest --test-dir build/test -R '^SectionCodecCoverage$|^SectionMutationRules$' --output-on-failure
```

### Expanded M7b C++ baseline

The M7b baseline at `04735f48` adds paragraph existence, creation and reset,
paragraph direction, row-storage mutations and queries, footnote insertion,
box-depth queries, semantic font/viewport queries, and table read-ahead requests.
These records keep the existing `Paragraph.` prefix for compatibility with the
expression recorder. Inputs and non-void replies are recorded; footnotes encode
both bounded strings. The archive was captured from C++ before the Rust port,
with coordinator approval to add coverage; existing section/layout baselines
were not changed. The original M7a archive remains in the external M7b evidence
as `ops-v1.tar.gz` with its manifest.

This is still a selected-operation oracle, not a complete memory-state trace.
Assembly's new `semantic*` feedback calls are represented by the original output
observations, not uniformly as independent records. `instrument_chapter.py`
contains the explicit service-to-record mapping (`paragraph_names` and
`added_services`), while its named hooks identify semantic/page observations.
The raw parser event stream, complete sections/layouts, cancellation tests and
emulator caches remain independent checks.

### Direct Rust paragraph observations

Phase 3 removes paragraph services from the chapter-to-C++ path. Operation
comparison filters only the names in `run.py`'s `RUST_PARAGRAPH_OPS` exclusion
set and their immediately following `Reply` records from both the frozen archive
and candidate. Live assembler services such as `bodySpaceWidth`, `bodyTextAdvance`,
`pageWidth`, `tableBytes`, `setTableStacked` and `addPendingFootnote` remain
compared, even though their recorded names also begin with `Paragraph.`. Every other record
and reply retains its order and exact bytes. The original archive is unchanged
and its raw sizes and hashes are checked against the manifest before filtering;
filtered sizes and hashes are then used for inventory comparison. Section and
layout comparisons remain unfiltered.

List-marker, ruby-group and ruby-append observations moved to the Rust
`chapter-trace` feature so they remain in the operation oracle. Production builds
do not enable that feature. Private operation streams use the same
`filter_paragraph_ops` helper before `compare`.

### Multilingual coverage limits

The frozen public corpus has RTL and soft-hyphen cases but no ruby or CJK body
text, and no exercised small-caps tokens. Its 312 sections and 5,529 layout calls
do not independently validate those paths. Focused paragraph/parser tests cover
ruby, CJK/Korean boundaries, small caps and size slots; private Korean emulator
and cache evidence provides additional coverage. Keep the frozen archives intact
when adding supplementary fixtures, and capture new baselines from the approved
pre-port implementation.
