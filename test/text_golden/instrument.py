"""Add observers to a build-directory copy; never replace measurement/drawing logic."""
import pathlib
import sys

root, out = map(pathlib.Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
source = (root / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
source = source.replace('#include "../Memory/Memory.h"', '#include <Memory.h>')
# Observe helper entry, including missing glyphs, before any bitmap access.
anchor = '  if (renderer.grayPlanesAreAbsolute()) renderMode = GfxRenderer::BW;'
if source.count(anchor) != 2:
    raise SystemExit('Glyph hook anchors changed: review instrument.py')
source = source.replace(anchor, '  textGoldenGlyph(fontFamily, cp, cursorX, cursorY, style);\n' + anchor)
source = source.replace('#include "GlyphBitmap.h"', '#include "GlyphBitmap.h"\n#include "Trace.h"')
# Drop caps bypass both helper entries; observe their actual API entry too.
anchor = '  if (scale256 <= 0) return 0;'
if source.count(anchor) != 1:
    raise SystemExit('Scaled glyph hook changed: review instrument.py')
source = source.replace(anchor, '  textGoldenScaled(fontId, cp, x, baselineY, style);\n' + anchor)
(out / 'GfxRenderer.cpp').write_text(source)
# Expose only the resolver to the harness, with identical declarations in every TU.
header = (root / 'lib/GfxRenderer/GfxRenderer.h').read_text()
anchor = '  int resolveTextFontId(int fontId, const char* text, EpdFontFamily::Style style) const;'
if header.count(anchor) != 1:
    raise SystemExit('Resolver declaration changed: review instrument.py')
header = header.replace(anchor, ' public:\n' + anchor + '\n private:')
(out / 'GfxRenderer.h').write_text(header)
