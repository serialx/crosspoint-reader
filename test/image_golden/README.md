# Image pipeline byte oracle

This standalone host target links the production PNG/JPEG/GIF converters and
patched firmware decoder dependencies. Its X3 renderer fake exposes the real
write-target contract: 792 × 528 physical pixels, four orientations, BW/LSB/MSB,
absolute/differential planes, and full-frame/41-row strip targets. The framebuffer
starts at `0xa5` so skipped writes remain observable. File access uses the shared
host HAL. No private books or fonts are included.

Configure with an existing, patched PlatformIO dependency directory:

```sh
cmake -S test/image_golden -B build/image-golden \
  -DDECODER_SOURCE_DIR="$PWD/.pio/libdeps/default"
cmake --build build/image-golden -j 6
python3 test/image_golden/run.py build/image-golden/image_oracle
```

`goldens.json.gz` records the C++ baseline at
`5d6523086a696abac78a9816f13d0956ab5ff61c`. Its inventory contains SHA-256 for
every corpus member. Each of 50,160 cases records the dimension query, streaming
header probes with 1/7/4096-byte chunks (including accepted byte count), decode
success, framebuffer SHA-256, renderer write-target query sequence SHA-256 and
length, cache existence, cache SHA-256 and byte count. Cache bytes include the
header, row packing and final padding. `samples.tar.gz` retains four complete
binary outputs, including full framebuffers and caches, for inspection.

The corpus contains 95 synthetic/public images: all distinct image files in
`test/epubs/*.epub`, gray PNG 1/2/4/8/16-bit, indexed PNG at 1/2/4/8-bit with
transparency, RGB/RGBA/gray-alpha, interlaced PNG, tiny/odd dimensions, baseline
and progressive JPEG at 4:4:4 and 4:2:0, grayscale JPEG, large downscales, GIF
transparency/interlacing/animation, truncated headers and oversized dimensions.
Unsupported inputs retain their failure results. Each image runs 11 render
variants across all 48 targets: fit/exact dimensions, down/upscale, clipping,
crops, alpha, dither, cache disabled, grayscale and performance flags.

The binary result uses little-endian u32 fields: 16 status words, length-prefixed
framebuffer, length-prefixed renderer query bytes, cache-exists u32, and
length-prefixed cache. Query codes 0–8 identify target, origin, rows, render mode,
absolute-plane flag, stride, physical width, physical height, and orientation.
These are observations of the existing renderer boundary, not decoder internals.

`corpus.py` regenerates fixtures using Pillow; normal replay needs only Python's
standard library. `run.py --capture` is reserved for capturing the unmodified C++
baseline. Never recapture from Rust to hide a mismatch. `IMAGE_ASAN=ON` enables
address/undefined-behavior sanitizers for the host target.

`ctest --test-dir build/image-golden --output-on-failure` also runs allocation
failure injection and cache open/write/close failure checks. The tests require
all Rust owners to be released and an abort callback for each opened failed
cache. The in-memory sink does not verify filesystem deletion. The old
`PixelCache.h` remains only because `test/gif_support/PixelCacheTest.cpp` includes
it; production converters use the Rust writer. The X3 oracle does not establish
correct output on panels whose framebuffer exceeds 65,535 bytes. The existing
16-bit image offset is preserved and covered by a Rust regression test.
