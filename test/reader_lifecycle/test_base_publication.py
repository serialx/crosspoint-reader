"""Run the production base publication helpers while a paint is blocked."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
sys.path.insert(0, str(HERE))
from extract import definition
source = (ROOT / 'src/activities/reader/ReaderActivity.cpp').read_text()
methods = ['void ReaderActivity::render(', 'uint32_t ReaderActivity::readBaseField(',
           'void ReaderActivity::writeBaseField(', 'ReaderSession ReaderActivity::snapshotReaderSession(',
           'void ReaderActivity::resetReaderSession(', 'void ReaderActivity::completeReaderSession(',
           'void ReaderActivity::queueForcedRefresh(', 'void ReaderActivity::setRefreshCounter(',
           'int32_t& ReaderActivity::renderRefreshCounter(', 'bool ReaderActivity::takeForcedRefresh(',
           'void ReaderActivity::markPageRendered(', 'bool ReaderActivity::acceptsRemoteInput(',
           'void ReaderActivity::notePageTurn(']
fixture = (HERE / 'base_publication.cpp').read_text().replace('// BASE_PUBLICATION_METHODS', '\n'.join(definition(source, m) for m in methods))
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    cpp = p / 'base.cpp'
    cpp.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', str(cpp), *rust_link_flags(), '-o', str(p / 'base')], check=True)
    subprocess.run([str(p / 'base')], check=True)
    nested = subprocess.run([str(p / 'base'), 'nested'], capture_output=True, text=True)
    assert nested.returncode != 0 and 'renderFrame' in nested.stderr
    (p / 'abi.c').write_text('''#include <xp_ui_reader.h>
void (*begin)(XpUiReaderBaseModel*, XpUiReaderBaseModel*) = xp_ui_reader_base_begin_render;
void (*publish)(XpUiReaderBaseModel*, const XpUiReaderBaseModel*) = xp_ui_reader_base_publish;
void (*force)(XpUiReaderBaseModel*) = xp_ui_reader_base_force_refresh;
''')
    subprocess.run(['cc', '-std=c11', '-Werror', '-fsyntax-only', str(p / 'abi.c'), '-I' + str(ROOT / 'rust/crates/xp-ffi/include')], check=True)
    header = (ROOT / 'rust/crates/xp-ffi/include/xp_ui_reader.h').read_text()
    rust = (ROOT / 'rust/crates/xp-ui/src/reader/base.rs').read_text()
    for kind in ['Field', 'Query', 'Command', 'Operation']:
        body = re.search(r'pub enum ' + kind + r' \{(.*?)\n\}', rust, re.S)[1]
        for name, value in re.findall(r'(\w+) = (\d+)', body):
            suffix = re.sub(r'(?<!^)(?=[A-Z])', '_', name).upper()
            key = 'XP_UI_READER_BASE_' + kind.upper() + '_' + suffix
            assert re.search(r'\b' + key + r' = ' + value + r'\b', header), key
    drift = fixture.replace('xp_ui_reader_base_publish', 'xp_ui_reader_base_publish_drift')
    cpp.write_text(drift)
    failed = subprocess.run(['c++', '-std=c++17', '-fsyntax-only', str(cpp), '-I' + str(ROOT / 'rust/crates/xp-ffi/include')], capture_output=True, text=True)
    assert failed.returncode != 0 and 'xp_ui_reader_base_publish_drift' in failed.stderr
    print('base publication: actual input helpers finish during busy paint; pending turn/manual/counter survive once; C ABI, enum drift, nested render and ASan/UBSan checks pass')
