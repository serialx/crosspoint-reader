# Reviewing reader chain baselines

Run `python3 scripts/reader_chain_guard.py` on the matching default-X3 build.
A passing check matches a reviewed linked inventory; it does not prove all
runtime dispatch. `docs/contributing/rust.md` lists the R8b limits.

`allowlist.json` has three classes. `physical` entries permit a named physical
continuation with exact callers, sites and native implementation contracts.
`inherited` entries belong to R1, R1b, R2, R3a, R3b or R4 and must reach zero for
the common reading path. `deferred` entries belong to R5, R6 or R7; they remain
listed and may shrink. New or stale entries in either class fail until the
baseline change is reviewed. “Common path clean” means no **observed inherited
entry** remains; the guard must also pass, and R8b's proof is still required.

`uncertainty.json` gates each unresolved site and each reachable implementation.
A site uses its object/function, verified pointer source and occurrence number.
ELF symbol extents establish symbol ownership. An annotation beyond that extent
is not a field offset: anonymous references use the consuming instruction and
indirect-site ordinal. `unowned_references` in the report makes the missing
ownership explicit. Actual load offsets and verified symbol-relative offsets
remain significant. Anonymous data contents are not a complete points-to model.

Before editing either baseline, save the old report and record **each changed
key, its old/new value, the task, a reason from the table below, and the evidence
that supports it** in the task's review packet. This requirement applies to
function hashes and physical subgraph hashes as well as named entries. A
nonblank reason in JSON cannot prove that review occurred; the reviewer must
check this record against the narrow baseline diff. Never replace all hashes
merely because the command failed. There is no accept-output command.

| Acceptable reason | What the reviewer must verify |
| --- | --- |
| Chain or unknown site removed | The new linked witness and source change remove that exact crossing/site; no replacement was hidden or moved outside the roots. Delete its stale entry. |
| Identity or layout correction | Map old/new keys one-to-one using object provenance, owning symbol extents, slots and linked targets. The implementation and target sets remain equivalent. Preserve owners and reasons. Add a stability fixture when fixing normalization. |
| Compiler/linker transformation with unchanged source behavior | Inspect old/new disassembly, aliases, inlining and target sets, plus the pinned compiler/build policy. Distinguish changed generated code from a relocation-only difference. Record every affected key and any new resolved crossing; do not assume equal counts imply equivalence. |
| Intended implementation change | Cite the task and source diff, inspect the changed code and callback suppliers, and keep the correct inherited/deferred owner. A new chain is not justified by a convenient owner label. |
| New or changed physical continuation | Check the reason against the permitted physical role, all caller sites and the entire native subgraph before the first Rust return. A new reader decision hidden between the same endpoints is not a physical exception. |
| New unresolved site | Give a site-specific reason, supplying-contract evidence and remaining-proof owner. Prefer resolving it; an unrelated old unknown is not permission to add one. |
| Root, binding, compiler or LTO policy change | Re-audit provenance/reachability and source evidence before updating fingerprints. This is not an identity-only refresh. |

After review, update only the justified entries/hashes and their metadata, rerun
the linked guard and host tests, and commit the baseline changes with the
implementation. Keep the old report and per-key explanation so later reviewers
can distinguish an intentional change from a blanket refresh. R5–R7 deferral
is an objective boundary, not permission to grow those chains silently.
