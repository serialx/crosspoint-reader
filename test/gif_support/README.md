# GIF compatibility tests

The production framebuffer and BMP consumers now use Rust. These tests need no
AnimatedGIF installation. The optional `ANIMATEDGIF_SOURCE_DIR` CMake argument
builds the original raw-decoder tests against an external, already patched copy
of revision `67cfacafe12ef4926c7713342c57d2a4af040e5e`.

`goldens.json.gz` contains 5,784 outcomes from 723 generated or existing public
fixtures. The first 5,744 were captured twice from the unmodified da609bbf
consumers before implementation. The later 40 dictionary saturation, maximum-chain, clear,
early-EOI and zero-frame outcomes were captured from the preserved baseline
executables; every earlier outcome was checked unchanged. Seven BMP variants
cover fit/crop, both 2-bit thresholds, and three 1-bit thumbnail sizes. The eighth
variant records framebuffer, renderer-query, probe and pixel-cache bytes.

The mutation corpus covers every truncation and individual bit flip of a small
transparent image, code sizes 0–15/31/127/255, dimension and frame boundaries,
local/global palettes, extension labels, disposal methods, incomplete streams,
full dictionaries, interlacing, and animation. Each subprocess has a two-second
limit. All captured baseline processes exited normally; successful decode does
not imply valid input.

`matrix.py` independently replays all 8,544 GIF cases in the shared frozen image
oracle, across every orientation, render plane, strip target, crop, scaling,
dithering and cache variant. All these outputs must remain byte-identical.

`deviations.json` separately records 744 changed outcomes in 93 mutation
fixtures, approved by the coordinator as an explicit allowlist. The reader-visible change
remains a human-acceptance item in the task report. In 92 fixtures Rust rejects an
undefined dictionary code or missing compressed bits; the old decoder reported
success while using its zero-filled or stale buffers. One fixture introduces
consecutive clear codes: Rust resets twice instead of treating the second clear
as a pixel code. Early EOI, absent trailer after enough pixels, first-frame-only
animation, and consumer rejection of interlacing remain unchanged. No deviation
was written over a C++ golden.

Run the host checks:

```sh
cmake -S test -B build/test
cmake --build build/test -j 6
ctest --test-dir build/test --output-on-failure -R 'Gif'
```

`GifMutations` requires the exact documented difference set, including all new
output hashes; any additional difference fails. For strict comparison without
exceptions, run `oracle.py build/test/gif_support/gif_oracle
build/test/gif_support/gif_frame_oracle`. `GifAllocationTest` fails each Rust
allocation in turn, verifies cleanup, and checks input/cache-output failures.

`large.py OUTPUT` requires Pillow and creates four synthetic large GIFs and a
five-page EPUB outside the repository. Three GIFs decode; the interlaced fixture
checks rejection. `gif_benchmark IMAGE ITERATIONS`, built by the standalone
image oracle CMake project, times repeated calls to the production framebuffer
converter. Compare alternating old/new runs using the same compiler and host.

To replay an external pinned C++ reference against the frozen records, pass its
`gif_oracle` and `image_oracle` executables to `oracle.py` without
`--allow-documented`. The baseline executables must come from the oracle-first
commit, before the production consumer replacement.
