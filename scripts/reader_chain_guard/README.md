# On-demand linked-image audit

Run `python3 scripts/reader_chain_guard.py --elf <ELF> --map <map> --report <JSON>`
from matching default-X3 source. This is an audit tool, not the build gate.
The gate is [the native port check](../rust_ports/README.md).

The analyser retains object-based language provenance, named root tables, source
callback supplements, direct calls, RISC-V tail jumps, static function-pointer
candidates and explicit unresolved indirect sites. `roots.json` holds those
source-backed bindings. `allowlist.json` now contains only 14 physical endpoint
names, categories and reasons as audit notes; it grants no exception to the port
gate. The generated unknown-site baseline and inherited/deferred ELF allowlist
have been removed. Address/implementation hashes in reports are diagnostics only.

The tool exits nonzero for malformed inputs or stale source evidence; the number
of chains or unresolved sites does not decide its exit status. Its synthetic
tests can be run manually with `python3 test/test_reader_chain_guard.py`; CTest
runs the port-check fixtures instead.

R8b must review all surviving physical continuations and dynamic callback paths.
Unknown indirect targets are not proved safe. Static data references can suggest
infeasible calls; LTO can merge languages or remove symbol boundaries. Compiler
register allocation, linker relaxation and unrelated layout changes can change
candidate paths or diagnostic hashes. Keep these limits in the audit report;
do not recreate a gate from a bulk hash baseline.
