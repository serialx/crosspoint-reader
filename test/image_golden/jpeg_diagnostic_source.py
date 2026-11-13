"""Observe native diagnostic arguments without adding a firmware test hook."""
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
anchor = "  switch (static_cast<XpBookJpegEvent>(event)) {"
assert source.count(anchor) == 1
source = source.replace(anchor, "  jpegDiagnosticArguments(event, width, height);\n" + anchor)
if len(sys.argv) == 4:
    assert sys.argv[3] == "old-control"
    for field in ("width", "height"):
        guarded = f"event == XP_BOOK_JPEG_EVENT_DIMENSIONS ? c.dimensions.{field} : 0"
        assert source.count(guarded) == 1
        source = source.replace(guarded, f"c.dimensions.{field}")
pathlib.Path(sys.argv[2]).write_text(
    "#include <cstdint>\nvoid jpegDiagnosticArguments(uint32_t, int, int);\n" + source
)
