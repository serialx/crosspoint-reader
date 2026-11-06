# Frozen bookmark source inputs

Three public-source snapshots (4,593 B) were captured at `dff2b732`, before
P16-O5a removes `BookmarkFile` and `BookmarkEntry`. The manifest pins their bytes
and names their consumers. D0 expectations remain the independently frozen
`../golden.json` from `2543f2a4`.

`run.py` still compiles the live sources during this mechanical freeze checkpoint.
Before production retirement it will combine these verified declarations and
implementation into a generated oracle translation unit in the test build
directory. Snapshots must not enter an include path or shadow candidate headers.
The bookmark parent fixture also needs the historical record declaration.

Keep these inputs while the independent load/save/record and parent fixtures
consume them. Retire them only with equivalent independent coverage; never
regenerate expected bytes from the Rust candidate.
