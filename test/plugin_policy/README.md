# Plugin policy oracle

This test compiles the public C++ plugin interfaces with scripted storage, HTTP,
clock, random-number and UI leaves. It compares the resulting logs, HTTP calls,
storage operations and persisted bytes with traces captured from revision
`386e3c45108357d8445c0b21410e072045970594`, before moving the policy to Rust.
The initial oracle was committed separately in `da4c1b8b`.

Build the host tests, then run:

```sh
ctest --test-dir build/test -R plugin_policy --output-on-failure
```

`plugin_policy_oracle` runs 48 scenarios. They cover manifest defaults and edits,
path and version rules, subscription limits, event IDs and substitutions,
outbox limits and replay, token refresh, and download replacement. Storage and
HTTP failures include short writes, failed rewrites, failed backup/restore and
401/403 responses. The deliberately changed outbox cap (`>` to `>=`) is detected
by the exact-cap scenario.

`plugin_policy_json_faults` runs the 48 normal scenarios plus 112 allocation
failure positions in each of eight representative scenarios: 944 runs in total.
It uses the real ArduinoJson allocator interface. Allocation and growing
reallocation calls can fail; shrinking reallocations remain successful, as
required by ArduinoJson's string builder. Each run writes allocator attempt
counts, peak requested JSON bytes and remaining live JSON bytes to `memory.txt`.
These measurements cover ArduinoJson allocations, not all C++ or firmware heap.

The compressed goldens contain readable line-oriented traces. `run.py --source`
can compile a separate original source tree against the same leaves; `--record`
updates the goldens and must only be used with the original implementation.
The storage stub records operation ordering and injects failures at the HAL
boundary. It does not simulate physical SD power loss or replace device testing.
