"""Compose the frozen clipping rows and native list drawing with the owned Rust child."""
from pathlib import Path
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
base_output = output.with_name('clipping_shared_drawing.cpp')
subprocess.run([sys.executable, str(HERE / 'prepare_percent_drawing.py'), str(base_output)], check=True)
base = base_output.read_text().split('int main() {', 1)[0]
base = base.replace('  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*);',
    '  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*);\n  void drawHelpText(const GfxRenderer&, Rect, const char*);')
base = base.replace('STR_CONFIRM };', 'STR_CONFIRM, STR_CLIPPINGS, STR_NO_CLIPPINGS, STR_HOLD_SELECT_TO_DELETE, STR_BACK, STR_SELECT, STR_DIR_UP, STR_DIR_DOWN, STR_LOADING_POPUP };')
base = base.replace('"Side buttons:", "Confirm"};', '"Side buttons:", "Confirm", "Clippings", "No clippings", "Hold Select to delete", "Back", "Select", "Up", "Down", "Loading..."};')
base = base.replace('      default: std::abort();', '''      case StrId::STR_CLIPPINGS: return tr(STR_CLIPPINGS);
      case StrId::STR_NO_CLIPPINGS: return tr(STR_NO_CLIPPINGS);
      case StrId::STR_HOLD_SELECT_TO_DELETE: return tr(STR_HOLD_SELECT_TO_DELETE);
      case StrId::STR_DIR_UP: return tr(STR_DIR_UP);
      case StrId::STR_DIR_DOWN: return tr(STR_DIR_DOWN);
      case StrId::STR_LOADING_POPUP: return tr(STR_LOADING_POPUP);
      default: std::abort();''', 1)
shared = native_text('src/activities/reader/ReaderChromeListActivity.cpp')
header = native_text('src/activities/reader/ReaderChromeListActivity.h')
clipping = native_text('src/activities/reader/EpubReaderClippingListActivity.cpp')
leaves = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
definitions = [
    definition(native_text('src/components/UiAppHelpers.h'), 'inline freeink::ui::BitmapRef listIconFor('),
    definition(native_text('src/components/themes/BaseTheme.cpp'), 'void BaseTheme::drawHelpText(').replace('BaseTheme::', 'Gui::'),
    definition(header, 'struct ReaderChromeRow {') + ';',
    definition(header, 'class ReaderChromeCommands {') + ';',
    definition(shared, 'bool ReaderChromeCommands::reserve('),
    definition(shared, 'bool ReaderChromeCommands::grow('),
]
body = (HERE / 'clipping_drawing.cpp.in').read_text()
window = re.search(r'ROW_WINDOW = (\d+);', native_text('src/activities/reader/EpubReaderClippingListActivity.h'))[1]
body = body.replace('@ROW_WINDOW@', window)
for signature in ('const char* ReaderChromeListActivity::text(', 'XpUiReaderRows ReaderChromeListActivity::rows(',
                  'bool ReaderChromeListActivity::buildRustScene(', 'XpUiPaint ReaderChromeListActivity::rustResources(',
                  'void ReaderChromeListActivity::drawFooter('):
    definitions.append(definition(shared, signature))
for signature in ('int EpubReaderClippingListActivity::listCount(', 'const char* EpubReaderClippingListActivity::headerTitle(',
                  'void EpubReaderClippingListActivity::refreshRowWindow(', 'const ReaderChromeRow* EpubReaderClippingListActivity::rowAt('):
    definitions.append(definition(clipping, signature))
# Definitions needing the fixture class declarations follow @NATIVE_CLASSES@.
setup = '\n'.join(definitions[:6])
methods = '\n'.join(definitions[6:])
for field, name in [('child_list_help', 'candidateHelp'), ('child_list_bookmark_icon', 'candidateIcon')]:
    methods += '\nstatic auto ' + definition(leaves, '.' + field + ' =').replace('.' + field + ' =', name + ' =', 1) + ';\n'
output.write_text(base + body.replace('@NATIVE_CLASSES@', setup).replace('@METHODS@', methods))
