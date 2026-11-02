"""App-owned EPUB rendering through actual native frame and metadata code.

Checks strip-on/off compositions and a wrong-font semantic mutant.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
PRODUCTION = Path('crates/xp-ffi/src/app/epub/book_render.rs')


def digest(value):
    return hashlib.sha256(value).hexdigest()


def once(source, before, after):
    assert source.count(before) == 1, ('source anchor', before, source.count(before))
    return source.replace(before, after, 1)


def sdk_source(root, definition):
    source = (root / 'test/reader_lifecycle/render_oracle.cpp').read_text()
    source = source[source.index('namespace HalDisplay {'):source.index('#ifdef RENDER_SESSION_OWNER\nstruct PageMetadata')]
    allocator = definition(source, '[[gnu::noinline]] std::unique_ptr<T> makeUniqueNoThrow(size_t size)')
    source = once(source, 'template <class T>\n' + allocator, '')
    source = once(source, 'void* operator new[](size_t size) {',
                  'void* operator new[](size_t size) {\n  assert(criticalDepth == 0);')
    source = once(source, 'void operator delete[](void* pointer) noexcept {',
                  'void operator delete[](void* pointer) noexcept {\n  assert(criticalDepth == 0);')
    source = once(source, '  bool deviceIsX3() const { return x3; }',
                  '  bool deviceIsX3() const { return x3; }\n  uint32_t lastTouchHeldMs() const { return 0; }')
    source = once(source, 'int getReaderFontId() const { return 12; }', 'int getReaderFontId() const { return 41; }')
    source = once(source, '  void (*paint)();', '  uint32_t (*paint)(void*, uint8_t*, uint16_t, uint16_t);')
    source = once(source, '  int getDisplayWidthBytes() const { return 2; }', '  int getDisplayWidthBytes() const { return 66; }')
    source = once(source, '  int getDisplayWidth() const { return 16; }', '  int getDisplayWidth() const { return 528; }')
    source = once(source, '  int getDisplayHeight() const { return 83; }', '  int getDisplayHeight() const { return 792; }')
    source = once(source, '  int getScreenHeight() const { return 83; }', '  int getScreenHeight() const { return 792; }')
    source = once(source, '  RenderMode renderMode = BW;', '''  std::array<uint8_t, 66 * 792> frameBuffer{};
  uint8_t* stripTarget = nullptr;
  int stripRows = 0;
  uint8_t* getFrameBuffer() { return frameBuffer.data(); }
  void copyStatusToStrip(int, bool) {}
  XpNativeProvider textProvider() const { return {}; }
  int fontHeight = 20;
  int getLineHeight(int32_t) const { return fontHeight; }
  int getTextWidth(int, const char*, int) const { return 24; }
  std::array<int,4> getClipRect() const { return {}; }
  void setClipRect(int,int,int,int) {}
  void drawUiText(const XpUiCommand&, XpUiRect, const char*) {}
  bool paintOk = true;
  bool drawUiScene(const XpUiScene&, const XpUiPaint*) const { return paintOk; }
  void physicalPageInk() {
    std::memset(stripTarget ? stripTarget : frameBuffer.data(), 0x5a,
                stripTarget ? size_t(stripRows) * 66 : frameBuffer.size());
  }
  RenderMode renderMode = BW;''')
    source = once(source, '  void clearScreen(uint8_t value = 0xff) { std::printf("clear %u\\n", value); }',
                  '''  void clearScreen(uint8_t value = 0xff) {
    std::memset(stripTarget ? stripTarget : frameBuffer.data(), value,
                stripTarget ? size_t(stripRows) * 66 : frameBuffer.size());
    a1Physical("clear", value, 0, 0);
  }''')
    source = once(source, '    std::printf("mode %d\\n", mode);',
                  '    a1Physical("mode", int(mode), 0, 0);')
    source = once(source, '  void beginStripTarget(uint8_t*, int y, int rows) { std::printf("strip-begin %d %d\\n", y, rows); }',
                  '''  void beginStripTarget(uint8_t* data, int y, int rows) {
    assert(data && !stripTarget); stripTarget = data; stripRows = rows;
    a1Physical("strip-begin", y, rows, 0);
  }''')
    source = once(source, '  void endStripTarget() { std::puts("strip-end"); }',
                  '  void endStripTarget() { assert(stripTarget); stripTarget = nullptr; stripRows = 0; a1Physical("strip-end",0,0,0); }')
    source = once(source, '  void cleanupGrayscaleWithFrameBuffer() { std::puts("cleanup"); }',
                  '  void cleanupGrayscaleWithFrameBuffer() { a1Physical("cleanup",0,0,0); }')
    source = once(source, '  void displayBufferAsync(HalDisplay::Mode mode) const { std::printf("async %d\\n", mode); }',
                  '  void displayBufferAsync(HalDisplay::Mode mode) const { a1Physical("display-async",int(mode),0,0); }')
    source = once(source, '  void restoreBwBuffer() { std::puts("restore"); }',
                  '  void restoreBwBuffer() { a1Physical("restore",0,0,0); }')
    source = once(source, 'static bool replayOnDetach;', 'static bool replayOnDetach = true;')
    source = once(source, '    std::printf("source %d\\n", source != nullptr);',
                  '    a1Physical("source", source != nullptr, 0, 0);')
    source = once(source, '    if (!source || sourceOk) boundSource = source;', '''    if (!source || sourceOk) boundSource = source;
    if (source && sourceOk) {
      assert(source->paint && source->context && source->scratch);
      for (uint16_t y = 0; y < 792; y += 80) {
        const uint16_t rows = std::min<uint16_t>(80, 792 - y);
        assert(source->paint(source->context, source->scratch, y, rows) == 0);
      }
    }''')
    source = once(source, '  Page() { assert(livePages++ == 0); }', '''  Page() {
    assert(livePages++ == 0);
    std::strcpy(note.number, "7"); std::strcpy(note.href, "chapter.xhtml#note");
    std::strcpy(linkValue.href, "chapter.xhtml#link");
    linkValue.x=20; linkValue.y=25; linkValue.width=30; linkValue.height=18;
  }''')
    source = once(source, '  ~Page() { assert(livePages-- == 1); }', '''  ~Page() {
    std::memset(note.href, '!', sizeof(note.href)); std::memset(linkValue.href, '!', sizeof(linkValue.href));
    assert(livePages-- == 1);
  }''')
    source = once(source, '  bool images = false, decode = false;', '''  int number = 0;
  uint32_t visibleTextOffset = 100;
  FootnoteEntry note;
  PageLink linkValue;
  uint16_t footnoteCount() const { return 1; }
  uint16_t linkCount() const { return 1; }
  void footnote(uint16_t i, FootnoteEntry& out) const { assert(i==0); out=note; }
  void link(uint16_t i, PageLink& out) const { assert(i==0); out=linkValue; }
  bool images = false, decode = false;''')
    source = once(source, '  void render(GfxRenderer& r, int, int, int) const {\n    std::puts("page");',
                  '''  void render(GfxRenderer& r, int font, int left, int top) const {
    a1Physical("page-pass", font, left, top); r.physicalPageInk();''')
    source = once(source, '    std::printf("prepare %d %d %d\\n", width, images, prepareOk);',
                  '    a1Physical("prepare", width, images, prepareOk);')
    painter = definition(source, 'struct ReaderStripPainter {') + ';'
    actual_painter = definition((root / 'src/activities/reader/ReaderStripPainter.h').read_text(),
                                'struct ReaderStripPainter {') + ';'
    source = once(source, painter, actual_painter + '''
static void replayBeforeDetach() {
  assert(boundSource && livePages == 1);
  a1Physical("source-replay", 0, 8, 0);
  assert(boundSource->paint(boundSource->context, boundSource->scratch, 0, 8) == 0);
}
''')
    return '''#pragma once
#include <xp_ffi.h>
#include <xp_epub_session.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include "Epub/FootnoteEntry.h"
#include "Epub/PageLink.h"
static void a1Physical(const char*, int, int, int);
#ifndef LOG_DBG
#define LOG_DBG(...) ((void)0)
#endif
''' + source


def prepare(root, work, definition, native_leaf, failed):
    failed.write_stubs(root, work)
    stub = (work / 'Activity.h').read_text()
    start, end = stub.index('class HalDisplay {'), stub.index('class Activity {')
    stub = stub[:start] + '#include "a1_render_sdk.h"\n' + stub[end:]
    (work / 'Activity.h').write_text(stub)
    (work / 'a1_render_sdk.h').write_text(sdk_source(root, definition))
    # Keep current physical lifecycle/loop/input/toolbar facts; override renderer/Page below.
    (work / 'scene_physical_bindings.inc').write_bytes((root / 'test/reader_lifecycle/scene_physical_bindings.inc').read_bytes())
    source = failed.fixture_source(root, definition, native_leaf)
    source = source[:source.index('class EpubReaderActivity {')]
    source = once(source, 'struct { uint32_t lastTouchHeldMs() const {return 0;} } gpio;\n', '')
    native = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
    bridge = (HERE / 'a1_bindings.inc').read_text()
    metadata = definition((root / 'lib/Epub/Epub/Page.h').read_text(), 'class PageMetadata {') + ';\n'
    page_cpp = (root / 'lib/Epub/Epub/Page.cpp').read_text()
    metadata += definition(page_cpp, 'void PageMetadata::clear()') + '\n'
    metadata += definition(page_cpp, 'bool PageMetadata::assign(const Page& page)')
    bridge = once(bridge, '// ACTUAL_PAGE_METADATA', metadata)
    for marker, signature in [('FRAME', 'struct NativeRenderFrame {'), ('RESOURCES', 'struct NativeResources {')]:
        bridge = once(bridge, '// ACTUAL_NATIVE_' + marker, definition(native, signature) + ';')
    bridge = once(bridge, '// ACTUAL_NATIVE_ACCESS', definition(native, 'NativeResources& resources(void* handle)').replace('resources(', 'nativeResources('))
    fields = [('SERVICES', 'render_services'), ('BIND', 'render_bind_page'), ('FINISH', 'render_finish_page'),
              ('PAGE_INFO', 'page_info'), ('METADATA_ASSIGN', 'metadata_assign'), ('METADATA_CLEAR', 'metadata_clear'),
              ('METADATA_FOOTNOTES', 'metadata_footnotes'), ('METADATA_LINKS', 'metadata_links'),
              ('METADATA_FOOTNOTE', 'metadata_footnote'), ('METADATA_LINK', 'metadata_link')]
    for marker, field in fields:
        leaf = native_leaf(native, field).replace('resources(', 'nativeResources(').replace('text(', 'nativeText(')
        bridge = once(bridge, '/* ACTUAL_' + marker + ' */', leaf)
    source += bridge + '\n' + (HERE / 'a1_main.inc').read_text()
    (work / 'a1.cpp').write_text(source)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--archive-off', type=Path)
    parser.add_argument('--archive-on', type=Path)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_APP_EPUB_A1_EVIDENCE_DIR'),
                        help='optional new directory for retained evidence')
    args = parser.parse_args()
    root = args.repo.resolve()
    sys.dont_write_bytecode = True
    sys.path[:0] = [str(root / 'test/reader_lifecycle'), str(root / 'test')]
    from test_popup_resource import definition, native_leaf
    import test_failed_scene_owner as failed
    from rust_host import rust_link_flags
    paths = [root / 'rust' / PRODUCTION, root / 'src/activities/reader/RustEpubResources.cpp',
             root / 'src/activities/reader/ReaderStripPainter.h', root / 'lib/Epub/Epub/Page.cpp',
             root / 'lib/Epub/Epub/Page.h', root / 'test/reader_lifecycle/render_oracle.cpp',
             root / 'test/reader_lifecycle/app_owned_epub.cpp', root / 'test/reader_lifecycle/physical_defaults.h',
             root / 'test/reader_lifecycle/scene_owner_bridge.inc', root / 'test/reader_lifecycle/scene_physical_bindings.inc',
             root / 'test/reader_lifecycle/test_failed_scene_owner.py',
             root / 'lib/BlePageTurner/src/BleRust.h', root / 'lib/BlePageTurner/src/RadioPort.h',
             root / 'lib/BlePageTurner/include/BlePageTurner.h', root / 'lib/BlePageTurner/include/BleKeyBinding.h',
             root / 'freeink-sdk/libs/network/BleKeyboardHost/include/BleRemoteProfiles.h',
             HERE / 'a1_bindings.inc', HERE / 'a1_main.inc', Path(__file__).resolve()]
    paths += list((root / 'rust').rglob('*.rs'))
    paths = [p for p in paths if 'target' not in p.parts]
    hashes = {str(path): digest(path.read_bytes()) for path in paths}
    production = (root / 'rust' / PRODUCTION).read_text()
    mutant = once(production, '            viewport.spec.font,', '            viewport.spec.font.wrapping_add(1),')
    commands, results = [], []
    def command(name, argv, **kw):
        commands.append([str(a) for a in argv])
        (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
        (evidence / (name + '.stdout')).write_bytes(result.stdout)
        (evidence / (name + '.stderr')).write_bytes(result.stderr)
        return result
    try:
        with tempfile.TemporaryDirectory(prefix='app-epub-a1-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir.resolve() if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=False)
            source = prepare(root, work, definition, native_leaf, failed)
            for name in ('a1.cpp', 'a1_render_sdk.h', 'Activity.h', 'scene_physical_bindings.inc'):
                (evidence / name).write_bytes((work / name).read_bytes())
            copied = work / 'source'
            shutil.copytree(root / 'rust', copied / 'rust', ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied / name).symlink_to(root / name, target_is_directory=True)
            target = work / 'target'
            def build(mode, name, content):
                (copied / 'rust' / PRODUCTION).write_text(content)
                features = 'app-test' + (',reader-strips' if mode == 'on' else '')
                built = command(name + '.cargo', ['cargo', 'build', '--manifest-path', str(copied / 'rust/Cargo.toml'),
                    '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked', '--features', features], cwd=copied / 'rust')
                assert built.returncode == 0, (name, 'Cargo failure is not a mutant kill')
                archive = work / (name + '.a')
                shutil.copyfile(target / 'release/libxp_ffi.a', archive)
                return archive
            def exercise(mode, name, archive):
                flags = rust_link_flags(archive=str(archive))
                binary = work / name
                built = command(name + '.compile', ['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-O0',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g',
                    '-DRENDER_SESSION_OWNER=1', '-DXP_FFI_HOST_FAULT_INJECTION',
                    '-DXP_STRIP_RENDER=' + ('1' if mode == 'on' else '0'),
                    '-I', str(work), '-I', str(root / 'test/reader_lifecycle'),
                    '-I', str(root / 'test/ui_runtime'), '-I', str(root / 'test/ui_runtime/stubs'),
                    '-I', str(root / 'freeink-sdk/libs/network/BleKeyboardHost/include'),
                    '-I', str(root / 'lib/BlePageTurner/include'), '-I', str(root / 'lib/BlePageTurner/src'),
                    '-I', str(root / 'lib/Epub'), str(work / 'a1.cpp'), *flags, '-o', str(binary)])
                assert built.returncode == 0, (name, 'C++ failure is not a mutant kill', built.stderr.decode(errors='replace'))
                result = command(name, [str(binary)], timeout=30)
                results.append({'mode': mode, 'name': name, 'archive_sha256': digest(archive.read_bytes()),
                                'returncode': result.returncode, 'stdout_sha256': digest(result.stdout),
                                'stderr_sha256': digest(result.stderr)})
                return result
            for mode, supplied in [('off', args.archive_off), ('on', args.archive_on)]:
                archive = supplied.resolve() if supplied else build(mode, 'baseline-' + mode, production)
                result = exercise(mode, 'baseline-' + mode, archive)
                assert result.returncode == 0 and b'A1 PASS strip=' in result.stdout, ('baseline', mode, result.returncode, result.stderr)
                wrong = build(mode, 'wrong-font-' + mode, mutant)
                result = exercise(mode, 'wrong-font-' + mode, wrong)
                assert result.returncode == 42 and result.stderr == b'EXPECTED_ASSERTION: A1_WRONG_FONT\n', (
                    'specific semantic failure after teardown required', mode, result.returncode, result.stderr)
            manifest = {'scope': 'hook-free App-owned render/turn/overlay/refresh host composition',
                        'source_sha256': hashes, 'generated_sha256': digest(source.encode()), 'commands': commands,
                        'results': results, 'mutant_source_sha256': digest(mutant.encode()), 'replacement_count': 1,
                        'limits': 'synthetic Page/storage/SDK facts; actual native frame, strip painter and metadata copy; no device claim'}
            (evidence / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
            print('A1: strip off/on passed; both wrong-font mutants failed after teardown', flush=True)
    finally:
        for path, expected in hashes.items():
            assert digest(Path(path).read_bytes()) == expected, ('source changed', path)


if __name__ == '__main__':
    main()
