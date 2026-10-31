"""Production text-screen/adapter routing and arena recovery with host leaves."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def method(source, signature):
    start = source.index(signature)
    pos = source.index('{', start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def run():
    source = (ROOT / 'src/activities/settings/TextSettingsActivity.cpp').read_text()
    if '--without-retry' in sys.argv:
        source = source.replace('!commands.ready() && !commands.reserve(32)', '!commands.ready()')
    if '--without-input-gate' in sys.argv:
        start = source.index('  if (sceneFailed) {', source.index('XpUiEffect TextSettingsActivity::updateRust('))
        end = source.index('  XpUiInput input = source;', start)
        source = source[:start] + source[end:]
    header = (ROOT / 'src/activities/reader/ReaderChromeListActivity.h').read_text()
    arena = (ROOT / 'src/activities/reader/ReaderChromeListActivity.cpp').read_text()
    start = header.index('class ReaderChromeCommands {')
    owner = header[start:header.index('\n};', start) + 3]
    methods = ''.join(method(source, signature) for signature in (
        'XpUiEffect TextSettingsActivity::updateRust(',
        'void TextSettingsActivity::applyRustEffect(',
        'bool TextSettingsActivity::buildRustScene(',
        'void TextSettingsActivity::rustPublished(',
        'void TextSettingsActivity::onExit(',
    ))
    enter = method(source, 'void TextSettingsActivity::onEnter(')
    methods += 'void TextSettingsActivity::enterArena() {\n' + enter[enter.index('  if (!commands.reserve(32))'):]
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        for name in ('RustActivityAdapter.h', 'RustActivityAdapter.cpp'):
            (work / name).write_text((ROOT / 'src/activities' / name).read_text())
        stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
        stub = stub.replace('uint8_t touchFlags = 0;', 'uint8_t touchFlags = 0; int16_t touchX=10, touchY=10;')
        stub = stub.replace('void displayBuffer(HalDisplay::RefreshMode) {', 'unsigned panels=0; HalDisplay::RefreshMode lastMode=HalDisplay::FULL_REFRESH; void displayBuffer(HalDisplay::RefreshMode mode) { lastMode=mode; ++panels;')
        stub = stub.replace('void requestUpdate() {}', 'unsigned requests=0; bool finished=false; void requestUpdate() { ++requests; } void finish() { finished=true; } virtual void onExit() {}')
        (work / 'Activity.h').write_text(stub)
        (work / 'components').mkdir()
        helpers = (ROOT / 'test/ui_runtime/stubs/components/UiAppHelpers.h').read_text()
        helpers = helpers.replace('return {10, 10,', 'return {input.touchX, input.touchY,')
        helpers = helpers.replace('inline void touchAction(bool) {}', 'inline unsigned count=0; inline void touchAction(bool=true) { ++count; }')
        (work / 'components/UiAppHelpers.h').write_text(helpers)
        code = (HERE / 'Routing.cpp').read_text().replace('// ARENA_OWNER', owner)
        code = code.replace('// ARENA_METHODS', method(arena, 'bool ReaderChromeCommands::reserve(') + method(arena, 'bool ReaderChromeCommands::grow('))
        code = code.replace('// PRODUCTION_METHODS', methods)
        (work / 'routing.cpp').write_text(code)
        subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I'+str(work),
                        '-I'+str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
                        str(work / 'routing.cpp'), str(work / 'RustActivityAdapter.cpp'),
                        str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'),
                        *rust_link_flags(), '-o', str(work / 'routing')], check=True)
        subprocess.run([str(work / 'routing')], check=True)


if __name__ == '__main__':
    run()
