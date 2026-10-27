"""Expose production helpers in build copies; do not replace their logic."""
from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
for name, directory in [('GfxRenderer', 'GfxRenderer'), ('FontDecompressor', 'EpdFont'), ('SdCardFont', 'EpdFont')]:
    header = (root / 'lib' / directory / (name + '.h')).read_text()
    (out / (name + '.h')).write_text(header.replace(' private:', ' public:'))
    source = (root / 'lib' / directory / (name + '.cpp')).read_text()
    source = source.replace('#include "../Memory/Memory.h"', '#include <Memory.h>')
    if name == 'GfxRenderer':
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
