# Linked X3 stack audit

Build the X3 `default` firmware with its pinned SDK and audit the resulting ELF. The audit reads RISC-V instructions, DWARF call-frame information, linked callback tables and checked route specifications. It writes the protected-route table and all changed totals to `summary.json` under the output directory.

From the firmware checkout, with PlatformIO, Python 3.11 or later, and `patch` installed, run:

```sh
git submodule update --init --recursive
pio run -e default
export PATH="$HOME/.platformio/packages/toolchain-riscv32-esp/bin:$PATH"
python3 test/stack_routes/audit.py --elf .pio/build/default/firmware.elf --out build/stack-audit
```

An existing ordinary X3 default ELF can be supplied with `--elf`. Use `--tool-prefix /path/to/riscv32-esp-elf-` for a different toolchain installation. A successful run covers 3,838 routes and 585 sleep continuations. The integrated reference ELF is identified in `data/totals.json`. Compare the input ELF hash and route deltas recorded in the new summary. Only the qualification inputs in this directory and the firmware sources are needed.

## Maintaining the inputs

- The two compressed inputs in `data/` preserve the original route identities and callback attestations. Their uncompressed SHA-256 values are `2e80d63347eeb9d8f34e543ab460563cd57ded96ed50540db7e9eb18e7df77a8` for routes and `ce3cb32b1e9a0f49bcaf842fddf025f19f4ca9977b6cc78f73da656d53a37342` for attestations. The runner verifies both, generates the current routes from the ELF, and compares them with the replay of `x3-default.patch` and `x3-default-attestations.patch`.
- `data/sync-bindings.json` names the callback callers and targets. `bindings.py` checks the instruction or initializer that selects each one. The audit also resolves the font and bookmark prologues, dictionary leaves, download GET and progress callback, Control Center, clipping, inflater and cache cleanup. Keep the caller, call site, all legal targets, frames and before/after totals with any rebinding. A relink can change the instructions used to establish a binding.
- `x3-default-sleep-event.json` specifies the emitter and callback paths. The audit checks the linked tables and sums the emitter prefix and physical fatal continuation. The separate sleep route model follows returning and fatal paths through file I/O, image decoding and cache operations. `data/rom.json` and the two bounds files carry the qualified X3 ROM and exception limits. Requalify these when the silicon or toolchain assumptions change.
- `data/wifi.json` records the qualified physical suffix's member frames and outgoing call inventories. A difference requires a new bound. Each tracked route also has a reference total and task ceiling. A growing route with less than 800 B remaining fails the audit. Review any change to the protected family ceilings in `report.py`.

The output directory retains the disassembly, edge proofs, selected chains, sleep continuations and ELF digest for review. These are static bounds for the named routes. Measure device task watermarks separately.
