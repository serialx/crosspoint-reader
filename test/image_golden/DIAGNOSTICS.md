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
  --exceptions test/image_golden/diagnostic-exceptions.json \
  --report /external/path/diagnostic-comparison.json
```

The resource probes accept `IMAGE_ALLOCATION_TRACE=1` to print ordered allocation
and free hashes, peaks, largest requests, and attempt counts at every replay.
`VERIFY_DIAGNOSTICS=1` requires an allocation-error event for every failed
required allocation; milestones cannot satisfy it. Optional cache fallback may
succeed without an error. `ImageAllocationDiagnostics` suppresses those error
events in a negative replay and requires the probe to reject the run.
`resource_diff.py` compares these probes against `png_resources_base`
and `gif_resources_base`, linked separately to the `e9c0cf1b` Rust host archive.
It covers valid decode, every allocation failure, short reads, truncation, BMP
scratch loans, and write failures. These are host allocator requests, not
firmware allocator block sizes.

`GifDiagnosticBounds` exercises completed rows and early end codes with 64 KiB,
1 MiB, and 16 MiB nonzero sub-block tails, including truncated tails, single-frame
and animated inputs. Both framebuffer and BMP calls must stay within 21 reads /
2,200 bytes for these fixtures, with identical reads and pixels when the diagnostic
callback is null. `FrameEnd` uses the existing logical read-ahead position: the
animation warning remains a legacy heuristic, not proof of another frame.
The test also verifies BMP gap-row write failure and successful animation-log order
with cropping and scaling disabled. For a paired run, pass `--base-probe` (the same
`GifReadProbe.cpp` linked to the baseline archive), `--base-bmp`, `--reference`, and
`--report` to `gif_diagnostic_bounds.py`. The reference flag uses the optional
frozen C++ targets described above and compares output bytes and ordered logs.

The coordinator approved four additional read-ahead boundary fixtures on 2026-10-28:
1,023 / 1,024 / 1,275 / 1,276 bytes of unused nonzero payload after a completed
frame. Rust fills a 1,024-byte decoded-input window; AnimatedGIF read ahead roughly
1,275 bytes. The candidate emits the animation warning while the legacy consumer
does not. The paired test requires identical pixels, cache/BMP bytes, and status,
and permits exactly that one line in both framebuffer and BMP logs. These cases
are separate from the 411 parser exceptions in the original frozen corpus. This
keeps diagnostics free of additional input reads; the warning remains heuristic.

## Image pipeline diagnostics

`ImagePipelineDiagnostics` replays 96 public images across 14 render configurations
against `pipeline-diagnostic-goldens.json.gz`. These logs were captured from the
`116195b4` framebuffer converters and pixel cache writer. It checks the PNG and
JPEG summaries, progressive JPEG notice, cache milestones, and pipeline errors
as exact `(level, origin, formatted text)` multisets. Input and cache paths are
normalized; the current `.px2` cache suffix is accepted.

`ImageCacheDiagnostics` triggers cache open, header, row, close, allocation, and
band-size failures, row-buffer OOM, and degenerate GIF/JPEG output dimensions.
`GifPipelineDiagnostics` distinguishes gap-row post-processing failure from final
cache-write failure. Both tests call production Rust and C++ diagnostic mappings.

The Phase 7 `ImageDiagnosticGoldens` and optional GIF reference replay exclude
these pipeline lines, which their older frozen snapshot predates. Their decoder
exceptions are unchanged; the separate upstream pipeline oracle owns the restored
lines. Capture the pipeline reference explicitly, using an executable built from
the upstream converters exported outside the repository:

```sh
python3 test/image_golden/pipeline_diagnostics.py /external/upstream/image_oracle \
  --corpus test/image_golden/corpus.tar.gz \
  --capture /external/pipeline-diagnostic-goldens.json.gz \
  --report /external/pipeline-diagnostic-capture.json
```
