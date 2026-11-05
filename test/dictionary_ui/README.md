# Dictionary and selection UI oracle

`python3 test/dictionary_ui/oracle.py` runs the Rust geometry and
compares it with `geometry.golden.gz`, recorded at `dc21f50f` before the Rust port.
Use `--source PATH` to compile the archived C++ source instead; that path also
changes the word-hit slop from four pixels to three and requires a mismatch.

The cases cover four logical widths, all four orientations, three metric sets,
button and touch modes, LTR and RTL words, 240 words over three pages, selection
handles, action buttons, scrolling offsets, hit routing and vertical/page moves.
Metrics and glyph advances are deterministic host fixtures; emulator captures
supply actual theme/font pixel and refresh coverage.

`python3 test/dictionary_ui/paint_oracle.py` compares full and incremental Rust
selection painting with frozen C++ packed pixels for 240 and 720 borrowed words.
It also requires full-target and 16-row strip painting to agree in every orientation.
