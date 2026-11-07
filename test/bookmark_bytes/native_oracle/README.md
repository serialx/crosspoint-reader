# Frozen bookmark source inputs

Three public-source snapshots (4,593 B) were captured at `dff2b732`, before
P16-O5a removes `BookmarkFile` and `BookmarkEntry`. The manifest pins their bytes
and names their consumers. D0 expectations remain the independently frozen
`../golden.json` from `2543f2a4`.

`prepare_native.py` verifies the snapshots and combines their declarations in
`BookmarkOracleFixture.h`, with explicitly named `BookmarkOracleEntry` and
`BookmarkOracleFile` fixtures. The generated implementation uses those names;
the original serializer bodies remain unchanged. No production-shaped header
tree is created. Both byte-runner compiler commands pass the include-isolation
guard, whose negative controls also reject copied historical header trees.
Snapshots must not enter an include path or shadow candidate headers.
The bookmark parent fixture also needs the historical record declaration.

Keep these inputs while the independent load/save/record and parent fixtures
consume them. Retire them only with equivalent independent coverage; never
regenerate expected bytes from the Rust candidate.
