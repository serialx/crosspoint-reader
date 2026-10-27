# Text measurement differential oracle

Build and run independently:

```sh
cmake -S test/textm_diff -B build/textm-diff
cmake --build build/textm-diff -j 6
ctest --test-dir build/textm-diff --output-on-failure
```

`extract.py` compiles the measurement method bodies from baseline `c9330408`.
`EpdFont`, `EpdFontFamily`, `SdCardFont`, UTF-8 helpers and MiniBidi are real
repository implementations. The renderer shell substitutes only registration
and SD prewarming for hardware ownership. Both measurement paths receive the
same visual string, prepared once using the baseline bidi byte-scan policy.
This tests measurement rules, not framebuffer drawing or bidi parity.

The public fixture combines Noto Sans 12 (four variants), Ubuntu 12 (style
fallback), and a generated version-4 SD font with fractional metrics and
partial coverage. It exercises every style byte, five tracking values including
both extremes, layout/render modes, three base directions, missing font IDs,
partial/full/absent SD advance tables, fixed corpus strings and 150 deterministic
generated strings. Pass an external `.cpfont` path as the first argument to use
private fonts without copying them into this repository.

The pre-port public baseline records 15,759,360 advance cases and FNV-1a digest
`0fb1ef2d6653c9b2` over little-endian integer observations. Private fixtures have
different digests. Generated data and executables remain in the build directory.
