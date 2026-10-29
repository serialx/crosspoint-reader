"""Build-only observers; production code and headers remain unchanged."""
import pathlib
import subprocess
import sys

root, out = map(pathlib.Path, sys.argv[1:])
subprocess.run([sys.executable, str(root / 'test/text_golden/instrument.py'), str(root), str(out)], check=True)
source = (out / 'GfxRenderer.cpp').read_text().replace('#include "Trace.h"', '#include "PageTrace.h"')
for anchor, hook in (
    ('  xp_native_draw_pixels_traced(\n', '  pageGoldenNativeDraw();\n'),
    ('\n  const auto& c = *static_cast<GlyphContext*>(raw);\n', '  pageGoldenEmission();\n'),
):
    if source.count(anchor) != 1:
        raise SystemExit('Page counter anchor changed: ' + anchor)
    source = source.replace(anchor, hook + anchor if 'xp_native' in anchor else anchor + hook)
for anchor, index in (
    ('  const auto status = xp_page_render_traced(', 0),
    ('        return static_cast<PageContext*>(raw)->glyph.renderer.ensureFontLoaded(id);', 1),
    ('        auto& c = static_cast<PageContext*>(raw)->glyph;', 2),
    ('        // Fallback residency accepts substrings, including focus and ruby tokens.', 3),
    ('        renderer.fontCacheManager_->recordText(reinterpret_cast<const char*>(text), id,', 4),
    ('        c.image(c.images, index, x, y, placeholder);', 5),
    ('          [](void* ctx, int32_t id, XpNativeFamily* out) {', 6),
    ('          [](void* ctx, int32_t id, uint32_t cp, uint8_t style, XpTextmGlyph* out) -> uint8_t {', 7),
    ('          [](void* ctx, int32_t id, uint32_t cp, uint8_t style) -> uint8_t {', 8),
    ('          [](void* ctx, int32_t id, uint32_t left, uint32_t right, uint8_t style) -> int8_t {', 9),
):
    if source.count(anchor) != 1:
        raise SystemExit('Page owner counter anchor changed: ' + anchor)
    hook = f'pageGoldenOwner({index});'
    source = source.replace(anchor, anchor + '\n' + hook if index >= 6 else hook + '\n' + anchor)
for name, index in (('xp_native_bidi_acquire', 10), ('xp_native_bidi_release', 11)):
    anchor = '          ' + name + ','
    if source.count(anchor) != 1:
        raise SystemExit('Page bidi counter anchor changed: ' + anchor)
    source = source.replace(anchor, f'          [] {{ pageGoldenOwner({index}); return {name}(); }},')
source = '#ifdef XP_STRIP_SPIKE\n#include "StripTrace.h"\n#endif\n' + source
anchor = '  const auto status = xp_page_render_traced('
source = source.replace(anchor, """  const auto status =
#ifdef XP_STRIP_SPIKE
#ifdef XP_STRIP_PLAN
      stripPhase ? pageGoldenPlan(arena, size, &provider, &services, &paint, fontId, x, y, renderMode) :
#else
      stripPhase ? xp_page_strip_render(arena, size, &provider, &services, &paint, fontId, x, y, renderMode,
          stripBounds.data(), stripBounds.size(), &stripStats, stripPhase == 1) :
#endif
#endif
      xp_page_render_traced(""")
(out / 'GfxRenderer.cpp').write_text(source)
(out / 'Page.cpp').write_text((root / 'lib/Epub/Epub/Page.cpp').read_text())

source = (root / 'lib/Epub/Epub/blocks/TextBlock.cpp').read_text().replace('#include "../SectionCodec.h"', '#include <SectionCodec.h>')
(out / 'TextBlock.cpp').write_text(source)

source = (root / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
source = source.replace('#include "../SectionCodec.h"', '#include <SectionCodec.h>')
source = '#ifdef XP_STRIP_SPIKE\n#include "StripTrace.h"\n#endif\n' + source
# Keep the original image writer for the full-frame reference.
for width in ('pxcSlotWidth', 'cachedWidth'):
    anchor = f'    xp_image_packed_row(&target, x, y + row, {width}, rowBuffer, bytesPerRow);'
    if source.count(anchor) != 1:
        raise SystemExit('Image row observer anchor changed')
    source = source.replace(anchor, f"""#ifdef XP_STRIP_SPIKE
    if (stripPhase == 2) xp_strip_packed_row(&target, x, y + row, {width}, rowBuffer, bytesPerRow);
    else
#endif
{anchor}""")
(out / 'ImageBlock.cpp').write_text(source)
