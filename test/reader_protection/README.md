# Reader protection checks

`reader_protection_parity` builds the shipping Rust protection module with a
host-only entry point enabled by `chapter-test` (without `app-test`). It compares
the retained native `openProtectedBook` with the reader's direct Rust gate.
Both use the actual SDK ZIP/protected stream, the firmware miniz-to-Rust adapter,
and the same strict SD/NVS fakes. Native table file/query/destructor bodies are
extracted from the shipping initializer. The native encryption-only package
adapter uses the same Rust package ABI and diagnostics as PackageParser.

The test generates an EPUB with a published test key, decrypts its chapter through
both paths, and compares complete operation/log traces, errors and bytes. It
injects failure at every SD and crypto operation and at protected-book/decryptor
allocation. Cases include missing and wrong keys, tampered wrapped records,
expired/unverified loans, unsupported schemes, malformed XML, a missing ZIP end
record, corrupt encrypted content and TXT input. A separate probe checks that the
Rust temporary key's destructor overwrites all 32 bytes. No private keys or binary
fixtures are stored in this directory. OpenSSL supplies host AES; the firmware
continues to use wolfSSL.

`reader_protection_reclaim_order` derives the expected eviction set and order from
GfxRenderer's MemoryManager registrations, checks the corresponding Rust release
operations and vector feature condition, then executes the shared Rust Pressure
sequence with strict recording fakes. It also pins font-cache wiring before
reader dispatch. Adding a native sink requires updating the shared Rust path.

Run through CTest, or run `run.py --build /tmp/protection-check` and
`reclaim_order.py --build /tmp/protection-pressure`. Host generation requires
Python's standard library and the existing OpenSSL installation. Emulator qualification
uses the same generated public-key book and provisions its key through the existing
`POST /api/book-key` endpoint; it does not alter the device secret or key format.
