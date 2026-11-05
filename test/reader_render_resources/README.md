# Reader render-resource ownership

These checks exercise the dormant Rust render-resource storage. Production still
uses the native frame. The tests import the real Rust owner, `Buffer` allocator
adapter, application resource ABI and shared plan/source descriptor declarations.
No synthetic reader session or native Page replaces the resource owner.

Physical allocation callbacks track every requested byte and inject each
reservation failure. Allocations start with nonzero bytes so the tests detect a
loss of the native arrays' value initialization. Physical prewarm callbacks model
a scope that retains its construction address; begin failure, completed scans
and unfinished scans have separate cleanup checks. Repeated scans and frames
must leave no reservations.

With `reader-strips`, the checks cover all four reservation failures, reuse,
adding an image window after text-only preparation, source rejection, plan/window
loans and borrowed scratch. A detach callback re-enters the owner after owned
planes have been freed, while strip storage is still protected. The assertions
keep the exact buffer, prewarm and image-cache cleanup order.

The host prewarm token and display callbacks are named physical substitutes.
These tests do not exercise the real font cache, plan encoding, strip replay,
panel pixels, DMA, target stack or runtime heap placement. The plan's command
pointer uses an opaque pointee in this host crate; its descriptor declaration is
the production source. Target layout probes compile the complete production
crate with the real command type. An unused `Platform` EPUB-leaf pointer is also
opaque here; it is never read or invoked.

The standalone subtree registers `ReaderRenderResources` and
`ReaderRenderResourcesStrips` with CTest, and the full host suite includes both.
Run the Rust checks directly with:

```sh
cargo test --manifest-path test/reader_render_resources/Cargo.toml --locked --offline
cargo test --manifest-path test/reader_render_resources/Cargo.toml --locked --offline --features reader-strips
MIRIFLAGS=-Zmiri-strict-provenance cargo +nightly miri test \
  --manifest-path test/reader_render_resources/Cargo.toml --locked --offline --features reader-strips
```
