# PNGdec miniz adapter checks

`PngZlibStream` exercises one-byte and partial input/output schedules, output
beyond the 32 KiB dictionary, optional Adler verification, truncated streams,
invalid headers, dictionary requests, arena capacity, and reset/end ownership.
The deterministic fixture expands to 70,000 bytes; byte `i` is
`(i * i / 71 + i * 13) % 256`.

```sh
cmake -S test/png_zlib -B build/png-zlib
cmake --build build/png-zlib -j 6
ctest --test-dir build/png-zlib --output-on-failure
```

`PatchPngdec` checks patch idempotence, active-environment isolation, source
exclusion, and rejection of an unknown or incomplete dependency without edits.
The normal host suite includes both tests.

Set `PNG_SOURCE_DIR` to a PNGdec library directory to build
`png_decoder_probe`. It reports open/decode status, row count, row-byte FNV-1a,
average decode microseconds, and the decoder allocation size:

```sh
cmake -S test/png_zlib -B build/png-zlib \
  -DPNG_SOURCE_DIR="$PWD/.pio/libdeps/default/PNGdec"
cmake --build build/png-zlib -j 6
build/png-zlib/png_decoder_probe image.png 100 0
```

The last argument is PNGdec's option mask (`0` or `1` for CRC checking).
`PNG_PROBE_ROWS=/tmp/rows.bin` writes the decoded row bytes for an independent
pixel comparison. Use one repetition when dumping rows. Run corrupt files
with an external timeout: the original PNGdec can loop on truncated input.
Host timings exclude file loading and decoder allocation; use firmware guest
time for the ESP32 performance gate.

PNGdec 1.1.6's bundled zlib corrupts decoded pixels in three existing fixtures:
`epub-037ac3791e984081.png`, `epub-9f9bd5bb1738460e.png`, and
`epub-ecb6e6a8a11961be.png`. Their IDAT streams and Adler checks are valid.
Miniz pixels equal Pillow, and the original decoder produces the same correct
pixels when its `inffast.c` unaligned-copy path is disabled. The original fast
copy can switch from window history to current output, then copy overlapping
bytes in four-byte units without checking the match distance.

The image oracle has 696 corrected records for those three images. The other
50,328 records, corpus bytes, and binary sample archive are unchanged. All
1,584 configurations for those images were also compared with Pillow-reencoded
uncompressed PNGs through the original image pipeline. The original fixture files and four binary sample outputs are unchanged.
