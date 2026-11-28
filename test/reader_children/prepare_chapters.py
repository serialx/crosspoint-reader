"""Compose the native chapter window with the production Rust cache and file leaves."""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "test/reader_children"))
from oracle_sources import native_text
sys.path.insert(0, str(HERE.parent / 'reader_lifecycle'))
from test_popup_resource import definition, native_leaf, replace_once

native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
package = (ROOT / 'lib/Epub/Epub/PackagePlatform.cpp').read_text()
source = (HERE.parent / 'reader_lifecycle/book_cache_fixture.h').read_text()
source = replace_once(source, '// ACTUAL_TEXT_HELPERS', '\n'.join(definition(native, sig) for sig in (
    'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)',
    'std::string owned(XpAppText value)')))
source = replace_once(source, '// ACTUAL_MUTEX_METHODS', '\n'.join(definition(package, sig) for sig in (
    'void BookMetadataCache::lockReadCursor()', 'void BookMetadataCache::unlockReadCursor()')))
source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join(
    'value.' + name + ' = ' + native_leaf(native, name) + ';' for name in (
        'file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size', 'cache_lock', 'cache_unlock')))
window = native_text('src/activities/reader/EpubReaderChapterSelectionActivity.cpp')
if len(sys.argv) > 2 and sys.argv[2] == 'guard':
    rows = (HERE / 'chapter_native.cpp.in').read_text().split('struct Epub {', 1)[0]
    rows = replace_once(rows, 'pod(bytes, 0, 2);', 'pod(bytes, 1, 2);')
    rows = replace_once(rows, 'bytes.resize(29 + 4 * kCount);', '''bytes.resize(29 + 4 * (kCount + 1));
  spineOffset = bytes.size();
  for (int i = 0; i < 4; ++i) bytes[29 + i] = static_cast<char>(bytes.size() >> (8 * i));
  field(bytes, "OPS/chapter0.xhtml"); pod(bytes, 100, 4); pod(bytes, 0, 2);''')
    rows = replace_once(rows, '29 + 4 * row + i', '33 + 4 * row + i')
    source += 'static size_t spineOffset;\n' + rows
    source += (HERE / 'chapter_guard.cpp.in').read_text()
else:
    source += (HERE / 'chapter_native.cpp.in').read_text().replace('@WINDOW@', definition(window,
        'void EpubReaderChapterSelectionActivity::refreshTocWindow('))
Path(sys.argv[1]).write_text(source)
