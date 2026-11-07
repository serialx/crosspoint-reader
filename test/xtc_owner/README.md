# XTC format resource owner checks

The standalone CMake suite compiles the production native HAL adapter and the
current Rust archive. It compares values, physical read/seek/open/close traces,
ordered diagnostic strings, page bytes, streamed chunks and reopen outcomes
against immutable parent output hashes. The active `XtcParser` facade now aliases
`RustXtcParser`; this branch still needs the CP2 qualification and review gates.

```
cmake -S test/xtc_owner -B /tmp/xtc-owner
cmake --build /tmp/xtc-owner -j 4
ctest --test-dir /tmp/xtc-owner --output-on-failure
cargo clippy --manifest-path test/xtc_owner/rust/Cargo.toml --all-targets --no-deps -- -D warnings
python3 test/xtc_owner/negative_controls.py --out /tmp/xtc-owner-miri
```

The last command runs strict-provenance Miri on the production owner with fake
physical leaves. Its passing control inspects non-lazy views during HAL,
allocation, diagnostic and stream callbacks; no mutable whole-owner reference
crosses those callbacks. Three ignored tests are run separately and must fail
with Miri UB: a moved inline metadata pointer, a chapter name after close, and
metadata after owner drop. An ignored negative test is not a skipped pass.

Only X3/C3 firmware allocation sequences are a qualification gate. Host libc++
strings are 24 B with short capacity 22; the Rust C3-profile representation is
32 B with short capacity 15 on host. Native/Rust host chapter rows are 32/40 B.
The host output oracle excludes allocation-profile measurements; the task packet
lists those differences separately. C3 paired firmware traces, including failure
paths, must establish actual request/free order. This suite does not establish
SD timing, render pixels, Library/Sleep integration or hardware behavior; those
have separate firmware smokes and the human device checklist.

The boundary composition compares the unchanged parent read engine with the
production owner using scripted C3 allocation limits. It checks chapter counts
around 76,695,845, multiplication-overflow counts, and stream sizes around 2 GiB,
including exact read/log order and the allocation kind passed before termination.
Its physical leaves model the native helper selection and exit before allocation;
they do not execute host STL failures. Separate native C3 images must prove the
actual helpers and allocation traces, with normal-ELF fatal stack bounds.
