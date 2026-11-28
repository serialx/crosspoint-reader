# QRCode scratch-buffer fork

Upstream: [ricmoo/QRCode](https://github.com/ricmoo/QRCode), PlatformIO release **0.0.1**. The original `src/qrcode.c` SHA-256 is `c2abf543af0043409323e88e880490c5c1250befe98f6e1147eec22315f43152`. The upstream MIT license is preserved in `LICENSE.txt` and the source headers. Upstream formatting is preserved except for trailing whitespace to keep the patch reviewable.

The fork replaces the codeword, function-grid and error-correction VLAs with caller-provided buffers, adds a version/ECC-based scratch-size query, and changes the alignment-position VLA to a bounded 7-byte local array. The encoding algorithm, lookup tables, mode choice, error correction and mask selection are unchanged. The entry points borrow three disjoint buffers for the duration of encoding; the error-correction buffer contains the result followed by its coefficients.

This is temporary maintenance debt. The encoder is a Rust port candidate for **Phase 18-render**; the frozen module goldens in `test/assets_oracle/qr/` must remain identical when it is replaced.
