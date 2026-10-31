# Inflate parity checks

Build and run through the shared host suite:

```sh
cmake -S test -B build/test
cmake --build build/test -j 6
ctest --test-dir build/test -R 'Inflate|MinizCompat' --output-on-failure
```

The fixtures contain only generated data and public images already present in
`test/image_golden/corpus.tar.gz`.

- `golden.bin.gz` preserves 4,032 facade schedules captured from hardened C
  at `9738db01`. Only the 30 reserved-distance rejections differ from the M2a
  `0604c44f` capture; there are no replay exceptions. The traces include exact
  bytes, statuses, chunk boundaries, truncation, early stops, wrapping and
  non-wrapping output.
- `mz-golden.bin.gz` preserves 1,176 C `mz_*` traces from M2a. It checks SDK
  refill/drain schedules, pointer advancement, counters, Adler values, the
  pinned SDK ABI, allocator hooks and failure cleanup.
- `png-idat.bin.gz` contains 49 valid IDAT streams extracted from the public
  image corpus, with expected bytes and CRCs from system zlib. The inventory
  records each input SHA-256 and PNG row pitch. `generate_png_inputs.py`
  regenerates this input corpus.
- `png-trace.bin.xz` records 1,568 original C miniz schedules on those IDAT
  streams: input chunks 1/7/512/2048, output chunks 1/7/row pitch/4096, valid
  and bad-Adler input. It includes exact status, consumption, production and
  final Adler values. Successful output bytes are compared to system zlib.
- `ZipInflateTest` extracts all entries from the checked-in EPUBs through
  production `ZipFile::readFileToStream` and verifies their directory CRCs.
  Set `INFLATE_EPUB_FIXTURES` to an external directory to replay private books
  without copying them into the repository.

CMake expands compressed fixtures into the build directory. Normal replay does
not need Python packages beyond the standard library.

For capture, build the test against the original C implementation and set
`RECORD_INFLATE_GOLDENS`, `RECORD_MINIZ_COMPAT_GOLDENS`, or
`RECORD_PNG_INFLATE_GOLDENS` to an output path. The PNG trace was captured with
the miniz sources from `c9330408`; its expected bytes were produced by zlib
1.2.12. Never replace these goldens with Rust output to resolve a mismatch.

The PNG schedule is retained historical adapter coverage: it reproduces
PNGdec's streaming API calls and row-buffer sizes, not a complete PNG decoder.
The current Rust PNG decoder calls the inflater directly; use the
[PNG consumer checks](../image_golden/PNG.md) and image/emulator oracles when
changing that path. `mz_inflate` supports `MZ_NO_FLUSH` only;
`mz_inflateInit2` supports window bits 15 and -15.

`InflateMutations` generates 47,886 deterministic schedules: all truncations and
single-bit mutations of small fixed/stored streams, stored LEN/NLEN boundaries,
40,000 random raw/zlib inputs, and invalid history through short and fast-loop
input schedules. Frozen compressed seeds make the corpus independent of the
host compressor version. `mutations-c.bin.gz` records hardened C results from
`9738db01`, before changing the backend. Each record has status, consumed and
produced counts, FNV-1a output hash, and Adler checksum. `MutationProbe.cpp`
can replay the same corpus against either backend; `test_mutations.py` defines
its record format and generation order.

The mutation test permits only earlier invalid-input rejection: Rust must
return DATA_ERROR without consuming or producing more bytes; equal output
lengths must retain the C hash. If C did not report DATA_ERROR, system zlib must also
reject the input. In the current corpus there are no successful-output changes
or Rust-only successes. The differences come from invalid Huffman tables and
reserved literal/length codes that C checks later or accepts. The normal
facade, SDK and PNG IDAT traces still require exact parity.

The Rust unit suite also drives 200,000 bounded raw/zlib and wrapping/non-wrapping
malformed schedules in debug mode, checking consumption/output bounds and
panics. These tests supplement, rather than prove, the safety of all inputs.
