"""Compare the production toolbar parent against input traces frozen from 8d534a99."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
freeze = '--freeze-original' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
nav = (ROOT / 'src/util/ButtonNavigator.cpp').read_text()
fixture = (HERE / 'overlay_oracle.cpp').read_text().replace('// OVERLAY_METHODS', definition(source, 'void EpubReaderActivity::handleOverlayInput()'))
fixture = fixture.replace('// NAV_METHODS', '\n'.join(definition(nav, 'int ButtonNavigator::' + name) for name in ['nextIndex(', 'previousIndex(']))
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'overlay.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(p / 'overlay.cpp'), '-o', str(p / 'overlay')], check=True)
    actual = subprocess.check_output([str(p / 'overlay')])
    golden = HERE / 'overlay-input.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        wrong = fixture.replace('focusedTool = (focusedTool + 2) % 3;', 'focusedTool = (focusedTool + 1) % 3;')
        assert wrong != fixture
        (p / 'wrong.cpp').write_text(wrong)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(p / 'wrong.cpp'), '-o', str(p / 'wrong')], check=True)
        assert subprocess.check_output([str(p / 'wrong')]) != actual
    print(f'overlay input: {len(actual.splitlines())} frozen trace lines, 1728 cases; zero lock depth on every path')
