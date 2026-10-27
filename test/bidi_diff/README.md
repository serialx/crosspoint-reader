# MiniBidi differential test

This compares the allocation-free Rust bidi API with the unchanged C MiniBidi
and C++ BidiUtils at `c9330408`. Firmware still uses the C/C++ implementation.
It tests the compatibility algorithm, not conformance with a newer Unicode
Bidirectional Algorithm.

From the repository root:

```sh
cmake -S test/bidi_diff -B build/bidi-diff -DCMAKE_BUILD_TYPE=Release
cmake --build build/bidi-diff -j 6
ctest --test-dir build/bidi-diff --output-on-failure
```

The main host suite also registers `BidiDifferential`. The standalone target
needs the existing Cargo toolchain and a C++20 compiler; it does not fetch
GoogleTest or change firmware sources.

The test compares:

- Classes, mirrors and transparent-mark predicates for every value from zero
  through U+110000.
- Visual UTF-8 bytes, success/fallback status, paragraph direction and packed
  word order, including partial output on a declined word-order call.
- Every `shapeVisual` regression input from `test/minibidi_arabic`, plus text
  from repository EPUBs (HTML text nodes split into chunks of up to 12 words).
- Empty input, malformed UTF-8, embedded NUL, buffer exhaustion, invalid packed
  offsets, the 128-codepoint limit, deep embeddings/isolates, and Hebrew name
  substitution.
- One million generated inputs over all 23 bidi classes, Arabic joining
  characters, brackets, numbers, controls, Unicode scalars and arbitrary bytes.
  The xorshift64 seeds are `0x5a17b1d1c9330408` and `0xd1ff202610270001`, with
  500,000 inputs per seed. Failure prints the case, PRNG state and input bytes.

A successful visual call is checked for writes past its output length. A failed
or short-buffer call must leave the output buffer unchanged. Workspace and
output lengths are checked at the ABI; readable/writable, aligned and disjoint
memory remains the caller's responsibility.

To include P5-T0's words, synthetic strings and space contexts when its worktree
or merged source is available:

```sh
python3 test/bidi_diff/corpus.py /tmp/bidi-corpus.txt \
  --text-oracle /path/to/test/text_golden/run.py
build/bidi-diff/BidiDiff /tmp/bidi-corpus.txt
```

The corpus is hex-encoded bytes, one input per line. This also accepts a corpus
prepared by another tool. No private fonts or EPUB text are committed here.
For C/C++ memory checks, configure a separate build with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and the
same `CMAKE_CXX_FLAGS`; Rust uses its safe slice checks but is not ASan-instrumented.

## Tables and ownership

`generate_tables.py` extracts the pinned `bidiclasses.t`, `bidi_pairs.t` and
Arabic joining/presentation-form tables from `lib/MiniBidi/minibidi.c`. Their
original authors are Ahmad Khalifa and Thomas Wolff (MIT); the CrossPoint
extensions retain their Unicode 17.0.0 data. Regenerate only after an intentional
source-table change:

```sh
python3 test/bidi_diff/generate_tables.py
cargo fmt --all --manifest-path rust/Cargo.toml
```

`xp-core::bidi::Workspace::EMPTY` can initialize persistent caller-owned storage.
C callers use `xp_bidi_workspace_size/align/init`; initialize once and reuse
exclusively. The current workspace is 4,860 bytes with 4-byte alignment on C3
and the host. It contains two 128-character arrays, algorithm scratch and word
anchors. It should not be constructed on a firmware task stack. Each call
borrows the workspace and input/output buffers synchronously, retains nothing
and performs no allocation. Separate workspaces permit independent calls; a
shared workspace needs the caller's existing synchronization.

Visual output requires at most four bytes per decoded input codepoint, capped
at 128 codepoints (512 bytes), and may grow relative to input bytes when Arabic
letters become presentation forms. Short output returns the exact required
size without changing output. Output has no NUL terminator. Status 0 preserves
the old fallback, including empty/malformed-at-start input and over-limit text;
status -1 reports insufficient output, and -2 invalid ABI/span arguments.
Malformed input after a valid prefix emits that prefix, matching BidiUtils.
The word-order output is meaningful only on status 1.

The Rust implementation does not add a process-wide cache. Later integration
must choose a workspace owner, reuse the caller's existing synchronization and
replace the per-call C++ `std::string visual` with reusable output storage.
That integration, real-device timing and font rendering are outside this test.

`GfxRenderer::resolveVisualText` also has its own RTL-lead-byte precheck and
falls back to the original text when a successful conversion emits no bytes.
Those caller rules are unchanged here and must be retained by integration.
Before deleting the C implementation, keep a frozen copy for this oracle and
update the table generator's source location.
