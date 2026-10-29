"""Freeze overlay opening state, framebuffer loans and lock ordering."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
from overlay_binding import binding
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
freeze = '--freeze-original' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
fixture = (HERE / 'overlay_open_oracle.cpp').read_text().replace('// OPEN_METHODS', definition(source, 'void EpubReaderActivity::openOverlay('))
if not freeze:
    fixture = '#include <optional>\n' + fixture
    fixture = fixture.replace('// OPEN_METHODS', '')
    fixture = fixture.replace('  void openOverlay(Overlay target);', '  void openOverlay(Overlay target);\n  void runOverlayPolicy(uint8_t);')
    fixture += '\n' + binding(source, True)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    def run(text, name):
        (p / (name + '.cpp')).write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(p / (name + '.cpp')),
                        *rust_link_flags(), '-o', str(p / name)], check=True)
        return subprocess.check_output([str(p / name)])
    actual = run(fixture, 'open')
    golden = HERE / 'overlay-open.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        wrong = fixture.replace('return o.pageBufferStale;', 'return false;')
        assert wrong != fixture
        assert run(wrong, 'wrong') != actual
    print(f'overlay open: {len(actual.splitlines())} frozen lines, 1600 cases; lock depth returns to zero')
