# Reader document ID parity

`run.py --build <directory>` builds the shipping Rust binding with chapter-test
entry points and the SD font catalog, production `RustReaderDocument.cpp` against
in-memory HAL fakes and CommonCrypto MD5, and the unchanged KOReader reference.
The suite runs under ASan/UBSan; it does not exercise physical SD or ESP ROM MD5.
The MD5 stubs share the digest API, not the sampling or session decision.

Healthy inputs compare exact digests and file/hash/log order, with logging on
and off and ASCII, Unicode, invalid-UTF8 and long paths. Sizes cover empty files,
every selected offset minus one/at/plus one, sample-end boundaries, 2 GiB and
UINT32_MAX. Files above the target's 32-bit size range are not claimed.
A legitimate short final sample remains valid because its requested count is
bounded by the file size captured once at open.

Each active sample receives a failed seek, negative read, zero read or short
read. Rust must return no ID, skip digest finalization, close the file and avoid
further sample I/O. Faults at inactive offsets preserve the healthy result.
Every fault is followed by a healthy calculation to pin recovery without an ID
cache. Native negative reads are not executed: converting the result to size_t
is defined, but the resulting oversized MD5 access is undefined (upstream bug 7).
Native short/zero reads and failed seeks produce partial digests; the Rust
reader deliberately cancels that session event instead.

Allocation injection rejects the first Rust allocation during each calculation
and proves none is attempted. Eight original shipping-session cases retain
subscription/worthiness, open-failure, file destruction and reset-order checks.
Additional sessions attach a real shipping ReaderBook loaded from a generated
plain EPUB and a valid metadata cache. Each read/seek fault leaves the book open,
its spine readable and its chapter bytes streamable, emits no document ID, and
resets once. Fresh sessions after recovery publish the healthy ID; empty and
legitimate short final samples also publish normally. No binary fixture is
checked in.

The actual native emitSession body exercises C-string variables: a trailing
NUL beyond the 32-byte wire view is pinned by a regression assertion. Other
native leaves are poisoned unless explicitly needed by the book fixture.
The historical app-test document_id relay remains HOST-RETIRE debt; this suite
exercises the shipping replacement independently.
