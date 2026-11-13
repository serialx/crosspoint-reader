# Image pipeline byte oracle

This standalone host target links the production PNG/JPEG/GIF converters and
patched JPEG firmware decoder dependency. PNG and GIF decoding use Rust and need
no PNGdec or AnimatedGIF checkout. Its X3 renderer fake exposes the real
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
`5d6523086a696abac78a9816f13d0956ab5ff61c`, with 696 independently verified
PNG pixel corrections described in [the PNG compatibility notes](PNG.md).
Its inventory contains SHA-256 for
every corpus member. Each of 51,024 cases records the dimension query, streaming
header probes with 1/7/4096-byte chunks (including accepted byte count), decode
success, framebuffer SHA-256, renderer write-target query sequence SHA-256 and
length, cache existence, cache SHA-256 and byte count. Cache bytes include the
header, row packing and final padding. `samples.tar.gz` retains four complete
binary outputs, including full framebuffers and caches, for inspection.

The corpus contains 96 synthetic/public images: all distinct image files in
`test/epubs/*.epub`, gray PNG 1/2/4/8/16-bit, indexed PNG at 1/2/4/8-bit with
transparency, RGB/RGBA/gray-alpha, interlaced PNG, tiny/odd dimensions, baseline
and progressive JPEG at 4:4:4 and 4:2:0, grayscale and CMYK JPEG, large downscales, GIF
transparency/interlacing/animation, truncated headers and oversized dimensions.
Each image runs 11 render
variants across all 48 targets: fit/exact dimensions, down/upscale, clipping,
crops, alpha, dither, cache disabled, grayscale and performance flags. Extra
cases cover uncropped fit-mode alpha PNG and independent X/Y crops for PNG,
JPEG and GIF (GIF retains its existing crop-ignore behavior). The 864 added
cases were captured from a scratch export of the same C++ revision. Apart from
the 696 documented corrections, the original records remain unchanged; all
corpus-member bytes are unchanged. The three
committed archives total 1,331,567 bytes, below the 1.5 MB budget.

| Feature in the corpus | Observed baseline behavior |
| --- | --- |
| Gray 1/2/4/8-bit, indexed, RGB, RGBA and gray-alpha PNG | Decodes, including transparency |
| 16-bit gray and interlaced PNG | Header probe succeeds; decoder dimensions and render fail |
| Baseline, progressive, grayscale and CMYK JPEG | Decodes; CMYK covers decode parity, not color accuracy |
| Noninterlaced GIF with transparency | Decodes |
| Interlaced GIF | Dimensions succeed; rendering rejects it and logs the unsupported feature |
| Animated GIF | Renders only the first frame and logs a warning |

These are observations for the committed fixtures, not a complete format
support guarantee. Header probing can succeed for an input the codec rejects.
The oracle preserves each failure flag, framebuffer, and cache result.

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
cache. The in-memory sink does not verify filesystem deletion. The full host suite also runs `CacheTest.cpp` through the production
`ImagePipeline` adapter: completion creates a file, while final-write and close
failures remove the partial file through the host HalFile implementation. The X3 oracle does not establish
correct output on panels whose framebuffer exceeds 65,535 bytes. The existing
16-bit image offset is preserved and covered by a Rust regression test.

The [GIF compatibility suite](../gif_support/README.md) separately covers BMP
consumers, malformed streams, allocation failures, and large-fixture timing.

## Direct reader binding

`ReaderImageGoldens` calls the production Rust page-image decode binding through
an app-test-only entry. It compares success, framebuffer bytes and complete cache
bytes/existence/length against all 4,608 existing variant-2 records. The immutable
corpus hashes are checked and golden-file digests recorded; no records are recaptured. The existing
pipeline diagnostic milestones are also checked for all 96 corpus images. Renderer-query
counts are omitted because the reader lends the existing target snapshot.

`ReaderImageFailures` compares that binding with the native shell under the strict
HalFile fake: source/cache open, seek, read, write, close and Rust allocation
failures, including strip targets. PNG and JPEG continue if cache creation fails; GIF
returns failure. The test asserts that Rust allocations are released. Native
leaf bodies are extracted from ImageBlock.cpp at build time, so the test exercises
the shipping storage and log adapters. JPEG uses the production native JPEGDEC leaf with Rust row/pipeline callbacks;
its heap threshold, decoder/file-handle allocations and cache failures are compared
with the retained native shell.

`ReaderImageGoldens` also probes dimensions through the production reader binding.
`ReaderImageProbeFailures` compares 73 PNG/GIF/JPEG probe fault cases against the
retained native shells, including file operations, allocation failures and the
JPEG heap threshold. Failed probes must preserve both caller output values.

`JpegDiagnosticArguments` compiles the native JPEG shell with pattern initialization
and observes its diagnostic arguments. Valid rendering and failed dimension probes
(open, low heap and allocation) must pass zero dimensions except for the dimensions
event. The observer is inserted into a generated host copy; firmware has no test hook.
