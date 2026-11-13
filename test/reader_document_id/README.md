# Reader document ID parity

`run.py --build <directory>` builds the shipping Rust binding with chapter-test
entry points and the SD font catalog, the actual native HAL/MD5 primitive, and
the unchanged KOReader reference. Strict in-memory file fakes and CommonCrypto
MD5 run under ASan/UBSan.
The MD5 stubs share the digest API, not the sampling or session decision.

The suite compares exact digests and file/hash/log order for empty and bounded
virtual files through UINT32_MAX, Unicode/invalid-UTF8/long paths, every sample's
seek failure and short/zero read, open failure, and logging enabled/disabled.
Negative reads are checked only on Rust because the native unsigned conversion
is undefined (upstream bug 7): they contribute zero bytes. Allocation injection
rejects the first Rust allocation while calculating each digest and proves
none is attempted. Eight cases call the shipping session-flush code and pin
subscription/worthiness, file destruction before event emission, reset order,
and the event's borrowed digest bytes. The fixture uses the actual native
emitSession body: its C-string variables require a trailing NUL beyond the
32-byte wire view, pinned by a separate regression assertion.

All other native leaves are poisoned; unexpected calls abort. The historical
app-test document_id relay remains only for existing lifecycle fixtures; this
suite tests the shipping replacement independently.
