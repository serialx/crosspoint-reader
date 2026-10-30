"""Freeze protected-open recovery and retained Wi-Fi/popup callbacks."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
freeze = '--freeze-original' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
methods = '\n'.join(definition(source, signature) for signature in [
    'bool EpubReaderActivity::handleLoadFailure(', 'void EpubReaderActivity::beginLoanTimeSync(',
    'void EpubReaderActivity::render(',
])
if 'bool EpubReaderActivity::runRecoveryPolicy(' in source:
    methods += '\n' + definition(source, 'bool EpubReaderActivity::runRecoveryPolicy(')
fixture = (HERE / 'recovery_oracle.cpp').read_text().replace('// RECOVERY_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    def run(text):
        (path / 'recovery.cpp').write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                        str(path / 'recovery.cpp'), *rust_link_flags(), '-o', str(path / 'recovery')], check=True)
        return subprocess.check_output([str(path / 'recovery')])
    actual = run(fixture)
    golden = HERE / 'recovery.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
        for before, after in [('result.isCancelled || WiFi.status()', 'false || WiFi.status()'),
                              ('trustedtime::syncNow(5000)', 'trustedtime::syncNow(4999)'),
                              ('APP_STATE.openEpubPath = bookPath;', 'APP_STATE.openEpubPath = "original.epub";')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual
    else:
        assert actual == gzip.decompress(golden.read_bytes())
    print(f'recovery: {len(actual.splitlines())} original trace lines')
