# Reader image file lifetime

This harness compiles the shipping `page_render/images.rs` source with strict file
fakes. The fake file retains its own placement address and checks that every
operation and destruction uses that address. Tests cover early open failure,
successful extraction bytes, short writes, flush/close/remove ordering, cache
header validation, and skipping a failed path before file construction.

Run `cargo test --manifest-path test/page_image_pin/Cargo.toml --locked --offline`.
For pointer checks, use `MIRIFLAGS=-Zmiri-strict-provenance cargo +nightly miri test
--manifest-path test/page_image_pin/Cargo.toml --locked --offline`.
The `reader-strips` feature exercises the same source with the optional row
seeking contract enabled. Decoder and ZIP behavior have their own oracles; this
harness verifies the binding and file lifetime rather than decoding pixels.

The direct PNG/GIF cases decode tiny fixtures while tracking both pinned file
cells independently. Each fake records its own address, cursor and open state;
movement, unbalanced destruction, wrong file identity or stale cache paths fail
the test. Run both the normal and reader-strips configurations and strict-provenance
Miri. Decoder fixture bytes come from the existing image corpus.

The JPEG case compiles the shipping operation table and row callback with a
synchronous native-record fake. It keeps that record borrowed across two row
callbacks, checks strict-provenance aliasing, and injects decode failure to verify
that input close precedes cache abort and JPEGDEC release. The sequence itself is
extracted verbatim from book_parser.rs; image and dimension policies are shared.
