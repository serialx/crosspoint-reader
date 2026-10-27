# Decoder serial diagnostics

`ImageDiagnosticGoldens` compares the multiset of `(level, origin, formatted text)`
for 5,411 PNG/GIF framebuffer, BMP and short-write cases. It uses the existing public mutation
corpora and images. Binary pixel/cache output stays on stdout; the opt-in host
logging stub captures serial diagnostics on stderr.

The frozen logs were captured from the `da609bbf` converters, PNGdec, and patched
AnimatedGIF. The ordinary test needs no old decoder dependencies:

```sh
ctest --test-dir build/image -R ImageDiagnosticGoldens --output-on-failure
```

Input paths are normalized to `<input>`. PNGdec return codes and AnimatedGIF
error/frame-state integers are removed from both sides because those decoder
states no longer exist. Log levels, origin tags, other text, argument values,
and multiplicities remain part of the comparison. Host clocks return zero.

`diagnostic-exceptions.json` contains exact expected candidate logs for 411 cases:
390 follow the already documented PNG/GIF parser outcome changes, and 21 fail
at an earlier PNG validation stage. The coordinator approved those 21 exceptions
on 2026-10-28. Their inputs have a missing IHDR tag or overflowing row size; the
Rust parser rejects them before PNGdec's zero/signed-dimension check. Both
versions still fail. Every exception names its reason and source, and the test
rejects stale exceptions or any changed candidate multiset.

To reproduce the reference capture, export these libraries from `da609bbf` into
an external directory: `lib/Epub/Epub/converters`, `lib/GifSupport`,
`lib/GifToBmpConverter`, `lib/PngToBmpConverter`, `lib/PngZlib`, and `lib/miniz`.
Set `IMAGE_DIAGNOSTIC_REFERENCE` to that export, `PNGDEC_REFERENCE_DIR` to the
preserved PNGdec export, and `ANIMATEDGIF_SOURCE_DIR` to the patched AnimatedGIF
source used before Phase 7. `diagnostics.cmake` builds the three reference
executables. Capture is an explicit operation:

```sh
python3 test/image_golden/diagnostics.py build/image \
  --capture /external/path/diagnostic-goldens.json.gz \
  --report /external/path/diagnostic-comparison.json
```

The resource probes accept `IMAGE_ALLOCATION_TRACE=1` to print ordered allocation
and free hashes, peaks, largest requests, and attempt counts at every replay.
`VERIFY_DIAGNOSTICS=1` also requires a diagnostic callback on each allocation
failure. `resource_diff.py` compares these probes against `png_resources_base`
and `gif_resources_base`, linked separately to the `e9c0cf1b` Rust host archive.
It covers valid decode, every allocation failure, short reads, truncation, BMP
scratch loans, and write failures. These are host allocator requests, not
firmware allocator block sizes.
