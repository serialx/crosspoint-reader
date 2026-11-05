"""Preserve all 131 failed-scene scenarios through the App-owned EPUB session.

Use actual native toolbar/command faults and fail on unexpected physical calls.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
PRODUCTION = Path('crates/xp-ffi/src/app/epub/loop_owner.rs')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def once(source, old, new):
    assert source.count(old) == 1, ('mechanical source anchor', old, source.count(old))
    return source.replace(old, new, 1)


def write_stubs(root, out):
    stub = (root / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = '#include <array>\n#include <cassert>\n' + stub
    stub = once(stub, 'using portMUX_TYPE = std::mutex;', 'using portMUX_TYPE = std::recursive_mutex;')
    stub = once(stub, '#define taskENTER_CRITICAL(m) (m)->lock()',
                'inline thread_local unsigned criticalDepth=0;\n#define taskENTER_CRITICAL(m) ((m)->lock(), ++criticalDepth)')
    stub = once(stub, '#define taskEXIT_CRITICAL(m) (m)->unlock()',
                '#define taskEXIT_CRITICAL(m) (--criticalDepth, (m)->unlock())')
    stub = once(stub, 'class RenderLock {',
                'inline thread_local unsigned sceneRenderDepth = 0;\n'
                'void sceneLockDepthChanged(unsigned);\nstruct SceneAdoptRenderLock {};\nclass RenderLock {')
    stub = once(stub, '  RenderLock() { renderMutex.lock(); }',
                '  RenderLock() { assert(!sceneRenderDepth); renderMutex.lock();\n'
                '    sceneRenderDepth=1; sceneLockDepthChanged(1); }\n'
                '  explicit RenderLock(SceneAdoptRenderLock) { assert(!sceneRenderDepth);\n'
                '    sceneRenderDepth=1; sceneLockDepthChanged(1); }')
    stub = once(stub, '  ~RenderLock() { renderMutex.unlock(); }',
                '  ~RenderLock() { assert(sceneRenderDepth==1); sceneRenderDepth=0;\n'
                '    sceneLockDepthChanged(0); renderMutex.unlock(); }')
    stub = once(stub, 'uint8_t touchFlags = 0;', 'int16_t touchX=10, touchY=10; uint8_t touchFlags = 0;')
    stub = once(stub, '  bool longConfirm = false;', '''  bool hasTouch() const { return true; }
  bool isNavDirectionSwapped() const { return false; }
  uint32_t homeButtonAction() const { return 0; }
  bool wasBackGesture() const { return false; }
  bool wasMenuGesture() const { return false; }
  bool wasReaderMenuSwipeUp() const { return false; }
  bool wasScreenTapped(int& x, int& y) const { x=touchX; y=touchY; return touchFlags & XP_UI_TOUCH_RELEASED; }
  bool longConfirm = false;''')
    stub = once(stub, 'return 20;', 'return fontHeight;')
    stub = once(stub, '  bool paintOk = true;', '''  int fontHeight=20;
  int getTextWidth(int, const char*, int) const {return 24;}
  std::array<int,4> getClipRect() const {return {};}
  void setClipRect(int,int,int,int) {}
  void drawUiText(const XpUiCommand&,XpUiRect,const char*) {}
  bool paintOk = true;''')
    (out / 'Activity.h').write_text(stub)
    (out / 'components').mkdir()
    touch = (root / 'test/ui_runtime/stubs/components/UiAppHelpers.h').read_text()
    (out / 'components/UiAppHelpers.h').write_text(once(touch, 'return {10, 10,', 'return {input.touchX, input.touchY,'))
    (out / 'physical_defaults.h').write_bytes((root / 'test/reader_lifecycle/physical_defaults.h').read_bytes())
    (out / 'scene_physical_bindings.inc').write_bytes((HERE / 'scene_physical_bindings.inc').read_bytes())


def fixture_source(root, definition, native_leaf):
    oracle = root / 'test/reader_lifecycle'
    original = (oracle / 'failed_scene.cpp').read_text()
    prefix = original[:original.index('class ReaderActivity :')]
    prefix = once(prefix, '#include "RustActivityAdapter.h"', '#include "Activity.h"')
    toolbar = (root / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
    header = (root / 'src/activities/reader/ReaderToolbarUi.h').read_text()
    header = re.sub(r'^#include.*\n|^#pragma once\n', '', header, flags=re.M)
    header = header.replace(' private:', ' public:')
    commands = (root / 'src/activities/reader/ReaderChromeListActivity.h').read_text()
    commands = commands[commands.index('class ReaderChromeCommands {'):commands.index('// Owns the resource lifetime')]
    command_source = (root / 'src/activities/reader/ReaderChromeListActivity.cpp').read_text()
    methods = toolbar[toolbar.index('void ReaderToolbarUi::begin()'):]
    methods = once(methods, 'xp_ui_reader_toolbar_view(', 'injectedView(')
    prefix = once(prefix, '// COMMAND_CLASS', commands)
    prefix = once(prefix, '// TOOLBAR_CLASS', header)
    prefix = once(prefix, '// TOOLBAR_METHODS', definition(command_source, 'bool ReaderChromeCommands::reserve(') + '\n' +
                  definition(command_source, 'bool ReaderChromeCommands::grow(') + '\n' + methods)
    scaffold = (oracle / 'app_owned_epub.cpp').read_text().split('int main(int argc, char** argv) {', 1)[0]
    scaffold += '\nvoid sceneLockDepthChanged(unsigned depth) { if (fixture) fixture->renderDepth=int(depth); }\n'
    bridge = (HERE / 'scene_owner_bridge.inc').read_text()
    native = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
    bridge = once(bridge, '// ACTUAL_PATH_TEXT', definition(native, 'const char* pathText(XpAppText value)') + '\n' +
                  definition(native, 'std::string_view view(XpAppText value)'))
    memory = (root / 'lib/Memory/Memory.h').read_text()
    begin = memory.index('template <typename F>\nstruct [[nodiscard]] ScopedCleanup')
    end = memory.index('ScopedCleanup(F) -> ScopedCleanup<F>;', begin) + len('ScopedCleanup(F) -> ScopedCleanup<F>;')
    bridge = once(bridge, '// ACTUAL_SCOPED_CLEANUP', memory[begin:end])
    for marker, field in [('PAINT', 'toolbar_paint'), ('ROUTE', 'toolbar_route'), ('SCROLL', 'toolbar_scroll'),
                          ('VISIBLE', 'toolbar_visible_rows'), ('RESET', 'toolbar_reset_navigation')]:
        bridge = once(bridge, '/* ACTUAL_' + marker + '_LEAF */', native_leaf(native, field))
    snapshot = (root / 'src/components/RustInputSnapshot.h').read_text()
    helpers = (root / 'src/activities/reader/ReaderUtils.h').read_text()
    input_methods = '''struct { uint32_t lastTouchHeldMs() const {return 0;} } gpio;
struct { bool wasTiltedForward() const {return false;} bool wasTiltedBack() const {return false;} } halTiltSensor;
namespace ReaderUtils {
''' + definition(helpers, 'inline XpUiReaderInputServices inputServices(') + '\n}\n'
    bridge = once(bridge, '// ACTUAL_INPUT_SNAPSHOT', definition(snapshot, 'inline XpUiInput rustInputSnapshot(') + '\n' + input_methods)
    main = original[original.index('int main(int argc, char** argv) {'):]
    main = once(main, 'int main(int argc, char** argv) {', 'static int failedSceneScenario(int argc, char** argv) {')
    # Keep the original observation values after retiring the facade enum names.
    for name, value in (('XP_UI_READER_OVERLAY_EVENT_TOOL', '2'),
                        ('XP_UI_READER_OVERLAY_MODE_TEXT', '3'),
                        ('XP_UI_READER_OVERLAY_MODE_TOOLBAR', '1')):
        main = main.replace(name, value)
    main = once(main, '  if (check == "cancel-mixed") {\n    a.build();',
                '  if (check == "cancel-mixed") {\n    a.build();\n    a.primeMixedRefresh();')
    start = main.index('    if (stage == "contents") a.fields[0]')
    end = main.index('    a.routes = a.sampled', start)
    main = main[:start] + '    a.seedMixed(stage);\n' + main[end:]
    main = once(main, '    a.panelHoldJumped = true;', '    a.seedHeldPanel();')
    original_assertion = '    assert(a.actions == previous && a.notifications == 1 && a.requested > 0);'
    main = once(main, original_assertion, original_assertion + '''
    if (scene.afterFailureRoutes || scene.afterFailureSamples)
      throw std::runtime_error("FAILED_SCENE_FINAL_DISPATCH");
    assert(scene.dispatchInjections == 1);''')
    main = once(main, '  assert(a.actions == previous + 1);\n}',
                '  assert(a.actions == previous + 1);\n  return 0;\n}')
    # Each original return still destroys the App-owned session through the same fixture RAII.
    main += '''
int main(int argc, char** argv) {
  try { return failedSceneScenario(argc, argv); }
  catch (const std::runtime_error& failure) {
    std::fprintf(stderr, "EXPECTED_ASSERTION: %s\\n", failure.what());
    return 42;
  }
}
'''
    return '#include <stdexcept>\n' + prefix + '\n' + scaffold + '\n' + bridge + '\n' + main


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--archive', type=Path, help='App-owned app-test archive with installed App/scene entries')
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_FAILED_SCENE_EVIDENCE_DIR'),
                        help='new directory only')
    args = parser.parse_args()
    root = args.repo.resolve()
    sys.dont_write_bytecode = True
    sys.path[:0] = [str(root / 'test/reader_lifecycle'), str(root / 'test')]
    from extract import definition
    from test_popup_resource import native_leaf
    from rust_host import rust_link_flags
    source = fixture_source(root, definition, native_leaf)
    production = (root / 'rust' / PRODUCTION).read_text()
    before = 'if !self.consume_rejected_input() {\n                    self.handle_overlay_input();\n                }'
    changed = once(production, before, 'self.handle_overlay_input();')
    sources = [root / 'rust' / PRODUCTION, root / 'test/reader_lifecycle/app_owned_epub.cpp',
               root / 'test/reader_lifecycle/physical_defaults.h',
               root / 'src/components/RustInputSnapshot.h', root / 'src/activities/reader/ReaderUtils.h',
               root / 'src/activities/reader/ReaderToolbarUi.cpp', root / 'src/activities/reader/ReaderToolbarUi.h',
               root / 'src/activities/reader/ReaderChromeListActivity.cpp',
               root / 'src/activities/reader/ReaderChromeListActivity.h',
               root / 'src/activities/reader/RustEpubResources.cpp', root / 'test/reader_lifecycle/failed_scene.cpp',
               root / 'test/reader_lifecycle/test_failed_scene.py', root / 'test/ui_runtime/stubs/Activity.h',
               root / 'test/ui_runtime/stubs/components/UiAppHelpers.h', root / 'lib/Memory/Memory.h']
    sources += [HERE / name for name in ('scene_owner_bridge.inc', 'scene_physical_bindings.inc')]
    sources += [Path(__file__).resolve()]
    hashes = {str(path): digest(path.read_bytes()) for path in sources}
    cases = [('first', 'builder', 'prepaint')]
    cases += [(stage, failure, guarantee) for stage in ('first', 'open', 'setting')
              for failure in ('builder', 'reserve', 'grow', 'paint')
              for guarantee in ('button', 'touch', 'held-button', 'held-touch', 'retry', 'notification',
                                'busy', 'back', 'gesture-state', 'dispatch')]
    cases += [(variant, recovery, 'cancel-mixed') for variant in ('toolbar', 'contents', 'text', 'more', 'popup')
              for recovery in ('failed', 'recover')]
    assert len(cases) == 131 and len(set(cases)) == 131
    try:
        with tempfile.TemporaryDirectory(prefix='app-owned-failed-scene-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=False)
            write_stubs(root, work)
            (work / 'scene.cpp').write_text(source)
            (evidence / 'scene.cpp').write_text(source)
            commands, records = [], []
            def command(name, argv, **kwargs):
                commands.append(list(map(str, argv)))
                (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
                result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result
            def compile_fixture(name, archive):
                flags = rust_link_flags() if archive is None else rust_link_flags(archive=str(archive))
                binary = work / name
                result = command(name + '.compile', ['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-O0',
                                    '-DXP_FFI_HOST_FAULT_INJECTION',
                                    '-I', str(work), '-I', str(root / 'test/ui_runtime'),
                                    '-I', str(root / 'test/ui_runtime/stubs'), '-I', str(root / 'test/reader_lifecycle'),
                                    str(work / 'scene.cpp'), *flags, '-o', str(binary)])
                assert result.returncode == 0, (name, 'compile failure', result.stderr.decode(errors='replace'))
                return binary
            baseline = compile_fixture('baseline', args.archive)
            for case in cases:
                name = '-'.join(case)
                result = command(name, [str(baseline), *case], timeout=10)
                records.append({'case': case, 'returncode': result.returncode,
                                'stdout_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr)})
                assert result.returncode == 0, (case, result.returncode, result.stderr.decode(errors='replace'))
            copied = work / 'source'
            shutil.copytree(root / 'rust', copied / 'rust', ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied / name).symlink_to(root / name, target_is_directory=True)
            (copied / 'rust' / PRODUCTION).write_text(changed)
            target = work / 'target'
            result = command('mutant.cargo', ['cargo', 'build', '--manifest-path', str(copied / 'rust/Cargo.toml'),
                              '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                              '--features', 'app-test'], cwd=copied / 'rust')
            assert result.returncode == 0, 'actual final-guard mutant did not build'
            mutant = compile_fixture('mutant', target / 'release/libxp_ffi.a')
            result = command('mutant', [str(mutant), 'open', 'grow', 'dispatch'], timeout=10)
            assert result.returncode == 42 and result.stderr == b'EXPECTED_ASSERTION: FAILED_SCENE_FINAL_DISPATCH\n', (
                'specific semantic failure required; crashes/abort/compile errors do not kill mutant',
                result.returncode, result.stderr.decode(errors='replace'))
            summary = {'scenario_count': 131, 'scenario_results': records, 'source_sha256': hashes,
                       'generated_sha256': digest(source.encode()), 'commands': commands,
                       'mutant': {'source': str(PRODUCTION), 'sha256': digest(changed.encode()), 'returncode': result.returncode},
                       'limits': 'App-owned hook-free host composition with deterministic physical injection; '
                                 'synthetic storage/renderer/input facts; no real-thread dispatch/recovery race or device claim'}
            (evidence / 'manifest.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps({'scenario_count': 131, 'mutant': summary['mutant'], 'limits': summary['limits']}), flush=True)
    finally:
        for path, expected in hashes.items():
            assert digest(Path(path).read_bytes()) == expected, ('source changed', path)


if __name__ == '__main__':
    main()
