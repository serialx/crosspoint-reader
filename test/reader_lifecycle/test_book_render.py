"""Freeze section build, page restore and reader post-render resource ordering."""
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
release = '--release-log' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
fixture = (HERE / 'book_render_oracle.cpp').read_text().replace('// BOOK_METHOD', definition(source, 'void EpubReaderActivity::renderBook()'))
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    def run(text, name):
        (p/(name+'.cpp')).write_text(text)
        subprocess.run(['c++','-std=c++17','-UNDEBUG','-Wno-format','-fsanitize=address,undefined',str(p/(name+'.cpp')),
                        *(['-DBOOK_RELEASE_LOG'] if release else []),*rust_link_flags(),'-o',str(p/name)],check=True)
        return subprocess.check_output([str(p/name)],timeout=20)
    actual=run(fixture,'book')
    golden=HERE/('book-render-release.golden.gz' if release else 'book-render.golden.gz')
    if freeze: golden.write_bytes(gzip.compress(actual,mtime=0))
    else:
        assert actual==gzip.decompress(golden.read_bytes())
        for before,after in [('o.pageLoadRetryCount = static_cast<decltype(o.pageLoadRetryCount)>(value);','o.pageLoadRetryCount = 0;'),
                             ('o.buildPopupPending = static_cast<decltype(o.buildPopupPending)>(value);','o.buildPopupPending = false;'),
                             ('o.pendingScreenshot = static_cast<decltype(o.pendingScreenshot)>(value);','o.pendingScreenshot = true;')]:
            wrong=fixture.replace(before,after)
            assert wrong!=fixture
            assert run(wrong,'wrong')!=actual
    print(f'book render ({"release" if release else "debug"}): {len(actual.splitlines())} frozen lines across 224 build/load/notice cases')
