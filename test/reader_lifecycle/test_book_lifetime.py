"""Freeze book loading, progress restore, extractor lifetime and reader teardown."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT/'test'))
from rust_host import rust_link_flags
freeze = '--freeze-original' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT/'src/activities/reader/EpubReaderActivity.cpp').read_text())
methods = '\n'.join(definition(source, s) for s in ['bool EpubReaderActivity::loadBook(', 'EpubReaderActivity::~EpubReaderActivity('])
fixture = (HERE/'book_lifetime_oracle.cpp').read_text().replace('// LIFETIME_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    def run(text):
        (path/'book.cpp').write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(path/'book.cpp'),
                        *rust_link_flags(), '-o', str(path/'book')], check=True)
        return subprocess.check_output([str(path/'book')])
    actual = run(fixture)
    golden = HERE/'book-lifetime.golden.gz'
    if freeze: golden.write_bytes(gzip.compress(actual, mtime=0))
    else:
        assert actual == gzip.decompress(golden.read_bytes())
        for before, after in [('if (uncached) loan.emplace(renderer);', 'if (!uncached) loan.emplace(renderer);'),
                              ('if (currentSpineIndex == 0)', 'if (currentSpineIndex == 1)'),
                              ('if (footnoteDepth > 0 && epub)', 'if (footnoteDepth > 1 && !epub)')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual
    print(f'book lifetime: {len(actual.splitlines())} frozen lines across 576 load and 32 teardown cases')
