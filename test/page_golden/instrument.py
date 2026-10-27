"""Build-only observers; production code and headers remain unchanged."""
import pathlib
import subprocess
import sys

root, out = map(pathlib.Path, sys.argv[1:])
subprocess.run([sys.executable, str(root / 'test/text_golden/instrument.py'), str(root), str(out)], check=True)
source = (out / 'GfxRenderer.cpp').read_text().replace('#include "Trace.h"', '#include "PageTrace.h"')
for anchor, hook in (
    ('  xp_native_draw(\n', '  pageGoldenNativeDraw();\n'),
    ('      [](void* raw, const XpGlyphPlacement* glyph) {\n', '        pageGoldenEmission();\n'),
):
    if source.count(anchor) != 1:
        raise SystemExit('Page counter anchor changed: ' + anchor)
    source = source.replace(anchor, hook + anchor if 'xp_native' in anchor else anchor + hook)
(out / 'GfxRenderer.cpp').write_text(source)
(out / 'Page.cpp').write_text((root / 'lib/Epub/Epub/Page.cpp').read_text())

source = (root / 'lib/Epub/Epub/blocks/TextBlock.cpp').read_text().replace('#include "../SectionCodec.h"', '#include <SectionCodec.h>')
(out / 'TextBlock.cpp').write_text(source)
(out / 'GlyphBitmap.h').write_text((root / 'lib/GfxRenderer/GlyphBitmap.h').read_text())
