# Frozen historical native vector oracle

`TtfEpdFont.cpp` and `TtfEpdFont.h` are unchanged copies of the native vector
owner at `f2b4a784`, retained when Rust took over its shipping responsibilities.
Host golden and differential tests use them as a historical reference; firmware
must not include this directory. Do not maintain them as a second implementation.

Current source, family, registration, variant and cache behavior is tested through
the production Rust owner by `VectorFontOwnedDifferential`,
`VectorManagerDifferential` and `VectorManagerLifetime`. See the
[vector owner qualification](../owned/README.md) for coverage and limits.
The separate `frozen/` directory retains the earlier oracle snapshots and their
compatibility adapters.
