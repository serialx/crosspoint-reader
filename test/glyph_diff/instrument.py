"""Expose owner state and compile the frozen pre-port C++ raster reference."""
from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
for name, directory in [('GfxRenderer', 'GfxRenderer'), ('FontDecompressor', 'EpdFont'), ('SdCardFont', 'EpdFont')]:
    header = (root / 'lib' / directory / (name + '.h')).read_text()
    if name == 'GfxRenderer':
        header = '#include "GlyphBitmap.h"\n' + header
        header = header.replace('  void drawPixel(int x, int y, bool state = true) const;', '''
  void drawPixel(int x, int y, bool state = true) const;
  void drawGlyphBitmap(const uint8_t*, int, int, const glyphBitmap::Frame&, bool, RenderMode, bool) const;
  void drawScaledGlyph(int, uint32_t, EpdFontFamily::Style, int, int, int) const;
''')
    (out / (name + '.h')).write_text(header.replace(' private:', ' public:'))
    source = (root / 'lib' / directory / (name + '.cpp')).read_text()
    source = source.replace('#include "../Memory/Memory.h"', '#include <Memory.h>')
    if name == 'GfxRenderer':
        source += (root / 'test/glyph_diff/reference/Raster.inc').read_text()
        source += '''
void referenceGlyph(const GfxRenderer& renderer, const EpdFontFamily& family, uint32_t cp,
                    const EpdGlyph* glyph, int x, int y, int kind, int num, int den, bool state) {
  switch (kind) {
    case 0: renderCharImpl<TextRotation::None>(renderer, renderer.renderMode, family, cp, x, y,
              state, EpdFontFamily::REGULAR, glyph); break;
    case 1: renderCharImpl<TextRotation::Rotated90CW>(renderer, renderer.renderMode, family, cp, x, y,
              state, EpdFontFamily::REGULAR, glyph); break;
    case 2: renderCharScaled(renderer, renderer.renderMode, family, cp, x, y,
              state, EpdFontFamily::REGULAR, num, den, glyph); break;
  }
}
'''
    (out / (name + '.cpp')).write_text(source)

(out / 'GlyphBitmap.h').write_text((root / 'test/glyph_diff/reference/GlyphBitmap.h').read_text())
