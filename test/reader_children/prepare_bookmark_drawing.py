"""Compare frozen bookmark/list drawing with the Rust owner and physical bitmap adapter."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'reader_lifecycle'))
from oracle_sources import native_text
from extract import definition

output = Path(sys.argv[1])
base_output = output.with_name('bookmark_shared_drawing.cpp')
subprocess.run([sys.executable, str(HERE / 'prepare_percent_drawing.py'), str(base_output)], check=True)
base = base_output.read_text().split('int main() {', 1)[0]
base = base.replace('  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*);',
    '  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*);\n  void drawHelpText(const GfxRenderer&, Rect, const char*);')
keys = [('STR_BOOKMARKS', 'Bookmarks'), ('STR_NO_BOOKMARKS', 'No bookmarks'),
        ('STR_HOLD_OPEN_FOR_ACTIONS', 'Hold Open for actions'), ('STR_UNNAMED', 'Unnamed'),
        ('STR_BACK', 'Back'), ('STR_SELECT', 'Select'), ('STR_DIR_UP', 'Up'), ('STR_DIR_DOWN', 'Down')]
base = base.replace('STR_CONFIRM };', 'STR_CONFIRM, ' + ', '.join(k for k,v in keys) + ' };')
base = base.replace('"Side buttons:", "Confirm"};', '"Side buttons:", "Confirm", ' + ', '.join(json.dumps(v) for k,v in keys) + '};')
base = base.replace('      default: std::abort();', '\n'.join('      case StrId::' + k + ': return tr(' + k + ');' for k,v in keys if k not in ('STR_BACK','STR_SELECT')) + '\n      default: std::abort();', 1)
shared = native_text('src/activities/reader/ReaderChromeListActivity.cpp')
header = native_text('src/activities/reader/ReaderChromeListActivity.h')
bookmarks = native_text('src/activities/reader/EpubReaderBookmarksActivity.cpp')
book_header = native_text('src/activities/reader/EpubReaderBookmarksActivity.h')
row_capacity = re.search(r'XP_UI_READER_BOOKMARKS, (\d+)\)', bookmarks)[1]
command_start = re.search(r'kind == XP_UI_READER_BOOKMARKS \? (\d+)', shared)[1]
ledger = json.loads((ROOT/'test/bookmark_bytes/native_oracle/manifest.json').read_text())
record = next(r for r in ledger['snapshots'] if r['source'] == 'src/BookmarkEntry.h')
entry = (ROOT/'test/bookmark_bytes/native_oracle'/record['snapshot']).read_bytes()
assert len(entry) == record['bytes'] and hashlib.sha256(entry).hexdigest() == record['sha256']
setup = '\n'.join([
    definition(native_text('src/components/UiAppHelpers.h'), 'inline freeink::ui::BitmapRef listIconFor('),
    definition(native_text('src/components/themes/BaseTheme.cpp'), 'void BaseTheme::drawHelpText(').replace('BaseTheme::', 'Gui::'),
    definition(entry.decode(), 'struct BookmarkEntry {') + ';',
    definition(header, 'struct ReaderChromeRow {') + ';',
    definition(header, 'class ReaderChromeCommands {') + ';',
    definition(shared, 'bool ReaderChromeCommands::reserve('),
    definition(shared, 'bool ReaderChromeCommands::grow('),
])
methods = '\n'.join(definition(shared, signature) for signature in (
    'const char* ReaderChromeListActivity::text(', 'XpUiReaderRows ReaderChromeListActivity::rows(',
    'bool ReaderChromeListActivity::buildRustScene(', 'XpUiPaint ReaderChromeListActivity::rustResources('))
methods += '\n' + '\n'.join(definition(bookmarks, signature) for signature in (
    'void EpubReaderBookmarksActivity::rebuildBookmarkRowItems(',
    'const ReaderChromeRow* EpubReaderBookmarksActivity::rowAt(',
    'void EpubReaderBookmarksActivity::drawFooter('))
leaves = (ROOT/'src/activities/reader/RustEpubResources.cpp').read_text()
methods += '\nstatic auto ' + definition(leaves, '.child_list_help =').replace('.child_list_help =', 'candidateHelp =', 1) + ';\n'
icon = definition(leaves, '.child_list_icon =').replace('.child_list_icon =', 'candidateIcon =', 1)
methods += '\nstatic auto ' + icon + ';\n'
getters = '\n'.join(definition(book_header, signature) for signature in (
    'int listCount() const override', 'const char* headerTitle() const override',
    'const char* emptyText() const override', 'const char* helpText() const override'))
body = (HERE/'bookmark_drawing.cpp.in').read_text()
for token, value in [('@NATIVE_CLASSES@', setup), ('@METHODS@', methods), ('@GETTERS@', getters),
                     ('@ROW_CAPACITY@', row_capacity), ('@COMMAND_START@', command_start)]:
    body = body.replace(token, value)
output.write_text(base + body)
