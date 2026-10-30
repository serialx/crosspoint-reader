"""Original bookmark matching/persistence and consumed link-stack composition."""
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
methods = '\n'.join(definition(source, signature) for signature in [
    'void EpubReaderActivity::loadCachedBookmarks(', 'void EpubReaderActivity::addBookmark(',
    'void EpubReaderActivity::updateBookmarkFlag(', 'void EpubReaderActivity::saveLinkStack(',
    'void EpubReaderActivity::loadLinkStack(',
])
if 'void EpubReaderActivity::runBookmarkPolicy(' in source:
    methods = definition(source, 'struct ReaderBookmarkResources') + ';\n' + methods
    methods += '\n' + definition(source, 'void EpubReaderActivity::runBookmarkPolicy(')
    methods += '\n' + definition(source, 'void EpubReaderActivity::runLinkPersistence(')
match = '\n'.join(definition(source, signature) for signature in ['ProgressRange getPageProgressRange(', 'bool bookmarkMatchesProgress('])
fixture = (HERE / 'bookmark_parent_oracle.cpp').read_text().replace('// BOOKMARK_METHODS', methods).replace('// MATCH_METHODS', match)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    def run(text):
        (path / 'bookmark.cpp').write_text(text)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                        '-I'+str(ROOT/'src'), '-I'+str(HERE/'stubs'),
                        *(['-DBOOKMARK_RELEASE_LOG'] if release else []), str(path / 'bookmark.cpp'),
                        *rust_link_flags(), '-o', str(path / 'bookmark')], check=True)
        return subprocess.check_output([str(path / 'bookmark')])
    actual = run(fixture)
    golden = HERE / f'bookmark-parent{"-release" if release else ""}.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual, mtime=0))
        for before, after in [('bookmarkProgressEpsilon = 0.0001f', 'bookmarkProgressEpsilon = 0.0f'),
                              ('currentPageVisibleOffset.has_value()', 'false'),
                              ('Storage.remove(path.c_str());', '(void)path;')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual
    else:
        assert actual == gzip.decompress(golden.read_bytes())
    print(f'bookmark parents release={release}: {len(actual.splitlines())} original trace lines')
