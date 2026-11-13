"""Observe native diagnostic arguments without adding a firmware test hook."""
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
anchor = "  switch (static_cast<XpBookJpegEvent>(event)) {"
assert source.count(anchor) == 1
source = source.replace(anchor, "  jpegDiagnosticArguments(event, width, height);\n" + anchor)
pathlib.Path(sys.argv[2]).write_text(
    "#include <cstdint>\nvoid jpegDiagnosticArguments(uint32_t, int, int);\n" + source
)
