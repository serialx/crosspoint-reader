# Inflate parity checks

Build and run through the shared host suite:

```sh
cmake -S test -B build/test
cmake --build build/test -j 6
ctest --test-dir build/test -R 'Inflate|MinizCompat' --output-on-failure
```

The fixtures contain only generated data and public images already present in
`test/image_golden/corpus.tar.gz`.

- `golden.bin.gz` preserves the 4,032 C facade traces from RIIR M2a commit
  `0604c44f`. It includes exact output bytes, statuses, chunk boundaries,
  truncation, early stops, wrapping and non-wrapping output. Thirty documented
  reserved-distance cases must now reject before output; the C decoder copied
  unwritten ring history in those cases.
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

CMake expands compressed fixtures into the build directory. Normal replay does
not need Python packages beyond the standard library.

For capture, build the test against the original C implementation and set
`RECORD_INFLATE_GOLDENS`, `RECORD_MINIZ_COMPAT_GOLDENS`, or
`RECORD_PNG_INFLATE_GOLDENS` to an output path. The PNG trace was captured with
the miniz sources from `c9330408`; its expected bytes were produced by zlib
1.2.12. Never replace these goldens with Rust output to resolve a mismatch.

The PNG schedule reproduces the streaming API and row-buffer sizes, not the
whole PNGdec implementation. Full image and emulator oracles remain necessary
when switching PNGdec's backend. `mz_inflate` supports `MZ_NO_FLUSH` only;
`mz_inflateInit2` supports window bits 15 and -15.
