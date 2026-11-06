# Native reader child oracle inputs

These are immutable, named test inputs for the O4a ownership switch. All14
snapshots are byte-for-byte copies of public source at8389157f, verified again
at e209b1b5 before deletion. `manifest.json` records each original path, byte
count and SHA-256, plus the pinned SDK and retained supporting sources.

The five retired owners and their headers preserve native menu construction,
chapter-window loading, Percent input/drawing, QR setup and Toolbar
render/input/publication. RustEpubResources.cpp preserves the four factory
adapters and eight Toolbar leaves. The three headers preserve the retired wire
contracts for historical test fixtures. The `.txt` suffix prevents these
snapshots from becoming compiled production sources or shadowing live headers.

At this checkpoint the existing tests still read the identical live sources.
The switch must move only their native-oracle reads to these named inputs,
while candidate compositions continue reading current production Rust/native
adapters. Header snapshots must never override the candidate include tree.
Retired wrappers may be represented only by named host-only compatibility
fixtures; they must remain absent from shipping images. Keep all existing
cases, immutable goldens and assertions. Do not regenerate these snapshots
from the candidate to make a comparison pass.

The checkpoint test disposition is in the evidence packet
`p16-o4a/CP3-TEST-DISPOSITION.md`. It identifies consumers that need mechanical
fixture binding changes and those that must continue exercising active owners.
This directory is test data only; it adds no firmware source, ABI or build step.
