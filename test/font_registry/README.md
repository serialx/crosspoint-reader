# Registry boundary checks

`FontRegistryAllocation` scans eight bitmap families followed by one mixed
family with a bitmap file and 64 vector files. The fixed storage stub allocates
nothing. The executable records every C++ allocation and free during discovery,
including discarded filenames, candidate vectors, family-vector growth and final
shrinking. The check denies each allocation in turn and tests budgets one byte
below, at, and above the observed peak. Throwing-container failures exit the
subprocess with status 77 instead of unwinding through Rust; checked filename
failures return null and discovery continues.

The regression check requires discarded filenames to be released before family
storage grows. For an exact differential run, export
`lib/EpdFont/SdCardFontRegistry.{cpp,h}` from `1d95ed1d` outside the repository,
configure this directory separately with
`-DFONT_REGISTRY_SOURCE_DIR=/export/lib/EpdFont`, and pass that executable as
`check.py CANDIDATE --oracle ORIGINAL`. This compares exit status and the complete
allocation, diagnostic and selected-file transcript at every failure position
and heap boundary. Short family paths fit both standard libraries' small-string
storage, isolating candidate/container lifetime from path-string allocation.
Requested payload bytes are host measurements, not ESP32 heap estimates.

`FontRegistryAbi` uses filename owners with stateful deleters, reordered fields,
and padding. It exercises indexed size lookup, enumeration and face-name ties
through the C ABI without exposing owner representation. Existing manager and
registry tests exercise the production adapter. `SdFontRegistryBoundaries` tests
127, 128 and 129 families across both roots, sorted truncation, rediscovery,
point-size endpoints and nearest-size ties.

```sh
cmake -S test -B build/test
cmake --build build/test --target FontRegistryAllocation FontRegistryAbi SdFontRegistryTest
ctest --test-dir build/test --output-on-failure -R 'FontRegistry|SdFontRegistry'
```
