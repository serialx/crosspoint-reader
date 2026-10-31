# XTC parser differential oracle

This suite compares the production `xtc::XtcParser` (Rust byte parser plus C++
file adapter) with the C++ parser from `9738db01f107a67f5ee392a45b26f97e90c65e06`.
The three files in `reference/` are copied from `lib/Xtc/Xtc/` at that revision;
only their namespace was renamed to `reference_xtc`. Do not update the reference
to match a candidate. The in-memory HAL supplies identical seek/read semantics
to both parsers; real SD I/O and device rendering require separate verification.

Run from the repository root:

```sh
cmake -S test/xtc_diff -B build/xtc-diff -DCMAKE_BUILD_TYPE=Release
cmake --build build/xtc-diff -j 4
ctest --test-dir build/xtc-diff --output-on-failure
# Optionally append private XTC paths; they are read directly and never copied.
build/xtc-diff/XtcDiff build/xtc-diff/fixtures /path/to/private.xtc
```

The suite also runs from the main host CMake project. `generate.py` creates small,
explicitly synthetic XTC and XTCH files with metadata, chapters, three pages,
legacy 48-byte table offsets, zero dimensions, odd dimensions, and overlapping
page entries. It commits no book binaries. Each generated file is tested intact,
truncated at every byte, with every single bit flipped, and with boundary values
for header counts and offsets. Private files receive intact parsing and page
checks only; the exhaustive mutations apply to the small synthetic corpus.

Comparisons cover open/probe errors, header snapshots (including failed opens), metadata bytes,
default dimensions, bit depth, lazy chapter values and flags, page-table records,
page-read errors and partial writes, streaming callback bytes/offsets/errors,
and the reader's BW/LSB/MSB pixel predicates. Pixel comparisons use the unchanged
XtcReaderActivity layout rules on the returned bytes; they do not substitute for
an emulator framebuffer comparison. Mutated page counts probe the first three,
last, out-of-range, and UINT32_MAX indices. Page buffers are capped at 100,000
bytes and pixel expansion at one million source pixels to bound host test work.

Compatibility includes swapped version bytes, ignored flags/color/compression/
checksum fields, fixed title/author positions (ignoring metadataOffset), distinct
buffered/streaming bad-magic errors, lazy chapter sentinel/filter/clamp behavior,
and the historical 64-bit chapter-offset interpretation across chapterOffset and
padding. Zero-sized and overlapping pages remain accepted where the original
accepts them. Existing C++ chapter-vector and string ownership is unchanged.
