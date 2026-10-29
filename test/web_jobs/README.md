# Transfer job oracle

The frozen traces were captured from CrossPoint `72224c26` before replacing its
application transfer code. They compare the public C++ interfaces, response
bytes, diagnostic messages, callback order, file effects, progress updates and
firmware scratch allocations. The test executables use scripted platform leaves;
they never contact a server, open a device, or write a flash partition.

CMake registers four tests: `web_jobs_oracle`, `web_dav_oracle`,
`web_lcp_oracle`, and `web_lcp_disabled_oracle`. Build the shared host archive
before running them:

```sh
cmake -S test -B build/test
cmake --build build/test -j 6
ctest --test-dir build/test -R '^web_' --output-on-failure
```

The main oracle has 836 cases: firmware validation and simulated writes,
board tags split at different chunk boundaries, HTTP fetch/post/download
responses, failures and cancellation, Range resume/restart, and protected paths.
The WebDAV oracle has 144 cases covering methods, paths, response bodies,
headers, directory listings and upload transitions. The LCP oracle has 18 cases
with a synthetic enrollment key and 18 without one, including persisted
identities, response truncation, nested IDs and permissive hexadecimal parsing.
The SDK `ResumableFetch.h` remains the actual resume engine in the host build;
only its `SecureHttpClient` is replaced by the scripted transport.

The SHA leaf uses a deterministic test digest. The oracle verifies the byte
sequence fed to hashing and comparison/rejection policy, not SHA256 itself.
Likewise LCP key generation, base64, wrapping and obfuscation are deterministic
leaves. These tests do not qualify cryptography, FAT behavior, TLS timing, HTTP
wire parsing, WiFi power transitions or real OTA writes. The simulated slow
response records elapsed transport time; it does not sleep on the host.
Firmware allocation traces cover the existing 4,096-byte scratch buffer and its
failure/release paths, not every STL/platform allocation.

For a differential check, export the original `src/network` outside the source
tree and pass `--source` to `run.py`; include `--archive` for a Rust candidate.
`--record` deliberately replaces a golden and is only for the original source.
The recorded mutation check changes the original checksum seed from `0xEF` to
`0xEE`; the comparator must reject its trace. Keep platform-stub changes honest
by rerunning both the original and candidate against the same stubs.

# Ownership at the ABI

`xp-ffi/src/web_jobs` owns job decisions and state transitions. The descriptors
in `xp_ffi.h` lend synchronous storage, networking, JSON, crypto and partition
operations. Callbacks cannot retain byte/path pointers or reenter a job, except
for the documented HTTP body and LCP JSON callbacks during their active request.
Those two paths use raw state pointers without holding a mutable Rust reference
across the callback.

C++ keeps the public interfaces, existing strings/file handles, board tag bytes,
log formatting, and platform objects. The SDK resume engine, OPDS transport
adapter and WiFi power guard remain SDK/platform leaves. Firmware updates use
the existing 4,096-byte fallible scratch allocation. WebDAV retains its existing
4,096-byte copy buffer on the stack. Rust adds no heap owner or worker task.
The scanner borrows the embedded board tag so its magic is not duplicated in
the linked image.

Compatibility includes existing behavior that deserves separate review: a
malformed POST URL leaves its staged file, WebDAV replacement removes an old
file before rename, a nonpositive COPY read ends the copy without setting its
write-failure flag, and untagged firmware images remain accepted. This port
preserves these behaviors rather than changing the protocol or update policy.
