"""Record the original selection geometry and dictionary viewport on the host."""
from pathlib import Path
import gzip
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def record(root=ROOT, mutation=False):
    clip = (root / 'src/activities/reader/ClipSelectionActivity.cpp').read_text()
    definition = (root / 'src/activities/reader/DictionaryDefinitionActivity.cpp').read_text()
    geometry = (root / 'src/clippings/SelectionGeometry.h').read_text()
    geometry = geometry[geometry.index('namespace selectionGeometry'):]
    functions = '\n'.join(method(clip, sig) for sig in [
        'int ClipSelectionActivity::closestInRow', 'int ClipSelectionActivity::wordAt',
        'int ClipSelectionActivity::dragWordAt', 'bool ClipSelectionActivity::selectionContains',
        'int ClipSelectionActivity::nextPageStartIndexForTouchDrag',
        'bool ClipSelectionActivity::isWithinCurrentPageEndDwellSlop',
        'void ClipSelectionActivity::moveVertical', 'void ClipSelectionActivity::selectIndex',
        'void ClipSelectionActivity::moveToPage', 'Rect ClipSelectionActivity::handleRect',
        'int ClipSelectionActivity::selectionTop', 'int ClipSelectionActivity::actionAt',
        'Rect ClipSelectionActivity::actionRect', 'int ClipSelectionActivity::textOffset',
        'int ClipSelectionActivity::textXOffset'])
    functions += '\n' + method(definition, 'DictionaryDefinitionActivity::BodyArea DictionaryDefinitionActivity::bodyArea')
    if mutation:
        functions = functions.replace('constexpr int SLOP = 4;', 'constexpr int SLOP = 3;')
    with tempfile.TemporaryDirectory() as temporary:
        cpp = Path(temporary) / 'oracle.cpp'
        binary = Path(temporary) / 'oracle'
        harness = (Path(__file__).parent / 'oracle.cpp').read_text()
        cpp.write_text(harness.replace('// GEOMETRY', geometry).replace('// METHODS', functions))
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
        return subprocess.check_output([str(binary)])


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    data = record(args.source)
    if args.record:
        args.record.write_bytes(data)
    else:
        assert data == gzip.decompress((Path(__file__).parent / 'geometry.golden.gz').read_bytes())
        assert record(args.source, True) != data, 'slop mutation was not detected'
        print('Dictionary/selection geometry matches; slop mutation rejected')
