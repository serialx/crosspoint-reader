# Asset output oracle

This suite freezes production JPEG-to-BMP conversion, screenshot BMP bytes, and QR rectangle output at CrossPoint `ba6860d8`. It uses the patched JPEGDEC and QRCode leaves from the firmware dependency directory. The image inputs are the existing public image corpus; no private inputs are stored here.

```sh
cmake -S test/assets_oracle -B build/assets
cmake --build build/assets -j 6
ctest --test-dir build/assets --output-on-failure
```

The 2,451 cases include baseline/progressive JPEGs, cropped/fitted/native sizes, both output depths, tuned/original thresholds, truncated files, short output writes, screenshot rotations/padding, and QR version/UTF-8 limits. Hashes include the success byte and all output bytes; diagnostics are compared literally. JPEG currently ignores short output writes, and the screenshot writer rejects rows larger than 68 bytes; these are frozen behavior.

`run.py --capture` replaces goldens and is only for capturing an explicitly selected reference. A deliberate inversion of the JPEG success byte was checked before the port and failed 2,400 cases; restoring it passed every case. PNG/GIF and pixel-cache coverage remains in `test/image_golden` and `test/gif_support`.

`ImageBlockGoldens` adds 240 cache, missing-file, malformed-header, scanning, and bounds cases across four orientations and six render planes. `AssetAllocationFailures` compares the frozen JPEG outcomes at twelve failure points for each fixture/configuration pair. `ImageCacheAllocations` checks 96 cache allocation failures, verifies release, and requires zero additional allocations on repeated successful cache passes. `AssetBufferContracts` checks caller-owned QR storage and each screenshot write-failure boundary.

The host page harness counts allocations made inside ImageBlock separately because these allocations moved from C++ to Rust. The page harness still rejects new allocations outside that owner; this suite checks the owner's repeated-pass and release contracts. Define `ASSET_REFERENCE_ROOT` to a `ba6860d8` source export to build the optional reference resource probe. `ASSET_RESOURCES=1` prints requested peak/live/largest allocation sizes, including the unchanged decoder leaf.
