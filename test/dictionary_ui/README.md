# Dictionary and selection UI oracle

`python3 test/dictionary_ui/oracle.py` compiles the current C++ geometry and
compares it with `geometry.golden`, recorded at `dc21f50f` before the Rust port.
It also changes the word-hit slop from four pixels to three and requires a
mismatch. Use `--source PATH` to run against an archived source tree.

The cases cover four logical widths, all four orientations, three metric sets,
button and touch modes, LTR and RTL words, 240 words over three pages, selection
handles, action buttons, scrolling offsets, hit routing and vertical/page moves.
Metrics and glyph advances are deterministic host fixtures; emulator captures
supply actual theme/font pixel and refresh coverage.
