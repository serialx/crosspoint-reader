# Native clipping sync through the Rust store

`run.sh` composes the actual `clippingSync::run`, stateless native facade and
installed Rust App service with an in-memory physical volume. It covers both C3
and S3 preprocessor branches. The host store uses the C3 capacity; these tests do
not claim S3 target layout or device verification.

The C3 case holds a native RenderLock and the real BuildScratch loan across the
whole sync transaction. Storage and HTTP hooks require both to remain active.
The shared non-recursive host mutex asserts on same-task reacquisition; the
pre-fix witness fails there before I/O. Success, maximum-length upload/remote
apply, probe failure, upload failure, truncated/oversized responses, allocation
failure and missing loan retain the existing outcome and release assertions.

Direct facade cases require zero additional lock operations under an outer guard
and exactly one take/give for each unlocked mutation. Metadata, text, journal,
move and delete retain their storage/metadata contracts without acquiring that
guard. A second thread observes another task's ownership as false, reaches the
actual mutex acquisition, and completes only after the holder releases it; the
proof uses an acquisition rendezvous, not a timeout.

The private `outer_lock.h` implements the production RenderLock interface with a
host mutex and uses BuildScratch lend/reclaim. It allocates no firmware storage
and changes no production RenderLock, framebuffer loan or task behavior.
