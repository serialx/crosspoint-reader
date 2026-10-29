"""Compile the original keyboard model with real SDK layouts and host input leaves."""
import argparse
import pathlib
import re
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SDK = ROOT / 'freeink-sdk/libs/ui/FreeInkUI'


def strip_includes(text):
    return re.sub(r'^#(?:include|pragma).*\n', '', text, flags=re.M)


def source(root):
    activity = (root / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()
    # Keep the production tables, navigation, edits, holds, masking and results.
    # Rendering and text-point placement have separate pixel/placement oracles.
    start = activity.index('int KeyboardEntryActivity::measureRange')
    end = activity.index('void KeyboardEntryActivity::loop()')
    activity = activity[:start] + activity[end:]
    start = activity.index('void KeyboardEntryActivity::render(')
    end = activity.index('void KeyboardEntryActivity::onComplete(')
    activity = activity[:start] + activity[end:]
    header = strip_includes((root / 'src/activities/util/KeyboardEntryActivity.h').read_text())
    header = header.replace(' private:', ' public:')
    layout = strip_includes((root / 'src/activities/util/KeyboardLayoutSet.h').read_text())
    nav = strip_includes((root / 'src/util/ButtonNavigator.h').read_text())
    return ((HERE / 'leaves.h').read_text() + '\n' + nav + '\n' + header + '\n' + layout + '\n' +
            strip_includes((root / 'src/activities/util/KeyboardLayoutSet.cpp').read_text()) + '\n' +
            strip_includes((root / 'src/util/ButtonNavigator.cpp').read_text()) + '\n' +
            strip_includes(activity) + '\n' + (HERE / 'transcript.cpp').read_text())


def record(root=ROOT, mutation=False):
    cpp = source(pathlib.Path(root))
    if mutation:
        assert 'cursorPos += n;' in cpp
        cpp = cpp.replace('cursorPos += n;', 'cursorPos += 0;')
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp)
        (path / 'oracle.cpp').write_text(cpp)
        subprocess.run(['c++', '-std=c++20', '-O1', '-DFREEINK_UI_COMPONENT_STYLE=0',
                        '-I' + str(SDK / 'include'), '-I' + str(ROOT / 'lib/I18n'),
                        str(path / 'oracle.cpp'), str(SDK / 'src/FreeInkUI.cpp'),
                        '-o', str(path / 'oracle')], check=True)
        return subprocess.check_output([str(path / 'oracle')])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=pathlib.Path, default=ROOT)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.out.write_bytes(record(args.root))
