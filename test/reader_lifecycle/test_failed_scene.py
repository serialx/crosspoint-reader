"""Inject failures into production toolbar rendering and the reader input boundary."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('XP_READER_GATE_SOURCE', ROOT))
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

revision = os.environ.get('XP_READER_GATE_REVISION')
def read(path):
    return (subprocess.check_output(['git', 'show', revision + ':' + path], cwd=ROOT, text=True)
            if revision else (SOURCE / path).read_text())
toolbar = read('src/activities/reader/ReaderToolbarUi.cpp')
parent = read('src/activities/reader/EpubReaderActivity.cpp')
header = read('src/activities/reader/ReaderToolbarUi.h')
parent_header = read('src/activities/reader/EpubReaderActivity.h')
header = re.sub(r'^#include.*\n|^#pragma once\n', '', header, flags=re.M)
# Public access is fixture-only: inspect publication and force an exact growth point.
header = header.replace(' private:', ' public:')
commands = (SOURCE / 'src/activities/reader/ReaderChromeListActivity.h').read_text()
commands = commands[commands.index('class ReaderChromeCommands {'):commands.index('// Owns the resource lifetime')]
methods = toolbar[toolbar.index('void ReaderToolbarUi::begin()'):]
# Interpose only the builder result. Normal calls execute the real Rust builder.
methods = methods.replace('xp_ui_reader_toolbar_view(', 'injectedView(')
reserve = (SOURCE / 'src/activities/reader/ReaderChromeListActivity.cpp').read_text()
reserve = definition(reserve, 'bool ReaderChromeCommands::reserve(') + '\n' + definition(reserve, 'bool ReaderChromeCommands::grow(')
# Preserve the actual loop entry ordering; stop before its unrelated book lifecycle.
loop = definition(parent, 'void EpubReaderActivity::loop()')
loop = (loop[:loop.index('  static_assert')] if not revision else 'void EpubReaderActivity::loop() {\n') + '  dispatchOverlayInput();\n}'
# This is the actual render-to-publication boundary, with its model already prepared.
if revision:
    publication = parent[parent.index('    toolbarUi->setModel(model);'):]
    publication = publication[:publication.index('    return;')].replace('toolbarUi->', 'o.toolbarUi->')
else:
    publication = parent[parent.index('        o.toolbarUi->setModel(model);'):]
    publication = publication[:publication.index('      }};')]
cancel = ''
if 'void EpubReaderActivity::cancelRejectedInput(' in parent:
    cancel = definition(parent, 'void EpubReaderActivity::cancelRejectedInput(').replace('::cancelRejectedInput(', '::dispatchCancel(')
fixture = (HERE / 'failed_scene.cpp').read_text()
fixture = fixture.replace('// COMMAND_CLASS', commands).replace('// TOOLBAR_CLASS', header)
fixture = fixture.replace('// TOOLBAR_METHODS', reserve + '\n' + methods)
fixture = fixture.replace('// LOOP_METHOD', loop).replace('// PUBLICATION', publication)
if revision:
    dispatch = 'o.handleOverlayInput();'
else:
    dispatch = parent[parent.index('case XP_UI_READER_LOOP_COMMAND_OVERLAY_INPUT:'):]
    dispatch = dispatch[dispatch.index(':')+1:dispatch.index('break;')]
fixture = fixture.replace('// OVERLAY_DISPATCH', dispatch)
render = definition(parent, 'void EpubReaderActivity::render(') if not revision else 'void EpubReaderActivity::render(RenderLock&& lock) { ReaderActivity::render(std::move(lock)); }'
fixture = fixture.replace('// PARENT_RENDER', render)
if revision:
    # The 8d534a99 parent uses the SDK ListNav; this fixture only resets it.
    fixture = re.sub(r'a.toolbarUi->resetNavigation\(0,\s*0\)', 'a.toolbarUi->nav().reset()', fixture)
failed = definition(parent_header, '  void rustSceneFailed()') if '  void rustSceneFailed()' in parent_header else '  void rustSceneFailed() {}'
failed = failed.replace('rustSceneFailed()', 'failedState()').replace(' override', '')
fixture = fixture.replace('// FAILED_STATE', failed)
fixture = fixture.replace('// CANCEL_METHOD', cancel)
fixture = fixture.replace('// INPUT_METHOD', definition(parent, 'void EpubReaderActivity::handleOverlayInput()'))
fixture = fixture.replace('// CANCEL_DECL', '''void dispatchCancel(const XpUiInput&);
  void cancelRejectedInput(const XpUiInput& input) override {
    if (recoverBeforeCancel) { failure=0; allocationFails=false; renderer.paintOk=true; build(); }
    dispatchCancel(input);
  }''' if cancel else '')
with tempfile.TemporaryDirectory(prefix='reader-failed-scene-') as directory:
    out = Path(directory)
    for name in ['RustActivityAdapter.cpp', 'RustActivityAdapter.h']:
        (out / name).write_bytes((SOURCE / 'src/activities' / name).read_bytes())
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('void requestUpdate() {}', 'unsigned requested=0; void requestUpdate() {++requested;}')
    stub = stub.replace('return 20;', 'return fontHeight;').replace('  bool paintOk = true;', '  int fontHeight=20;\n  bool paintOk = true;')
    stub = stub.replace('  bool paintOk = true;', '''  int getTextWidth(int, const char*, int) const {return 24;}
  std::array<int,4> getClipRect() const {return {};}
  void setClipRect(int,int,int,int) {}
  void drawUiText(const XpUiCommand&,XpUiRect,const char*) {}
  bool paintOk = true;''')
    stub = '#include <array>\n#include <cassert>\n' + stub
    stub = stub.replace('using portMUX_TYPE = std::mutex;', 'using portMUX_TYPE = std::recursive_mutex;')
    stub = stub.replace('#define taskENTER_CRITICAL(m) (m)->lock()',
                        'inline thread_local unsigned criticalDepth=0;\n#define taskENTER_CRITICAL(m) ((m)->lock(), ++criticalDepth)')
    stub = stub.replace('#define taskEXIT_CRITICAL(m) (m)->unlock()',
                        '#define taskEXIT_CRITICAL(m) (--criticalDepth, (m)->unlock())')
    stub = stub.replace('uint8_t touchFlags = 0;', 'int16_t touchX=10, touchY=10; uint8_t touchFlags = 0;')
    (out / 'Activity.h').write_text(stub)
    (out / 'components').mkdir()
    touch = (ROOT / 'test/ui_runtime/stubs/components/UiAppHelpers.h').read_text()
    (out / 'components/UiAppHelpers.h').write_text(touch.replace('return {10, 10,', 'return {input.touchX, input.touchY,'))
    (out / 'gate.cpp').write_text(fixture)
    flags = rust_link_flags()
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), str(out/'gate.cpp'),
                    str(out/'RustActivityAdapter.cpp'), *flags, '-o', str(out/'gate')], check=True)
    subprocess.run([str(out/'gate'), 'first', 'builder', 'prepaint'], check=True)
    results = []
    for stage in ['first', 'open', 'setting']:
        for failure in ['builder', 'reserve', 'grow', 'paint']:
            for guarantee in ['button', 'touch', 'held-button', 'held-touch', 'retry', 'notification', 'busy', 'back', 'gesture-state', 'dispatch']:
                result = subprocess.run([str(out/'gate'), stage, failure, guarantee], capture_output=True, text=True, timeout=10)
                print(stage, failure, guarantee, 'PASS' if result.returncode == 0 else 'FAIL', result.stderr.strip(), flush=True)
                results.append(result.returncode)
    for variant in ['toolbar', 'contents', 'text', 'more', 'popup']:
        for recover in ['failed', 'recover']:
            result = subprocess.run([str(out/'gate'), variant, recover, 'cancel-mixed'], capture_output=True, text=True, timeout=10)
            print('cancel-mixed', variant, recover, 'PASS' if result.returncode == 0 else 'FAIL', result.stderr.strip(), flush=True)
            results.append(result.returncode)
    if any(results):
        raise SystemExit(1)
    if not revision:
        # Bypassing the final admission check must expose the dispatch race.
        mutant = fixture.replace('if (!o.consumeRejectedInput()) o.handleOverlayInput();', 'o.handleOverlayInput();')
        assert mutant != fixture
        (out / 'mutant.cpp').write_text(mutant)
        subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                        '-I'+str(ROOT/'test/ui_runtime/stubs'), str(out/'mutant.cpp'),
                        str(out/'RustActivityAdapter.cpp'), *flags, '-o', str(out/'mutant')], check=True)
        result = subprocess.run([str(out/'mutant'), 'open', 'grow', 'dispatch'], capture_output=True)
        assert result.returncode != 0
        print('pre-dispatch gate bypass mutant rejected')
