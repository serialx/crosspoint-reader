# Embedded inflate subset

This is a modified subset of `miniz_oxide` 0.9.1, from the crates.io release.
The original MIT, Apache-2.0 and Zlib license notices are retained here.
Only the allocation-free decompression core and its buffer helpers are included.
Compression, allocated streaming wrappers, serialization and block-boundary
features are not supported by this private crate.

Changes from the release:

- Keep the three 1,024-entry fast lookup tables. Store 576 literal-tree nodes
  and 64 distance-tree nodes instead of three 576-node trees. The code-length
  alphabet has codes of at most seven bits, so it never traverses a tree with
  the ten-bit fast lookup. The distance alphabet has at most 32 symbols.
- Copy overlapping matches in byte order after checking the source and
  destination ranges once. Three-byte copies cover contiguous source ranges;
  a byte loop handles wraparound and the remainder. This is the only new
  unsafe code in this crate.
- Count produced history, saturated at 32 KiB, and reject back-references
  beyond that history in both decoding loops. Reset the count with the stream.
- Inline the hot two-byte input reader and literal writer.
- Apply two equivalent lint fixes for the pinned Rust toolchain.

The firmware owns decoder storage and the output window. This crate neither
allocates nor takes ownership of either. `xp-core::inflate` initializes the
integer arrays and zero-valued `State::Start` in place, avoiding a decoder-sized
stack temporary. Preserve that representation when updating this code.

Verification lives in `test/inflate_stream`: original C facade and SDK traces,
PNG IDAT bytes and traces, allocation failure injection, and EPUB entry CRCs.
