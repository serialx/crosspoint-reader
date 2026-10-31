"""Original reader orientation, auto-turn, text settings and retained picker results."""
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
    'void EpubReaderActivity::applyInitialOrientation(',
    'void EpubReaderActivity::applyOrientation(',
    'void EpubReaderActivity::toggleAutoPageTurn(',
    'void EpubReaderActivity::applyReaderTextSettings(',
    'void EpubReaderActivity::applyTextSettingLive(',
    'void EpubReaderActivity::showTextRowPopup(',
])
if 'void EpubReaderActivity::runSettingsPolicy(' in source:
    methods += '\n' + definition(source, 'void EpubReaderActivity::runSettingsPolicy(')
fixture = (HERE / 'reader_settings_oracle.cpp').read_text().replace('// SETTINGS_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    def run(text):
        (path / 'settings.cpp').write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                        str(path / 'settings.cpp'), *rust_link_flags(), '-o', str(path / 'settings')], check=True)
        return subprocess.check_output([str(path / 'settings')])
    actual = run(fixture)
    golden = HERE / 'reader-settings.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
        for before, after in [('return status;', 'return 0;'),
                              ('event("remember", lockDepth);', 'event("remember", 0);'),
                              ('return sizes;', 'return {};')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual
    else:
        assert actual == gzip.decompress(golden.read_bytes())
    print(f'reader settings: {len(actual.splitlines())} original trace lines')
