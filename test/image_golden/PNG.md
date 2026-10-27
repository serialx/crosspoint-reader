# PNG compatibility checks

The normal image oracle replays all 51,024 framebuffer/cache records without
PNGdec. `png_run.py` additionally checks 4,306 consumer outcomes over 3,806 PNG
inputs, including six cover/thumbnail modes, truncations, byte mutations,
chunk-length boundaries, unsupported sample combinations, and wide rows.
The baseline is `da609bbf`; the tall-aspect fixture was captured later from the
preserved baseline executable. No expected output was captured from Rust.

```sh
cmake -S test/image_golden -B build/image-golden \
  -DDECODER_SOURCE_DIR="$PWD/.pio/libdeps/default"
cmake --build build/image-golden -j 6
ctest --test-dir build/image-golden --output-on-failure -j 4
```

`png-rejections.json` records 256 approved differences on malformed inputs.
Every entry retains its old outcome, exact replacement outcome and reason.
The comparator checks the old outcome against the immutable archive and rejects
stale exceptions. These changes were approved by the Phase 7 coordinator:

- 181 truncated IDAT inputs terminate instead of hanging.
- 60 BMP cases reject invalid bit-depth/color combinations before partial output.
- Nine oversized chunk lengths or palette payloads reject before offset arithmetic or copying.
- Four unsupported color types reject instead of producing invented gray pixels.
- Two images with fewer decoded rows than their IHDR height reject.

Valid outputs remain identical. PNGdec's permissive unknown-chunk behavior,
unchecked non-IHDR CRCs, framebuffer filter-byte fallback, and low-byte tRNS
handling remain observable. Framebuffer decode still rejects 16-bit samples and
Adam7; cover conversion still accepts 16-bit gray/RGB/gray-alpha/RGBA and rejects
Adam7. Framebuffer rows retain the former 16,416-byte combined-row limit and the
8,208-pixel expanded-gray limit. BMP has its original 2,048 × 3,072 source bounds.
Output dimensions must fit signed BMP fields and the output length must fit its
32-bit file-size field. C++ owns file handles and renderer/cache callbacks.

`PngSleepHeader` compares Rust against the exact old sleep-header predicate over
8,518 cases. The frozen predicate remains host-only in `PngHeaderTest.cpp`.
Viewer and sleep-overlay pixel output use the production framebuffer converter
covered by the four orientations, three planes, strip targets, crops and alpha
variants in the image oracle. These tests do not exercise activity navigation.

`PngResources` fails every Rust allocation, verifies complete cleanup, checks
BMP output failures, and repeats with an exclusive caller-owned scratch loan.
`png_resources FILE.png` prints allocation count, peak live requested bytes and
largest request separately for framebuffer, BMP heap and BMP scratch paths.
Counts cover Rust allocations; HalFile and renderer ownership remain with C++.
`png_benchmark FILE.png REPEATS` times the production framebuffer converter,
including its allocation and file I/O, without process-launch overhead.

For optional live differential checks, set `PNGDEC_REFERENCE_DIR` to an external
export containing the pinned patched `PNGdec/`, `lib/PngZlib/`, `lib/miniz/` and
`lib/Epub/Epub/converters/` from `da609bbf`. This builds
`image_oracle_reference`; pass that binary to `run.py`. The regular target never
links those sources. Original unpatched PNGdec sources are also supported when
its private zlib `.c` files are present. Use subprocess timeouts for malformed
reference inputs: its IDAT loop does not terminate at EOF.

## Earlier PNG pixel corrections

The image archive includes 696 independently checked corrections from Phase 5.
PNGdec 1.1.6's bundled zlib corrupts decoded pixels in
`epub-037ac3791e984081.png`, `epub-9f9bd5bb1738460e.png` and
`epub-ecb6e6a8a11961be.png`; their streams and Adler checks are valid.
Miniz pixels matched Pillow and the original decoder with its `inffast.c`
unaligned-copy path disabled. The original fast copy can switch from window
history to current output and copy overlapping bytes in four-byte units without
checking the match distance. The other 50,328 records and all corpus bytes were
unchanged by that correction; this decoder replacement preserves all 51,024.
