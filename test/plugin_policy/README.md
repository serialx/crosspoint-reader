# Plugin policy oracle

This test compiles the public C++ plugin interfaces with scripted storage, HTTP,
clock, random-number, radio, diagnostics and UI leaves. It compares the resulting logs, HTTP calls,
storage operations and persisted bytes with traces captured from revision
`386e3c45108357d8445c0b21410e072045970594`, before moving the policy to Rust.
The initial oracle was committed separately in `da4c1b8b`.

Build the host tests, then run:

```sh
ctest --test-dir build/test -R 'plugin_(policy|reader_events)' --output-on-failure
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

The token-lifetime case uses an 8,192-byte auth response and expands the refreshed
8,173-byte token in the success toast. A host-only `operator new` probe measures
requested C++ allocation payload from retry transport teardown through popup
entry, excluding malloc-based ArduinoJson and real TLS/display allocations.
Four token-sized blocks remain at popup entry (two scripted response copies,
the adopted token, and the expanded toast); a retained mint copy fails the test.
Use `--token-lifetime --record` against frozen baseline sources, then
`--token-lifetime --memory-baseline <baseline build>` for byte-exact trace and
peak/live allocation comparison. The ordinary 48/944-case goldens are unchanged.

`plugin_policy_token_paths` checks exact saved bytes for empty and malformed paths,
escaped and embedded-NUL keys/values, numeric keys, UTF-8, and 384/512/2048 segments.
It runs the public save interface under ASan with function instrumentation and
requires deep paths to use no more call depth than the shallow control plus four
calls. This catches recursive serialization on hosts with larger stacks; the
firmware's stack bound still comes from its linked image audit.

`plugin_policy_contract` checks the persisted event IDs/wire names, each mask,
sleep's implied-connect bit, writer bounds at 512/513 and 4096/4097 bytes, and
64-bit rollback. `xp_plugin_contract.def` is the only event/limit definition:
C++ expands it directly; Cargo's host-only `xp-ffi/build.rs` emits Rust constants
in OUT_DIR and rejects noncontiguous IDs or an overflowing event mask.

`plugin_reader_events` runs the actual Rust reader payload/resource implementation
against the native codec and file primitives. It compares all outbox bytes and
file-operation traces with native emission for two open/session/exit/sleep cycles.
Cases include UTF-8/escaping, signed time limits, full-width duration/progress,
eight subscribers, no subscribers, oversized events, failed opens, cap reset and
short-write rollback. A random-call counter checks that both paths share one
cached nonce. Native sleep events between reader cycles also check the shared
sequence. ASan covers the native pinned contexts and their destruction.

`reader_direct_relays` separately compiles the shipping dispatch branch against
stub Rust owners and checks the two resource bindings. The event implementation
itself has no app-test policy branch; its test shim only exposes the same entry
points. `reader_events.py --baseline-source <tree>` compares against an archived
native implementation instead of the current generic emitter. These are scripted
resource checks; they do not establish physical SD timing or power-loss behavior.
