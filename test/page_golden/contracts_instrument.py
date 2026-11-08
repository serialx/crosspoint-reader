"""Observe the actual residency spans in a build-directory renderer copy."""
import pathlib
import sys

root, output = map(pathlib.Path, sys.argv[1:])
source = (root / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
source = '#include <cstddef>\n#include <cstdint>\nvoid contractResident(const uint8_t*, size_t);\n' + source
anchor = "void xp_native_sd_font_resident_slice("
start = source.index("{", source.index(anchor)) + 1
source = source[:start] + "\n  contractResident(text, length);" + source[start:]
output.write_text(source)
