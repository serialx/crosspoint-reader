"""Compare production release/swipe routing with the original C++ list owner.

The unchanged UiListActivity is the pre-port oracle. Extract its methods and
both production snapshot/reader bridge methods so the test includes non-tap
release classification, published hit lookup, and the Rust C ABI.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


baseline = '\n'.join(method('src/activities/UiListActivity.cpp', signature) for signature in [
    'void UiListActivity::rowActionTrampoline(',
    'void UiListActivity::onRowAction(',
    'void UiListActivity::closeSwipeDelete()',
    'bool UiListActivity::handleSwipeDeleteInput()',
    'void UiListActivity::loop()',
])
snapshot = method('src/components/UiAppHelpers.h', 'inline freeink::ui::InputSnapshot touchSnapshotFrom(')
adapter = method('src/activities/RustActivityAdapter.cpp', 'void RustActivityAdapter::loop()')
adapter = adapter[adapter.index('  XpUiInput input{};'):adapter.index('  XpUiEffect effect{};')]
bridge = method('src/activities/reader/ReaderChromeListActivity.cpp', 'XpUiEffect ReaderChromeListActivity::updateRust(')
fixture = (Path(__file__).parent / 'bookmark_input.cpp').read_text()
fixture = fixture.replace('// SNAPSHOT', snapshot).replace('// BASELINE', baseline)
fixture = fixture.replace('// ADAPTER', adapter).replace('// BRIDGE', bridge)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'input.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-DFREEINK_CAP_TOUCH=1', '-DFREEINK_UI_COMPONENT_STYLE=0',
                    '-I' + str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
                    str(p / 'input.cpp'), str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'), *rust_link_flags(), '-o', str(p / 'input')], check=True)
    subprocess.run([str(p / 'input')], check=True)
