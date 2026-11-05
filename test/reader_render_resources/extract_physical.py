"""Compile the dormant production physical adapters with the Page corpus harness."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf, replace_once

out = Path(sys.argv[1])
(out / 'physical').mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'test/reader_page_owner/extract_frame.py'), str(out)], check=True)
renderer = out / 'physical/GfxRenderer.h'
renderer_text = replace_once(renderer.read_text(),
    '  uint8_t* getFrameBuffer() { return framePixels; }',
    '  uint8_t* getFrameBuffer() { return framePixels; }\n'
    '  size_t getBufferSize() const { return frameStride * 800; }\n'
    '  uint16_t physicalWidthOverride = 0;')
renderer_text = replace_once(renderer_text, 'return frameStride * 8;',
    'return physicalWidthOverride ? physicalWidthOverride : frameStride * 8;')
renderer.write_text(renderer_text)
native = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
prefix = (root / 'test/reader_page_owner/Frame.cpp.in').read_text().split('// ACTUAL_FRAME')[0]
signatures = [
    '[[maybe_unused]] uint32_t physicalRenderQuery(',
    '[[maybe_unused]] uint32_t physicalRenderOperation(',
    '[[maybe_unused]] void physicalRenderPlan(',
    '[[maybe_unused]] void physicalImageWindow(',
    '[[maybe_unused]] uint8_t physicalDisplaySource(',
    '[[maybe_unused]] uint8_t* physicalFramebuffer(',
    '[[maybe_unused]] uint8_t physicalPrewarmBegin(',
    '[[maybe_unused]] void physicalPrewarmFinish(',
    '[[maybe_unused]] void physicalPrewarmDrop(',
]
prefix = '#include "activities/reader/RustEpubRenderPhysical.h"\n' + prefix
fragments = [definition(native, signature) for signature in signatures]
fragments[2] = '#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER\n' + fragments[2] + '\n#endif'
(out / 'ResourceFrame.cpp').write_text(prefix + '\n'.join(fragments)
    + '\n#undef ImageBlock\n}\n' + (here / 'PhysicalFrame.cpp.in').read_text())
bridge = (here / 'PhysicalFrame.cpp.in').read_text()
(out / 'ResourceCallbacks.h').write_text('#pragma once\n#include <xp_app.h>\n'
    '#include "activities/reader/RustEpubRenderPhysical.h"\n'
    + definition(bridge, 'struct ResourceCallbacks {') + ';\n')
log = definition((here / 'parent_frame.inc').read_text(), '[[gnu::noinline]] void log(uint32_t id) const')
(out / 'LogParity.cpp').write_text(replace_once((here / 'LogParity.cpp.in').read_text(), '// ACTUAL_LOG', log))
pixels = (here / 'PixelPhysical.cpp.in').read_text()
pixels = replace_once(pixels, '// ACTUAL_OPERATION', definition(native, signatures[1]))
pixels = replace_once(pixels, '// ACTUAL_FRAMEBUFFER', definition(native, signatures[5]))
(out / 'PixelPhysical.cpp').write_text(pixels)
# Keep all of the original oracle's assertions and failure controls unchanged.
oracle = (root / 'test/page_golden/ReaderStripReplay.cpp').read_text()
oracle = replace_once(oracle, '#include <activities/reader/ReaderStripPainter.h>',
    '#include "RustStripPainter.h"\n#define ReaderStripPainter RustStripPainter')
(out / 'RustStripReplay.cpp').write_text(oracle)

frequency = (here / 'Frequency.cpp.in').read_text()
frequency = replace_once(frequency, '// ACTUAL_GETTER', definition((root / 'src/CrossPointSettings.cpp').read_text(), 'int CrossPointSettings::getRefreshFrequency() const'))
frequency = replace_once(frequency, '/* ACTUAL_SNAPSHOT */', native_leaf(native, 'settings_snapshot'))
(out / 'Frequency.cpp').write_text(frequency)

# Add real session coverage beside the unchanged Page transfer oracle body.
session = (root / 'test/reader_page_owner/PageTransfer.cpp').read_text()
session = replace_once(session, 'void exercise(const uint8_t* wire, size_t size, bool faults) {',
    'void exercise(const uint8_t* wire, size_t size, bool faults) {\n'
    '  reset();\n  testRealSession(leaves, {wire, size}, renderer, faults);\n')
session = replace_once(session, 'void testPageFrame(const XpEpubPageView&, GfxRenderer&);',
    'void testPageFrame(const XpEpubPageView&, GfxRenderer&);\n'
    'void testRealSession(const XpEpubLeaves&, XpAppText, GfxRenderer&, bool);')
(out / 'SessionTransfer.cpp').write_text(session)
(out / 'SessionFrame.cpp').write_text((out / 'ResourceFrame.cpp').read_text() + '\n'
    + (here / 'SessionFrame.cpp.in').read_text())
