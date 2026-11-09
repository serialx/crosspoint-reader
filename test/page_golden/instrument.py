"""Build-only observers; production code and headers remain unchanged."""
import pathlib
import re
import subprocess
import sys

root, out = map(pathlib.Path, sys.argv[1:])
subprocess.run([sys.executable, str(root / 'test/text_golden/instrument.py'), str(root), str(out)], check=True)
source = (out / 'GfxRenderer.cpp').read_text().replace('#include "Trace.h"', '#include "PageTrace.h"')
for anchor, hook in (
    ('  xp_native_draw_pixels_traced(\n', '  pageGoldenNativeDraw();\n'),
):
    if source.count(anchor) != 1:
        raise SystemExit('Page counter anchor changed: ' + anchor)
    source = source.replace(anchor, hook + anchor if 'xp_native' in anchor else anchor + hook)
if 'GfxRenderer::glyphMiss(' in source:
    raise SystemExit('Retired native bitmap-miss callback returned')
(out / 'GfxRenderer.cpp').write_text(source)
page_source = (root / 'lib/Epub/Epub/Page.cpp').read_text()
anchor = 'const auto status = xp_page_paint('
if page_source.count(anchor) != 2:
    raise SystemExit('Native Page adapter observer anchor changed')
page_source = '#include "StripTrace.h"\n' + page_source.replace(anchor, 'const auto status = pageGoldenPaint(')
(out / 'Page.cpp').write_text(page_source)

source = (root / 'lib/Epub/Epub/blocks/TextBlock.cpp').read_text()
(out / 'TextBlock.cpp').write_text(source)

source = (root / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
source = '#ifdef XP_STRIP_SPIKE\n#include "StripTrace.h"\n#endif\n' + source
# Select the independent strip writer at the new Rust cache-owner boundary.
anchor = '*output = imageWriteTarget(*static_cast<Context*>(raw)->renderer);'
if source.count(anchor) != 1:
    raise SystemExit('Image target observer anchor changed')
source = source.replace(anchor, """
#ifdef XP_STRIP_SPIKE
  xp_assets_image_strip_spike(stripPhase == 2);
#endif
""" + anchor + "\n")
source = source.replace('#include "StripTrace.h"', '#include "StripTrace.h"\nextern "C" void xp_assets_image_strip_spike(bool);')
# Image allocations existed in C++ before this port; account for that owner separately.
# The asset oracle enforces zero allocations on repeated successful cache passes.
render = 'void ImageBlock::render(GfxRenderer& renderer, const int x, const int y) {'
if source.count(render) != 1:
    raise SystemExit('Image allocation observer anchor changed')
source = source.replace(render, render + """
  extern size_t pageGoldenImageAllocations;
  struct AllocationScope {
    size_t start = xp_test_allocation_attempts();
    ~AllocationScope() { pageGoldenImageAllocations += xp_test_allocation_attempts() - start; }
  } allocationScope;
""")
source = '#include <cstddef>\nextern "C" size_t xp_test_allocation_attempts();\n' + source
# The HalFile init/drop interval excludes image allocations from the text allocation
# count, matching the ImageBlock::render scope above without a firmware test hook.
init = re.compile(r'case XP_IMAGE_FILE_INIT:\s*new \(storage\) HalFile\(\);\s*return 0;')
drop = re.compile(r'case XP_IMAGE_FILE_DROP:\s*file->~HalFile\(\);\s*return 0;')
if len(init.findall(source)) != 1 or len(drop.findall(source)) != 1:
    raise SystemExit('Direct image allocation window changed')
source = init.sub('''case XP_IMAGE_FILE_INIT: {
      extern size_t pageGoldenImageAllocations;
      static_assert(sizeof(HalFile) + sizeof(size_t) <= sizeof(XpPageImageFile));
      new (storage) HalFile();
      const size_t start = xp_test_allocation_attempts();
      std::memcpy(reinterpret_cast<uint8_t*>(storage) + sizeof(*storage) - sizeof(start), &start, sizeof(start));
      return 0;
    }''', source)
source = drop.sub('''case XP_IMAGE_FILE_DROP: {
      extern size_t pageGoldenImageAllocations;
      size_t start;
      std::memcpy(&start, reinterpret_cast<uint8_t*>(storage) + sizeof(*storage) - sizeof(start), sizeof(start));
      file->~HalFile();
      pageGoldenImageAllocations += xp_test_allocation_attempts() - start;
      return 0;
    }''', source)
(out / 'ImageBlock.cpp').write_text(source)
