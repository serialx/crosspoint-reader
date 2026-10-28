# Package parser oracle

These goldens capture the C++ package parsers at `e621694b`, before the
package state machines move to Rust. The XML tokenizer already uses Rust.
The executable uses the production `PackageParser` adapter, Rust document passes
and book-cache writer, with the existing host HAL and ZIP-size adapters.
`instrument.py` adds observations to a build-directory copy of
`BookMetadataCache.cpp`; it does not replace serialization. The expected files
remain C++ captures: the original corpus at `e621694b` and the six Unicode
boundary cases at `8f71e528` (the last C++ serializer before the Rust writer).

Run from the repository root:

```sh
cmake -S test/package_golden -B build/package-golden
cmake --build build/package-golden -j 6
python3 test/package_golden/run.py --oracle build/package-golden/PackageOracle verify
```

`record` replaces expectations and is only for deliberate baseline capture.
Each compressed JSON contains exit status and all output bytes for each of six
chunk sizes (1, 7, 64, 512, 1024 and 4096), with both TOC preferences. Firmware
uses 512 for container and 1024 for OPF/TOC; 4096 also exercises facade splitting.
Metadata-only short counts depend on the offered chunk and are recorded separately.
Private EPUBs can be supplied with repeated `--epub`, an external `--goldens`
directory, and `--skip-synthetic`; never store their output in this directory.

The corpus covers all 18 repository EPUBs plus 22 synthetic cases: duplicate
manifest ids, missing idref, relative and escaped hrefs/anchors, nested labels,
UTF-8 BOM, both UTF-16 byte orders, missing rootfile, truncation in each document,
long title/label, and both AES spellings versus case/font-only controls.

The six `synthetic-unicode-*` cases add NFD Hangul/Japanese labels and paths,
precomposed Hangul, 512/513-byte metadata text and attributes, 512/513-byte hrefs,
a combining mark cut at byte 512, percent-escaped multibyte hrefs/anchors, and
UTF-16 with a supplementary character. Each runs all 12 nav/NCX chunk schedules
(480 public schedules total). Expectations preserve the existing byte clamp,
including incomplete UTF-8, and restricted composition: Japanese dakuten is
not composed by the firmware helper. Hrefs are decoded/normalized, not NFC.
Only the new expectations were captured with a scratch export of `8f71e528`;
no original golden was regenerated from the Rust writer.

## Observable contracts

- Container matches raw case-sensitive names and chooses the last qualifying
  rootfile path. Missing rootfile leaves an empty result.
- OPF matches local names for structural/metadata elements. Manifest attributes
  use exact names. Metadata whitespace collapses; creators join with comma-space;
  the first nonempty title wins. Metadata text is clamped at 512 bytes before
  appending each nonspace byte, so a pending separator can cross that boundary.
  Attribute metadata is capped at 512; collection candidates are capped at eight.
- OPF writes each manifest id and normalized href to `.items.bin` in encounter
  order. It builds a hash/length/offset index, sorts at spine start, resolves
  idrefs by reading that file, and emits cache spine calls in spine order.
  Duplicate ids retain the existing sort/lookup behavior. Destruction removes
  `.items.bin`; the oracle copies it to `items.bin` first.
- Metadata, identifiers, series/index, cover, guide/start references, NCX/nav
  paths and CSS order are logged. Metadata-only parsing suppresses semantic
  handling after completion but finishes the current 1024-byte XML feed; it
  intentionally returns a short write, including `size - 1` at an exact boundary.
- NCX emits on content close; nav emits on anchor close. They retain raw label
  whitespace and use one-based nesting counters narrowed to an eight-bit cache
  level. They split the first literal `#` before percent decoding, then normalize
  the path. Normalization removes empty and parent components, but retains `.`.
  There is no existing fixed href or label limit; inventing one changes behavior.
- The encryption probe matches local names and case-sensitive substrings
  `aes128-cbc` or `aes256-cbc`, then latches true. Finalization detects truncation.

`calls.txt` records cache method order, parser metadata, short writes, and CSS
order; string arguments are hexadecimal bytes. `book.bin`, temporary spine/TOC
files and `items.bin` preserve the serialized bytes. The adapters do not replace
full firmware I/O or establish device timing, stack or allocation behavior.

## Rust writer boundary

`xp-core::epub::facade` owns document feed splitting, metadata-only short counts,
the manifest index and CSS paths. `xp-core::epub::cache` owns the v10 encodings,
spine/TOC lookup, NFC title emission, LUTs and cumulative sizes. `PackageParser`
owns the manifest `HalFile`; `BookMetadataCache` owns the other files and keeps
its existing reader and SIZE_WINDOW state. The sink retains the 4 KiB pass writer,
three 4 KiB final-build streams, and ZIP batch-size service (400-spine threshold,
2,048 targets per batch).

The host-only Cargo feature `package-trace` preserves cache-call observations
at the corresponding Rust emission sites. The instrumenter installs the optional
sink callback and maps spine and TOC emissions to the original
`createSpineEntry` and `createTocEntry` lines, including argument order and bytes.
C++ orchestration method observations remain at their original boundaries.
Firmware does not enable this feature. No operation is filtered out and no
golden is regenerated.

`PackageAllocationFailure` covers both the record ABI and document-pass ABI.
`PackageWriterFailure` fails each successive Rust allocation through a complete
small book build, exercises the 400-spine index path, and checks short-write and
flush-failure cleanup. `TocBoundaryAllocation` retains the long ASCII/NFD label
cases; its injected NFC failure now targets the Rust allocation for the same
32 KiB label. All owners must release their allocations after teardown.

## Package and ZIP diagnostics

`PackageDiagnosticGoldens` compares exact `(level, origin, formatted text)`
multisets and exit statuses for 2,808 schedules against C++ `116195b4`.
The frozen capture includes the existing 480 schedules, cover/guide/collection
notices, a 405-item spine, every truncation of four small package documents,
and selected high-bit mutations. It retains repeated lines. The eight exact
exceptions in `diagnostic-exceptions.json` are existing XML error classifications:
both implementations reject, but Expat reports `partial character` and Rust
reports `not well-formed (invalid token)`. The test rejects unused exceptions.

```sh
ctest --test-dir build/package-golden --output-on-failure
python3 test/package_golden/diagnostics.py --oracle build/package-golden/PackageOracle
```

`PackageDiagnosticFailures` injects parser/input-buffer allocation failures,
manifest open failures, temporary/final write failures, ZIP-size failures and
spine-index allocation failure through the production facades. It also checks
that collection refinements do not print the collection-limit warning.
`ZipDiagnosticFailures` checks small archives, local-header reads and requested
entry-name reads. The logging and ZIP-size adapters are host-only.

To rebuild the diagnostic reference, run from the repository root. Keep all
exports outside the repository; the historical harness uses the original parser
classes, which the current harness no longer supports.

```sh
PACKAGE_REF=$(mktemp -d)
git archive 116195b4 lib/Epub lib/Utf8 lib/FsHelpers lib/Memory \
  lib/Serialization lib/XmlParserUtils | tar -x -C "$PACKAGE_REF"
mkdir -p "$PACKAGE_REF/test/package_golden"
for stub_name in xml_oracle fs_helpers book_metadata_cache chapter_html_slim_parser content_opf_parser; do
  mkdir -p "$PACKAGE_REF/test/$stub_name"
  cp -R "test/$stub_name/stubs" "$PACKAGE_REF/test/$stub_name/"
done
mkdir -p "$PACKAGE_REF/test/vendor"
cp -R test/vendor/expat "$PACKAGE_REF/test/vendor/"
cp -R test/package_golden/logging "$PACKAGE_REF/test/package_golden/"
for name in PackageOracle.cpp instrument.py CMakeLists.txt; do
  git show "52ec852e:test/package_golden/$name" > "$PACKAGE_REF/test/package_golden/$name"
done
```

In the exported `test/package_golden/CMakeLists.txt`, replace everything starting
with `include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)` with the following.
This links the original Expat parser and enables exact log capture instead of
linking the current Rust parser.

```cmake
target_sources(PackageOracle PRIVATE
  ${REPO_ROOT}/test/vendor/expat/xmlparse.c
  ${REPO_ROOT}/test/vendor/expat/xmlrole.c
  ${REPO_ROOT}/test/vendor/expat/xmltok.c)
target_include_directories(PackageOracle BEFORE PRIVATE
  ${REPO_ROOT}/test/vendor/expat ${CMAKE_CURRENT_SOURCE_DIR}/logging)
target_compile_definitions(PackageOracle PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
```

Build `PackageOracle`, capture to a new external file, then replay the candidate
against it with the existing exact exception list:

```sh
cmake -S "$PACKAGE_REF/test/package_golden" -B "$PACKAGE_REF/build"
cmake --build "$PACKAGE_REF/build" --target PackageOracle -j 6
python3 test/package_golden/diagnostics.py \
  --oracle "$PACKAGE_REF/build/PackageOracle" --record \
  --golden "$PACKAGE_REF/diagnostics.json.gz"
python3 test/package_golden/diagnostics.py \
  --oracle build/package-golden/PackageOracle \
  --golden "$PACKAGE_REF/diagnostics.json.gz"
```

Never record expectations from the candidate or replace checked-in goldens
without reviewing the differences.
