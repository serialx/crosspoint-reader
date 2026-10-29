"""Freeze original reader status, chapter titles and toolbar row values."""
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
signatures = ['void EpubReaderActivity::renderStatusBar(', 'bool EpubReaderActivity::usesToolbarMenu(',
              'std::string EpubReaderActivity::currentChapterTitle(', 'std::string EpubReaderActivity::textRowName(',
              'std::string EpubReaderActivity::textRowValue(', 'std::string EpubReaderActivity::moreRowName(',
              'std::string EpubReaderActivity::moreRowValue(']
methods = '\n'.join(definition(source, signature) for signature in signatures)
fixture = (HERE/'view_values_oracle.cpp').read_text().replace('// VALUE_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    def run(text):
        (path/'view.cpp').write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(path/'view.cpp'),
                        *rust_link_flags(), '-o', str(path/'view')], check=True)
        return subprocess.check_output([str(path/'view')])
    actual = run(fixture)
    golden = HERE/'view-values.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        for before, after in [('section->currentPage + 1', 'section->currentPage'),
                              ('note == bleturner::LinkNote::LowMemory', 'false'),
                              ('SETTINGS.fontPointSize) + " pt"', 'SETTINGS.fontPointSize) + " px"')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual
    print(f'view values: {len(actual.splitlines())} frozen lines across 960 status/title and 14 row-setting cases')
