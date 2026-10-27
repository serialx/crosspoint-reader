"""Add observers to a build-directory copy; never replace measurement/drawing logic."""
import pathlib
import sys

root, out = map(pathlib.Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
source = (root / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
source = source.replace('#include "../Memory/Memory.h"', '#include <Memory.h>')
# The host-only Rust observer records placements before rasterization.
source = '#ifndef XP_FFI_HOST_FAULT_INJECTION\n#define XP_FFI_HOST_FAULT_INJECTION\n#endif\n' + source
source = source.replace('#include "GfxRenderer.h"', '#include "GfxRenderer.h"\n#include "Trace.h"')
source = source.replace('  xp_native_draw_pixels(\n', '  xp_native_draw_pixels_traced(\n')
anchor = '\n      &paint, &glyphMiss);'
if source.count(anchor) != 1:
    raise SystemExit('Glyph paint observer anchor changed')
source = source.replace(anchor, '''
      &paint, &glyphMiss, [](void* raw, const XpGlyphPlacement* glyph) {
        const auto& c = *static_cast<GlyphContext*>(raw);
        textGoldenGlyph(*c.font, glyph->codepoint, c.paint->x + glyph->x, c.paint->y + glyph->y, c.style);
      });''')
# Page draws share the same glyph observer without routing through drawText.
anchor = 'xp_page_render(arena, size, &provider, &services, &paint, fontId, x, y, renderMode)'
if source.count(anchor) != 1:
    raise SystemExit('Page paint observer anchor changed')
source = source.replace(anchor, """xp_page_render_traced(arena, size, &provider, &services, &paint, fontId, x, y, renderMode,
      [](void* raw, const XpGlyphPlacement* glyph) {
        const auto& c = static_cast<PageContext*>(raw)->glyph;
        textGoldenGlyph(*c.font, glyph->codepoint, c.paint->x + glyph->x, c.paint->y + glyph->y, c.style);
      })""")
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
