# Native Rust port gate

`policy.txt` is human-owned. `ports.lock` is the sorted set of **root Rust-symbol**
pairs from the default-X3 native object files. It contains references, not proof
that a path executes. The baseline is firmware source `5ff7aa05`, after R1.
R-RELAY owns the 13 small EPUB forwarding roots (14 pairs, including the two
exports used by `radio_status`).

Run `python3 scripts/check_rust_ports.py` after a default build. The PlatformIO
prelink hook uses `custom_rust_ports = enforce`; `report` prints the same errors
without stopping the link for an explicitly requested audit. Both new and stale lock pairs fail. Physical pairs are
bound to an exact root and export, and require lock entries too. A new caller
cannot reuse a physical exception. Deferred roots (R5/R6/R7) obey the same lock.

## Updating the baseline

Build the matching source and generate a candidate without overwriting the lock:

```sh
python3 scripts/check_rust_ports.py --write-lock /tmp/ports.next --json /tmp/ports.paths.json
```

This command still reports differences and exits nonzero. Inspect the candidate
diff and the reported native section paths, then copy it over `ports.lock` and
run the check again. Use a fresh native build when source files have been removed:
a directory scan can otherwise include stale `.o` files. The linked Rust archive
must come from the same build. Do not use host or another board's objects.

A legitimate update reason is one of:

- The named task removed a native relay: remove its pairs and the now-unused owner
  line; identify the source change and verify that no replacement pair appeared.
- A port or export was renamed or split with unchanged behavior: identify each
  old/new pair and show its path before and after. Do not hide growth in a rename.
- A deliberate new native callback or physical operation is required: give the
  requirement, exact caller/root and exports, lifetime/streaming contract, and
  reason it cannot call the Rust owner directly. This is an explicit scope change.
- A task was deliberately deferred or reassigned: explain the plan decision;
  retain every pair. Changing ownership does not permit growth.
- Compiler/SDK changes changed references: provide both object inventories and
  explain each changed path. A clean source diff alone is insufficient.

The reviewer checks the actual source/signature and native path for **every**
addition, verifies ownership against the plan, and checks callback lifetimes and
physical scope. A new physical line must name the particular root and exports,
not an endpoint wildcard. Check that removed pairs really disappeared and that
new tables, callback typedefs, and non-pointer fields have not bypassed decoding.
Never refresh the lock just to make the build green. The file is larger than the study's 40–60-line target: exact per-root ownership
plus tables, callbacks and physical contracts cannot fit that target without
hiding the owners.

## Boundaries of the check

The mode is an explicit SCons dependency, so switching from report to enforce
cannot reuse a target validated only in report mode.

All native `.o` files under the build directory are inspected, including locally
compiled SDK and Arduino objects. Prebuilt archives and ROM are not walked.
Rust is only its archive symbol table; no Rust code or linked instructions are
analysed. Weak `compiler_builtins` fallbacks are excluded only when target `nm` also finds
a strong definition in the default C3 libgcc/libm/libc archives. Those archives
are read for symbol precedence, never traversed. `build_rust.py` orders the native
runtime before Rust. New weak fallbacks without a native definition and all weak
application exports remain visible. This is not an `xp_` naming filter. A
toolchain, multilib or link-order change requires reviewing this rule.

Every allocated native section is followed, including static data pointing to
other data or functions. Section-level reachability over-reports infeasible
branches, address-taking and several objects in one section. Duplicate native
weak definitions are all followed. A Rust definition is also reported when a
native definition has the same name; this does not choose the final linker
winner. Physical annotations do not stop traversal.
The lock compares root/export pairs, not individual native call sites. Rerouting
a path without changing its endpoints does not change the lock.
Runtime-created pointers (virtual objects built elsewhere, stored `std::function`,
startup-populated tables) remain invisible. The callback list identifies typed
callback parameters; it does not prove their targets or lifetime. Nested callback
structs need manual entries. The `XP_SD_FONT_CATALOG` default build has nine EPUB
callback-taking slots: `font_sizes` is compiled out and R1 removed
`child_prewarm_rows`.

Direct imports include two data tables as well as functions: this deliberately
checks their full closure in addition to individually named Wi-Fi/mode slots.
A newly imported static table is therefore walked even without a policy table
line; a table passed to Rust only at runtime still needs an explicit decoder.
A table's size, header field count and one R_RISCV_32 relocation per four-byte
slot must agree. Headers are preprocessed for the default X3 configuration.
Native bitcode/LTO-only objects are outside this decoder's input contract; changing
native compilation flags requires reviewing it. Only default X3 is supported. x4pro is Xtensa/ESP32-S3 and is **not determined**;
the hook does not run there and its relocation dialect is not accepted here.

“Common path clean: yes” means zero inherited/unowned reference pairs in these
roots, not a proof that all executions avoid Rust → C++ → Rust. The R8b linked
image audit and manual runtime callback review remain necessary.
