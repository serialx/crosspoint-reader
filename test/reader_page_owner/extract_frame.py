"""Compile the current Page-bound native frame with the real Page codec."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf, replace_once

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
native = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
source = Path(__file__).with_name('Frame.cpp.in').read_text()
for marker, signature in [('FRAME', 'struct NativeRenderFrame {'),
                          ('RESOURCES', 'struct NativeResources {'),
                          ('RESOURCE_ACCESS', 'NativeResources& resources(void* handle)')]:
    fragment = definition(native, signature)
    source = replace_once(source, '// ACTUAL_' + marker, fragment + (';' if signature.startswith('struct') else ''))
for marker, leaf in [('BIND', 'render_bind_page'), ('FINISH', 'render_finish_page')]:
    source = replace_once(source, '/* ACTUAL_' + marker + ' */', native_leaf(native, leaf))
(out / 'PageFrame.cpp').write_text(source)
# Share the codec fixture's physical metrics; append only frame/display services.
renderer = (root / 'test/section_golden/stubs/GfxRenderer.h').read_text()
renderer = replace_once(renderer, '#include <Arduino.h>', '#include <Arduino.h>\n#include <FrameDisplay.h>')
renderer = replace_once(renderer, '"../../chapter_html_slim_parser/stubs/GfxRenderer.h"',
                        '"' + str(root / 'test/chapter_html_slim_parser/stubs/GfxRenderer.h') + '"')
renderer = replace_once(renderer, '  FontCacheManager* getFontCacheManager() const { return nullptr; }', '')
renderer = replace_once(renderer, '  void preserveImagePolarity(int, int, int, int) {}',
                        '  void preserveImagePolarity(int, int, int, int) {}\n' + Path(__file__).with_name('frame_renderer.inc').read_text())
(out / 'physical/GfxRenderer.h').write_text(renderer)
