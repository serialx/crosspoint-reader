"""Freeze selection span pixels from the original C++ painting methods."""
from pathlib import Path
import gzip
import subprocess
import tempfile
from oracle import ROOT, method

HERE = Path(__file__).parent

def record(root=ROOT):
    source = (root / 'src/activities/reader/ClipSelectionActivity.cpp').read_text()
    names = ['void ClipSelectionActivity::prewarmWord', 'void ClipSelectionActivity::ditherGapBetween',
             'void ClipSelectionActivity::clearGapBetween', 'void ClipSelectionActivity::drawWordClean',
             'void ClipSelectionActivity::drawWordHighlight', 'bool ClipSelectionActivity::renderIncremental',
             'void ClipSelectionActivity::drawSelection']
    methods = '\n'.join(method(source, name) for name in names)
    with tempfile.TemporaryDirectory() as temporary:
        cpp = Path(temporary) / 'paint.cpp'
        binary = Path(temporary) / 'paint'
        cpp.write_text((HERE / 'paint_oracle.cpp').read_text().replace('// METHODS', methods))
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
        return subprocess.check_output([str(binary)])

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    command = ['cargo', 'run', '--quiet', '--manifest-path', str(ROOT / 'rust/Cargo.toml'),
               '-p', 'xp-ui', '--example', 'dictionary_paint']
    data = record(args.source) if args.source else subprocess.check_output(command)
    if not args.source:
        assert data == subprocess.check_output(command + ['--', '16']), 'full/strip mismatch'
    golden = HERE / 'paint.golden.gz'
    if args.record:
        golden.write_bytes(gzip.compress(data, mtime=0))
    else:
        assert data == gzip.decompress(golden.read_bytes())
        print('Selection full/incremental pixels match frozen C++')
