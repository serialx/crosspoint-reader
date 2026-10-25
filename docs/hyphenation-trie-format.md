# Hypher Binary Tries

CrossPoint embeds the exact binary automata produced by
[Typst's `hypher`](https://github.com/typst/hypher). 

## File layout

Each `.bin` blob is a single self-contained automaton:

```
uint32_t root_addr_be;  // big-endian offset of the root node
uint8_t  levels[];      // shared "levels" tape (dist/score pairs)
uint8_t  nodes[];       // node records packed back-to-back
```

The size of the `levels` tape is implicit. Individual nodes reference slices
inside that tape via 12-bit offsets, so no additional pointers are required.

### Node encoding

Every node starts with a single control byte:

- Bit 7 – set when the node stores scores (`levels`).
- Bits 5-6 – stride of the target deltas (1, 2, or 3 bytes, big-endian).
- Bits 0-4 – transition count (values ≥ 31 spill into an extra byte).

If the `levels` flag is set, two more bytes follow. Together they encode a
12-bit offset into the global `levels` tape and a 4-bit length. Each byte in the
levels tape packs a distance/score pair as `dist * 10 + score`, where `dist`
counts how many UTF-8 bytes we advanced since the previous digit.

After the optional levels header come the transition labels (one byte per edge)
followed by the signed target deltas. Targets are stored as relative offsets
from the current node address. Deltas up to ±128 fit in a single byte, larger
distances grow to 2 or 3 bytes. The runtime walks the transitions with a simple
linear scan and materializes the absolute address by adding the decoded delta
to the current node’s base.

## Embedding blobs into the firmware

The eleven raw `.bin` files live in `rust/crates/xp-core/data/hyph/`.
`xp-core::hyphen` embeds them with `include_bytes!` in a four-byte-aligned
wrapper. The root address remains in the first four bytes; node and level
offsets use the original file coordinates. The runtime reads bytes directly
from flash, without allocating or expanding the trie.

`scripts/generate_hyphenation_trie.py` validates the root address and copies
an input `.bin` unchanged. Refresh all languages with:

```sh
./scripts/update_hyphenation.sh
```

The updater downloads from [hypher's tries directory](https://github.com/typst/hypher/tree/main/tries).
The initial Rust files preserve the prior C arrays byte-for-byte after their
four-byte root addresses. Updating upstream patterns is a behavior change;
run the exact golden comparison and review any differences separately.

Hypher's code is dual-licensed MIT/Apache-2.0. The pattern files retain their
[individual licenses](https://github.com/typst/hypher/tree/main/patterns), as
explained in the [upstream license notice](https://github.com/typst/hypher#license).
Moving the existing tries does not change those licenses.
