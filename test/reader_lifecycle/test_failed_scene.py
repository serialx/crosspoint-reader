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
loop = (loop[:loop.index('  if (tryIdleLoop())')] if not revision else 'void EpubReaderActivity::loop() {\n') + '  handleOverlayInput();\n}'
# This is the actual render-to-publication boundary, with its model already prepared.
if revision:
    publication = parent[parent.index('    toolbarUi->setModel(model);'):]
    publication = publication[:publication.index('    return;')].replace('toolbarUi->', 'o.toolbarUi->')
else:
    publication = parent[parent.index('        o.toolbarUi->setModel(model);'):]
    publication = publication[:publication.index('      }};')]
cancel = ''
if 'void EpubReaderActivity::cancelRejectedInput(' in parent:
    cancel = definition(parent, 'void EpubReaderActivity::cancelRejectedInput(')
fixture = (HERE / 'failed_scene.cpp').read_text()
fixture = fixture.replace('// COMMAND_CLASS', commands).replace('// TOOLBAR_CLASS', header)
fixture = fixture.replace('// TOOLBAR_METHODS', reserve + '\n' + methods)
fixture = fixture.replace('// LOOP_METHOD', loop).replace('// PUBLICATION', publication)
if revision:
    # The 8d534a99 parent uses the SDK ListNav; this fixture only resets it.
    fixture = fixture.replace('a.toolbarUi->resetNavigation(0,0)', 'a.toolbarUi->nav().reset()')
fixture = fixture.replace('// CANCEL_METHOD', cancel)
fixture = fixture.replace('// CANCEL_DECL', 'void cancelRejectedInput(const XpUiInput&) override;' if cancel else '')
with tempfile.TemporaryDirectory(prefix='reader-failed-scene-') as directory:
    out = Path(directory)
    for name in ['RustActivityAdapter.cpp', 'RustActivityAdapter.h']:
        (out / name).write_bytes((SOURCE / 'src/activities' / name).read_bytes())
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('#define LOG_ERR(...) ((void)0)', '#define LOG_ERR(...) ((void)0)')
    stub = stub.replace('void requestUpdate() {}', 'unsigned requested=0; void requestUpdate() {++requested;}')
    stub = stub.replace('  bool paintOk = true;', '''  int getTextWidth(int, const char*, int) const {return 24;}
  std::array<int,4> getClipRect() const {return {};}
  void setClipRect(int,int,int,int) {}
  void drawUiText(const XpUiCommand&,XpUiRect,const char*) {}
  bool paintOk = true;''')
    stub = '#include <array>\n' + stub
    (out / 'Activity.h').write_text(stub)
    (out / 'gate.cpp').write_text(fixture)
    flags = rust_link_flags()
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), str(out/'gate.cpp'),
                    str(out/'RustActivityAdapter.cpp'), *flags, '-o', str(out/'gate')], check=True)
    results = []
    for stage in ['first', 'open', 'setting']:
        for failure in ['builder', 'reserve', 'grow', 'paint']:
            for guarantee in ['button', 'touch', 'held-button', 'held-touch', 'retry', 'notification', 'busy', 'back']:
                result = subprocess.run([str(out/'gate'), stage, failure, guarantee], capture_output=True, text=True, timeout=10)
                print(stage, failure, guarantee, 'PASS' if result.returncode == 0 else 'FAIL', result.stderr.strip(), flush=True)
                results.append(result.returncode)
    if any(results):
        raise SystemExit(1)
