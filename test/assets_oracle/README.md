# Asset output oracle

This suite freezes production JPEG-to-BMP conversion, screenshot BMP bytes, and QR rectangle output at CrossPoint `ba6860d8`. It uses the patched JPEGDEC dependency and the local QRCode scratch-buffer fork. The image inputs are the existing public image corpus; no private inputs are stored here.

```sh
cmake -S test/assets_oracle -B build/assets
cmake --build build/assets -j 6
ctest --test-dir build/assets --output-on-failure
```

The 2,451 cases include baseline/progressive JPEGs, cropped/fitted/native sizes, both output depths, tuned/original thresholds, truncated files, short output writes, screenshot rotations/padding, and QR version/UTF-8 limits. Hashes include the success byte and all output bytes; diagnostics are compared literally. JPEG currently ignores short output writes, and the screenshot writer rejects rows larger than 68 bytes; these are frozen behavior.

`run.py --capture` replaces goldens and is only for capturing an explicitly selected reference. A deliberate inversion of the JPEG success byte was checked before the port and failed 2,400 cases; restoring it passed every case. PNG/GIF and pixel-cache coverage remains in `test/image_golden` and `test/gif_support`.

`ImageBlockGoldens` adds 240 cache, missing-file, malformed-header, scanning, and bounds cases across four orientations and six render planes. `AssetAllocationFailures` compares the frozen JPEG outcomes at twelve failure points for each fixture/configuration pair. `ImageCacheAllocations` checks 96 cache allocation failures, verifies release, and requires zero additional allocations on repeated successful cache passes. `AssetBufferContracts` checks caller-owned QR storage and each screenshot write-failure boundary.

The host page harness counts allocations made inside ImageBlock separately because these allocations moved from C++ to Rust. The page harness still rejects new allocations outside that owner; this suite checks the owner's repeated-pass and release contracts. Define `ASSET_REFERENCE_ROOT` to a `ba6860d8` source export to build the optional reference resource probe. `ASSET_RESOURCES=1` prints requested peak/live/largest allocation sizes, including the unchanged decoder leaf.

`ImageAssetFailurePaths` uses production ImageBlock, ImageDecoderFactory, and the
JPEG/PNG decoders. It renders three cache images with different path lengths and
row widths across four orientations and six planes, repeats the planes without
new allocations, releases/reopens the page, and injects failure of the full
window and the one-row fallback. Cache creation rejects read-only opens and
short writes after seven bytes; cache reads fail after their header. Successful
fallback must match the complete framebuffer, failed decoding must draw the
existing two-rectangle placeholder, and the next render must retry successfully.
The host HAL's optional fault hooks are unset in all other suites.

The allocation probe rejects mismatched asset frees and caps other than DEFAULT
(3). `AssetBufferContracts` additionally allocates a 32 KiB JPEG MCU buffer,
fails each of its three native-size buffer requests, verifies zero live owners,
and immediately retries. Native asset tests exercise the default-platform path
used by X4 Pro; C3 retains its accepted internal path and is checked by the
firmware/emulator acceptance workload.

`QrModuleGoldens` checks 137 complete module buffers against ricmoo/QRCode 0.0.1 outputs frozen before the scratch-buffer change (source SHA-256 `c2abf543af0043409323e88e880490c5c1250befe98f6e1147eec22315f43152`). Cases cover all 40 byte-mode capacities, the production version boundaries, empty/numeric/alphanumeric input, embedded NUL and UTF-8 truncation at 2,953 bytes. The old encoder ran with its original VLAs on the host; its outputs, not the fork, define `qr/goldens.json.gz`.

The optional `ASSET_REFERENCE_ROOT` allocation target uses the original encoder from that historical checkout. Set `ASSET_REFERENCE_QR_SOURCE_DIR` to a separate QRCode 0.0.1 source directory if the checkout has no dependency cache. Production tests use the local fork and do not require the registry dependency.

The image fixtures call the shipping Rust painter and cache query. Placeholder pixels are checked against an independent per-pixel border/interior reference before converting them to the historical rectangle trace; all 240 frozen pixel records stay unchanged. The one-byte JPEG case now reaches JPEGDEC instead of the old fixture's null decoder factory, so its exact error log is checked against that real path. Repeated-pass allocation and strict file-failure checks use the same reader binding.
