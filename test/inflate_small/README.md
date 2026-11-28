# Compact inflate oracle

`InflateSmallOracle` checks every compressed group in the built-in font headers
(224 groups, 16 Noto Sans faces), independent zlib output, 105 input/output chunk
schedules, four mixed-block streams crossing the 32 KiB history wrap, and 141
production DictZip range extractions. Ubuntu headers have no compressed groups.
The generated dictionary uses public-domain text and a gzip RA table; full-flush
chunk boundaries and the final gzip CRC/length are valid. No dictionary file is
committed.

The malformed replay uses 1,266 frozen inputs: every byte truncation and bit
flip of fixed, stored, and dynamic blocks, short output limits, reserved block
type, invalid history, and over-subscribed/incomplete code lengths. Inputs are
replayed from the archive rather than recompressed with the local zlib version.
`uzlib-oracle.json.gz` records original C outcomes. `strict-results.json` pins
85 documented differences: 13 stricter rejections, 71 earlier failures with
fewer partial output bytes, and one identical full output reported Done rather
than Ok. No additional or changed difference is allowed by the test.

Configure standalone with `cmake -S test/inflate_small -B build/inflate-small`,
build that directory, then run `ctest --test-dir build/inflate-small
--output-on-failure`. The root host suite also includes it. The Rust mutation
unit test exercises both storage policies with raw/zlib and wrapping/linear
output; the C++ sanitizer run does not instrument Rust itself.

The baseline is firmware commit `da609bbf`. To regenerate original results,
extract `lib/uzlib`, `lib/InflateReader` and `src/util/DictZip.cpp` from that
revision into an external directory. Compile `Probe.cpp` with `UZLIB_ORACLE`,
the old `InflateReader.cpp`, and `tinflate.c`; compile `DictProbe.cpp` with the
old DictZip source, old reader/decoder, `stubs`, and production `lib/Memory`.
Use C++20 and optimize both baseline and candidate consistently. Unused uzlib
checksum references require dead-section elimination (`-Wl,-dead_strip` on
macOS, `-ffunction-sections -Wl,--gc-sections` on ELF).

Run `python3 test/inflate_small/oracle.py --probe OLD_PROBE --dict-probe
OLD_DICT_PROBE --record --output EXTERNAL_DIRECTORY` only when deliberately
refreshing the original-code record. Candidate checks omit `--record`. The
probe protocol is six little-endian u32 values (input size, output capacity,
streaming flag, input chunk, output chunk, repetitions), then compressed bytes;
its reply is status i32, produced u32, duration u64 nanoseconds, and output.
The DictZip probe takes a fixture pathname and accepts offset/length u32 pairs.
Its read-boundary case extracts the last 512-byte chunk into an exactly full
512-byte output buffer; both old and new production callers return success.
