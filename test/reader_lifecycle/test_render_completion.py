"""Exercise the production reader completion against model-locked transitions."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

source = Path(os.environ.get('XP_READER_GATE_SOURCE', ROOT))
render = definition((source / 'src/activities/reader/EpubReaderActivity.cpp').read_text(),
                    'void EpubReaderActivity::render(')
fixture = (HERE / 'render_completion.cpp').read_text().replace('// PARENT_RENDER', render)
with tempfile.TemporaryDirectory(prefix='reader-completion-') as directory:
    out = Path(directory)
    for name in ['RustActivityAdapter.cpp', 'RustActivityAdapter.h']:
        (out / name).write_bytes((source / 'src/activities' / name).read_bytes())
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    # ESP-IDF critical sections allow nesting on the same task/core.
    stub = stub.replace('using portMUX_TYPE = std::mutex;', 'using portMUX_TYPE = std::recursive_mutex;')
    stub = stub.replace('#define taskENTER_CRITICAL(m) (m)->lock()',
                        'inline thread_local unsigned criticalDepth=0;\n#define taskENTER_CRITICAL(m) ((m)->lock(), ++criticalDepth)')
    stub = stub.replace('#define taskEXIT_CRITICAL(m) (m)->unlock()',
                        'void afterModelUnlock();\n#define taskEXIT_CRITICAL(m) (--criticalDepth, (m)->unlock(), afterModelUnlock())')
    (out / 'Activity.h').write_text(stub)
    (out / 'completion.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), str(out/'completion.cpp'),
                    str(out/'RustActivityAdapter.cpp'), *rust_link_flags(), '-o', str(out/'completion')], check=True)
    subprocess.run([str(out/'completion')], check=True, timeout=10)
    split = fixture.replace('ModelLock modelLock(*this);\n  if (overlay == Overlay::None) publishRustScene({});',
                            'bool publish; { ModelLock modelLock(*this); publish = overlay == Overlay::None; }\n  if (publish) publishRustScene({});')
    assert split != fixture
    (out / 'split.cpp').write_text(split)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), str(out/'split.cpp'),
                    str(out/'RustActivityAdapter.cpp'), *rust_link_flags(), '-o', str(out/'split')], check=True)
    result = subprocess.run([str(out/'split')], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and 'overlay.value == Overlay::None' in result.stderr
    print('completion transitions pass; split decision/publication mutant rejected')
