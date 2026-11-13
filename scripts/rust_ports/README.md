# Native Rust port gate

The initial default-X3 baseline was firmware source `5aee6afa`, after R1 and R3a.
Later units retire entries through reviewed lock diffs.
`ports.lock` holds sorted **root target** reference pairs; `dynamic.lock` holds
sorted **table field-name** entries for the six declared port tables and
Rust-filled tables, and **table slot-index** entries for other tables. Both are
generated and both reject new and stale entries. The latter includes all native data symbols with function
relocations whose targets reach Rust, not just reader-reachable tables. It also
includes every field of the explicitly declared Rust-filled tables.

Run `python3 scripts/check_rust_ports.py` after a matching build. PlatformIO runs
an always-built, noncached alias after the ELF's object/archive dependencies;
an order-only dependency schedules it before linking or restoring the ELF.
The `buildprog` alias also requires it on no-link builds. Mode is an explicit
input: `enforce` fails on errors, while `report` prints errors and continues.
A direct toolchain invocation, disabled post script, or explicitly selected
report mode can bypass enforcement. Other board environments have no hook.

## Policy contracts

Every line requires a reason after `#`; patterns are full regular-expression
matches against exact mangled symbols, not demangled guesses.

| Kind | Fields after its key | Meaning |
|---|---|---|
| table | symbol-pattern struct header | Decode each pointer slot from the named native table. |
| patch | function-symbol | Key is Table.field; one static missing relocation is filled by this reviewed runtime store. |
| import | none | Key is the exact direct import; even a clean import needs a reviewed signature/ownership contract. |
| callback | none | Key is the root; explain parameter direction, retention and lifetime. |
| dispatch | caller-pattern target-pattern [method-pattern] | Bind a native caller to a stored function, or only matching method slots of a named vtable. |
| rustfill | caller-pattern header | Key is Struct.field; bind native callers to this Rust-filled function slot. |
| physical | exact Rust targets | Bind permission to specific root/target pairs, which must also exist in ports.lock. |
| inherited/deferred | owner | Assign nonphysical pairs from that root to a plan task. |

A dispatch caller can be `root:Table.field`, resolving the compiled function in
that slot without relying on its lambda ordinal. A vtable target requires a
method pattern: matching the whole vtable would falsely make theme snapshots
and protection size/encryption queries reach unrelated methods. Every caller,
target and method must exist. Patch targets are exact native function symbols;
compiler renumbering requires a reviewed rename update. Rust-filled structs must
have a caller contract for every field. Stable `Struct.field` pseudo-targets
represent Rust-private functions; Rust code is not parsed to infer the writer.

File-local data/vtable symbols include their relative object path (without the
incidental PlatformIO library hash); global/COMDAT symbols retain shared identity.
Declared port tables use their preprocessed header field names; Rust-filled
tables use their declared field names too. Retiring a middle field therefore
removes only that field, without renaming surviving entries. Field renames still
require an explicit old/new mapping at review.

Tables without a declared header mapping, including vtables, retain the word
index relative to their own symbol's start (including any ABI prefix words).
The key is the owning symbol, its object path when local, and that index, never
an address or distance to a neighboring symbol. It is stable under relocation
and internal function renames, not virtual-method insertion/removal/reordering;
review those layout changes against the old/new method relocations. The lock
records hot/cold slot status, not every export reachable from that
slot; the root/target lock catches endpoint growth along modelled root paths.
A different route to the same root/target pair, or a new caller of an existing
hot slot without a dispatch edge, can escape. Review the pointer store and its
invocation together.

Direct imports are ratcheted through the import policy even when no static Rust
reference exists. Header prototypes add callback requirements for function
pointers, callback typedefs and structs containing callback fields. Signatures
missing from these headers and opaque void pointers are reviewed manually at an
import change. The inventory rule catches a new callback helper regardless of
whether its prototype is available. It does not prove behavior changes hidden
behind an existing opaque signature. Callback output tables may be annotated
explicitly too; receiving output storage is not itself a Rust callback.

## Updating either lock

Use matching, freshly built default-X3 objects and the linked Rust archive:

```sh
python3 scripts/check_rust_ports.py --write-lock /tmp/ports.next \
  --write-dynamic-lock /tmp/dynamic.next --json /tmp/ports.paths.json
```

Writing candidates does not suppress errors. Review both diffs and every new
native witness path, then copy candidates to the checked-in locks and rerun.
When files are removed, clean stale `.o` files first. The directory inventory
includes unlinked objects and cannot distinguish an obsolete object from an
intended input. Stale import, callback, owner and physical declarations also
fail; retire their policy lines with the removed paths.

Legitimate update reasons and reviewer checks:

- A plan task removed a relay: identify the source change, remove its pairs and
  obsolete bindings/owners, and check for replacement paths. Ownership changes
  alone do not authorize growth. R-RELAY, R2b and R-RUNTIME are plan tasks;
  NONREADER, R5/R6/R7, RUNTIME-SHOT, RUNTIME-SLEEP and SET-OWN are explicit
  deferred buckets. The latter three name the accepted rare orchestration
  exceptions; changing an owner does not permit a new pair.
- A port, function or field was renamed/reordered: map every old/new identity
  and compare behavior. Moving data or inserting an unrelated symbol must not
  change either lock. Do not normalize distinct symbols together.
- A new callback or physical operation is deliberately required: state the
  requirement, exact root/targets, writer and caller, retained context lifetime
  and why Rust cannot invoke its owner directly. Add the import/callback or
  dispatch/rustfill contract and review every new pair and hot slot.
- A compiler/SDK change alters references: preserve both object inventories,
  explain each changed path/slot and check weak runtime binding assumptions.

A new physical line cannot globally permit an endpoint. A new root/target pair
always needs a lock change; a new Rust caller of an existing root is not visible.
Never refresh a lock only to make the build green. Coarse deferred root ownership
is a retirement assignment, not evidence that every path executes off-reader.

## Remaining proof obligations

The graph sees references, including address-taking, vptr writes, infeasible
branches and duplicate weak/COMDAT bodies. For example, protection_close's
conservative path enters decryptToSink through a destructor's vtable reference;
the destructor does not invoke that method. Physical annotations never stop
traversal. Project objects use -fno-rtti, so which function invokes which stored pointer
remains a manual dispatch/rustfill binding. Prebuilt C++ exception RTTI fragments
do not describe the project classes. A new target slot is visible; a new
unmodelled use of an already-hot slot need not be. Runtime-created pointers,
stored std::function state and callback lifetimes remain manual R8b work.

Locally compiled SDK/Arduino objects are inspected; prebuilt archives and ROM
are not walked. Rust is only a symbol table. Weak compiler-builtins fallbacks
are omitted only when a strong native libgcc/libm/libc definition precedes Rust
in the default link. New unmatched weak fallbacks and application weak exports
remain visible. Native LTO sections and other object formats/architectures fail;
x4pro/Xtensa is not determined. No catch-all indirect-call edges enter this gate.
The linked-image audit and manual runtime review are required for final R8b.
