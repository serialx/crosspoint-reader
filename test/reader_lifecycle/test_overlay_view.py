"""Compare the reader overlay model and rows to original C++ view output."""
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
fixture = (HERE / 'overlay_view_oracle.cpp').read_text().replace('// VIEW_METHODS', definition(source, 'void EpubReaderActivity::renderOverlay()'))
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    def run(text, name):
        (p / (name + '.cpp')).write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(p / (name + '.cpp')),
                        *rust_link_flags(), '-o', str(p / name)], check=True)
        return subprocess.check_output([str(p / name)])
    actual = run(fixture, 'view')
    golden = HERE / 'overlay-view.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        wrong = fixture.replace('model.selectedIndex = panelCursorShown ? panelIndex : -1;', 'model.selectedIndex = panelIndex;')
        assert wrong != fixture
        assert run(wrong, 'wrong') != actual
    print(f'overlay view: {len(actual.splitlines())} frozen lines, 960 cases plus three missing-resource cases')
