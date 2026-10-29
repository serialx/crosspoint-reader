"""Freeze A4 counter assignment and resource ordering when reader chrome closes."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
sys.path.insert(0, str(HERE))
from extract import definition
freeze = '--freeze-original' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
method = definition(source, 'void EpubReaderActivity::closeOverlayToPage()')
fixture = (HERE / 'close_overlay_oracle.cpp').read_text().replace('// CLOSE_OVERLAY_METHOD', method)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'close.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(p / 'close.cpp'), *rust_link_flags(), '-o', str(p / 'close')], check=True)
    actual = subprocess.check_output([str(p / 'close')])
    golden = HERE / 'close-overlay.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        # Moving A4 cleanup until after the snapshot path changes the observed resource boundary.
        mutated = fixture.replace('if (BoardConfig::isEegoA4()) setRefreshCounter(1);', 'if (false) setRefreshCounter(1);')
        assert mutated != fixture
        (p / 'wrong.cpp').write_text(mutated)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(p / 'wrong.cpp'), *rust_link_flags(), '-o', str(p / 'wrong')], check=True)
        assert subprocess.check_output([str(p / 'wrong')]) != actual
    print(f'close overlay: {len(actual.splitlines())} trace lines, 48 cases; A4 counter and resource ordering preserved')
