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
    def run(text, name, bounds=False):
        (p / (name + '.cpp')).write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(p / (name + '.cpp')),
                        *rust_link_flags(), '-o', str(p / name)], check=True)
        return subprocess.check_output([str(p / name), *(["--bounds"] if bounds else [])])
    if '--freeze-bounds' in sys.argv:
        original = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
        bounded = (HERE / 'overlay_view_oracle.cpp').read_text().replace('// VIEW_METHODS', definition(original, 'void EpubReaderActivity::renderOverlay()'))
        (HERE / 'overlay-view-bound.golden.gz').write_bytes(gzip.compress(run(bounded, 'original-bounds', True), mtime=0))
    actual = run(fixture, 'view')
    golden = HERE / 'overlay-view.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        expected = gzip.decompress(golden.read_bytes())
        if actual != expected:
            import difflib
            print(''.join(list(difflib.unified_diff(expected.decode().splitlines(True), actual.decode().splitlines(True)))[:80]))
        assert actual == expected
        wrong = fixture.replace('model.selectedIndex = view->selected_index;', 'model.selectedIndex = 0;')
        assert wrong != fixture
        assert run(wrong, 'wrong') != actual
        bounds = run(fixture, 'bounds', True)
        assert bounds == gzip.decompress((HERE / 'overlay-view-bound.golden.gz').read_bytes())
        labels = [line.split(b'] [')[1] for line in bounds.splitlines() if line.startswith(b'model ')]
        assert max(map(len, labels)) == 30
        assert all(len(label) + 1 <= 40 for label in labels)
    print(f'overlay view: {len(actual.splitlines())} frozen lines, 960 cases plus three missing-resource cases')
