# EPUB lifecycle oracle

This suite builds the production Epub, package/cache, CSS and ZIP implementations
against a traced host filesystem. Goldens record the original Epub implementation
at `0b70268b`, using a historical source export for the later extensions. It records return values, metadata, spine and TOC entries,
progress, exact logs, file operation order and every remaining cache byte.

Run `cmake -S test/book_io -B build/book-io`, `cmake --build build/book-io`, then
`ctest --test-dir build/book-io --output-on-failure`. The full host suite also
includes this target. `run.py --record` is only for approved baseline captures.
The optional CMake `BOOK_IO_EPUB_SOURCE` path supports a historical source export
or a deliberately changed source for checking oracle sensitivity.

Cases include repository EPUBs, a deterministic synthetic book, every truncation
of that ZIP, and an individually failed open/read/write/seek at every step of the
synthetic lifecycle. The sequence covers cold load, metadata-only and sync reads,
warm load, skipped CSS and rebuilding a deleted CSS cache. Failure is injected
once; cleanup and subsequent opens remain observable.

Limitations: the host uses native size_t and std::hash, not the C3 ABI. Device
cache-directory names and float behavior need firmware parity checks. Protection and image conversion use scripted leaves; TXT is inert in this suite; their own
suites and emulator workloads remain necessary. The oracle records no-throw C++ allocation sizes and individually fails them;
it does not intercept ordinary string allocations or emulate mutex contention. Goldens contain only public inputs.

The host target uses `-ftrivial-auto-var-init=zero`: the baseline cache reader
uses unchecked `readPod`/`readString` calls, and a failed read otherwise leaves
stack values undefined. This makes those captures repeatable; it does not fix
the firmware or establish a contract for its undefined failure behavior. The
first observed case was the version diagnostic at BookMetadataCache.cpp:322.

The extended baseline capture covers cover/thumbnail orchestration with JPG, PNG,
GIF and unsupported formats, duplicate CSS paths/content, and scripted protected
reads (size mismatch, allocation overflow/failure, key/decrypt failure and short
output). Converter stand-ins record configuration and emit deterministic bytes;
existing real decoder goldens remain the pixel contract. The 2,493 cases include 1,296 individually failed HAL operations. Golden shards
are each smaller than 1 MB. The final allocation and directory extensions were
captured from exported `0b70268b` Epub source and header after implementation
started, never from the candidate.
