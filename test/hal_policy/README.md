# HAL policy oracle

`run.py` compiles the production facades against deterministic SDK/platform leaves
and compares 60 traces with `golden.json.gz`. The goldens were captured from
CrossPoint `0b70268b` before the port. They include leaf-call order, failure and
retry behavior, lock spans, diagnostics, secret/time bytes, and C++ allocation
counts, requested bytes and frees. `abi.cpp` checks state sizes, null handling,
timer wraparound and all 2,880 valid clock strings.

Run through CTest, or supply a host Rust archive:

```sh
python3 test/hal_policy/run.py --archive rust/target/aarch64-apple-darwin/release/libxp_ffi.a
```

To compare an exported original tree, use `--root /path/to/original`. `--capture`
regenerates the goldens and must use the original C++ tree, not the port. The
original export and deliberate one-line mutation results for every subsystem
are kept in the P9-N-hal evidence directory, outside this repository.

The runner copies source and headers into a temporary directory and changes
`unsigned long` to `uint32_t`, and the signed deadline cast to `int32_t`, so
macOS's 64-bit `long` does not change ESP32 timer wraparound. Power-manager
sleep and Picco USB-hold bodies are excluded: they remain platform leaves.
Panic capture hooks are compiled but not called; only the post-reset policy is
exercised. SDK internals, real threads/interrupts, electrical timing and actual
NVS writes require separate hardware qualification.

Coverage includes NVS open/read/size/write/commit errors and retry caching;
trusted-time bounds, monotonic floor, RTC lead and wear guard; RTC read/write
failure and caching; light presence/clamping/toggle; tilt threshold/orientation,
startup, cooldown and I/O failures; nested power locks and frequency failures;
gauge polling and analog smoothing; device overrides/cache/probe, wake reasons,
ADC and input edges; display wake/resync and refresh translation; short/OOM/file
reads, replacement-handle close and USB rollback; panic write failure/consumption;
and haptic disabled, failed-init, single/double-pulse behavior.
