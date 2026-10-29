"""Freeze chapter/progress/offset/link navigation with 32-bit target size arithmetic."""
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
names = ['void jumpToPercent(', 'void applyProgressChange(', 'bool applyDeferredReposition(',
         'void clearDeferredReposition(', 'bool saveProgress(', 'void rememberCurrentContentOffset(',
         'void navigateToHref(', 'void restoreSavedPosition(']
methods = '\n'.join(definition(source, name.replace(' ', ' EpubReaderActivity::', 1)) for name in names)
if not freeze:
    methods += '\n' + definition(source, 'bool EpubReaderActivity::runNavigationPolicy(')
fixture = (HERE / 'navigation_oracle.cpp').read_text().replace('// NAVIGATION_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    def run(text, name):
        (p / (name + '.cpp')).write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(p / (name + '.cpp')),
                        *rust_link_flags(), '-o', str(p / name)], check=True)
        return subprocess.check_output([str(p / name)])
    actual = run(fixture, 'navigation')
    golden = HERE / 'navigation.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        for before, after in [('return o.epub->getBookSize();', 'return 0;'),
                              ('o.pendingAnchor = std::move(anchor);', 'o.pendingAnchor.clear();'),
                              ('o.section->currentPage = a;', 'o.section->currentPage = a + 1;')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture
            assert run(wrong, 'wrong') != actual
    print(f'navigation: {len(actual.splitlines())} frozen lines, 1882 cases; lock depth returns to zero')
