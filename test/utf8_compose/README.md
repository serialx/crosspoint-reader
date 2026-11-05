# Text helper oracle

`TextGoldens` compares the production C++ facades against output captured from
C++ at `c9114b3d`, before the text-helper Rust migration. The capture executable
source and fixtures were committed first in `66a9e567`.

Run with the normal host build:

```sh
cmake -S test -B build/test
cmake --build build/test --target TextGoldenDump -j 8
ctest --test-dir build/test -R '^TextGoldens$' --output-on-failure
```

The 27,806-byte `text-goldens.gz` contains 831 inclusive codepoint ranges and
SHA-256 digests of grouped complete outputs for 18,305 distinct byte strings.
Every codepoint from zero through U+10FFFF is checked for CJK, Hangul, combining
marks, small caps, decomposition, explicit breakable hyphens, and both CJK
boundary functions in both directions against seven representative neighbors.
Both encoder overloads and decoder consumption are also checked exhaustively;
the encoder/decoder stream has one FNV-1a digest. Invalid Unicode scalar values
in that interval are deliberately included because the encoder accepts them.
There is no `toLower` API in the current C++ Utf8 library; the existing Rust
hyphenation case mapping moves unchanged and retains its hyphenation goldens.

String inputs include all 847 original composition-table entries (including
chains), all 11,172 algorithmic Hangul syllable combinations, malformed and
truncated byte sequences, embedded NULs, all text nodes from every checked-in
EPUB spine item, and non-ASCII literals from the Korean, token-boundary and
combining-mark suites. Text nodes retain whitespace and hidden-element text.
The Python HTML reader decodes entities; this is a text-helper oracle, not an
XML parser oracle. `compose-inputs.json` preserves the original table's input
pairs independently of the new implementation.

For each string, the dump records allocating NFC output, absorbed source
positions, callback-rejection output, in-place output, every decoded codepoint
and consumed offset, both boundary predicates on adjacent codepoints, character
break offsets, safe truncation at every prefix, repeated character removal, and
two-character truncation. The script hashes groups of 32 complete input/output
records to keep fixtures small; no output bytes are normalized. Digest equality
is not a retained byte-for-byte archive. The Section oracle separately compares
all serialized bytes of 312 complete sections.

To reproduce the baseline, check out `66a9e567` in an isolated directory and run:

```sh
c++ -std=c++20 -O2 -Ilib -Ilib/Utf8 \
  test/utf8_compose/TextGoldenDump.cpp lib/Utf8/Utf8.cpp \
  lib/Epub/Epub/CjkBreaks.cpp lib/Epub/Epub/hyphenation/HyphenationCommon.cpp \
  -o /tmp/text-cpp-oracle
python3 test/utf8_compose/text_goldens.py --dump /tmp/text-cpp-oracle
```

`--capture` refuses an existing fixture. Capture only with the original C++
implementation; never replace the baseline to accept changed Rust output.

The public decoder also has exact-sized, unterminated allocation tests. Run
these and the KOReader UTF-16 conversion-buffer regression with AddressSanitizer:

```sh
cmake -S test -B build/test-asan -DCROSSPOINT_TEST_ASAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/test-asan --target Utf8ComposeTest KOReaderXPathResolverTest LibraryTextTest -j 8
ctest --test-dir build/test-asan -R 'ExactUnterminated|ResolvesUtf16' --output-on-failure
```

The option instruments C/C++ sources, including the facade and Expat; it does
not instrument the Rust archive or standalone Python-driven build commands.
