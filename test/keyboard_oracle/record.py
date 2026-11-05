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


def baseline(path, root=ROOT):
    if pathlib.Path(root).resolve() != ROOT:
        return (pathlib.Path(root) / path).read_text()
    return subprocess.check_output(['git', '-C', str(ROOT), 'show', 'ba6860d8:' + path], text=True)


def source(root):
    activity = baseline('src/activities/util/KeyboardEntryActivity.cpp', root)
    # Keep the production tables, navigation, edits, holds, masking and results.
    # Rendering and text-point placement have separate pixel/placement oracles.
    start = activity.index('int KeyboardEntryActivity::measureRange')
    end = activity.index('void KeyboardEntryActivity::loop()')
    activity = activity[:start] + activity[end:]
    start = activity.index('void KeyboardEntryActivity::render(')
    end = activity.index('void KeyboardEntryActivity::onComplete(')
    activity = activity[:start] + activity[end:]
    header = strip_includes(baseline('src/activities/util/KeyboardEntryActivity.h', root))
    header = header.replace(' private:', ' public:')
    layout = strip_includes(baseline('src/activities/util/KeyboardLayoutSet.h', root))
    nav = strip_includes(baseline('src/util/ButtonNavigator.h', root))
    return ((HERE / 'leaves.h').read_text() + '\n' + nav + '\n' + header + '\n' + layout + '\n' +
            strip_includes(baseline('src/activities/util/KeyboardLayoutSet.cpp', root)) + '\n' +
            strip_includes(baseline('src/util/ButtonNavigator.cpp', root)) + '\n' +
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
