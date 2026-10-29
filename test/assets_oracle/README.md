# Asset output oracle

This suite freezes production JPEG-to-BMP conversion, screenshot BMP bytes, and QR rectangle output at CrossPoint `ba6860d8`. It uses the patched JPEGDEC and QRCode leaves from the firmware dependency directory. The image inputs are the existing public image corpus; no private inputs are stored here.

```sh
cmake -S test/assets_oracle -B build/assets
cmake --build build/assets -j 6
ctest --test-dir build/assets --output-on-failure
```

The 2,451 cases include baseline/progressive JPEGs, cropped/fitted/native sizes, both output depths, tuned/original thresholds, truncated files, short output writes, screenshot rotations/padding, and QR version/UTF-8 limits. Hashes include the success byte and all output bytes; diagnostics are compared literally. JPEG currently ignores short output writes, and the screenshot writer rejects rows larger than 68 bytes; these are frozen behavior.

`run.py --capture` replaces goldens and is only for capturing an explicitly selected reference. A deliberate inversion of the JPEG success byte was checked before the port and failed 2,400 cases; restoring it passed every case. PNG/GIF and pixel-cache coverage remains in `test/image_golden` and `test/gif_support`.
