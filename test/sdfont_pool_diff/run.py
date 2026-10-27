"""Compare cache operation traces with the frozen da609bbf C++ implementation."""
import argparse
import gzip
import hashlib
import pathlib
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=pathlib.Path, required=True)
parser.add_argument('--font', type=pathlib.Path, action='append', default=[])
parser.add_argument('--private-font-dir', type=pathlib.Path)
parser.add_argument('--texts', type=pathlib.Path)
parser.add_argument('--save', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='sdpool-') as temp:
    paths = []
    for source in sorted((HERE.parent / 'text_golden/fonts').glob('*.cpfont.gz')):
        dest = pathlib.Path(temp) / source.stem
        dest.write_bytes(gzip.decompress(source.read_bytes()))
        paths.append(dest)
    paths.extend(args.font)
    if args.private_font_dir:
        paths.extend(sorted(args.private_font_dir.glob('*.cpfont')))
    for index, path in enumerate(paths):
        outputs = []
        for variant in ('reference', 'candidate'):
            command = [str(args.build_dir.resolve() / f'sdpool_{variant}'), str(path)]
            if args.texts:
                command.append(str(args.texts))
            outputs.append(subprocess.check_output(command))
        if outputs[0] != outputs[1]:
            lines = [output.splitlines() for output in outputs]
            first = next((i for i, pair in enumerate(zip(*lines)) if pair[0] != pair[1]), min(map(len, lines)))
            raise SystemExit(f'{path.name}: mismatch at line {first}: {lines[0][first:first+1]} / {lines[1][first:first+1]}')
        if args.save:
            args.save.mkdir(parents=True, exist_ok=True)
            (args.save / f'{index:02d}.trace.gz').write_bytes(gzip.compress(outputs[0], mtime=0))
        print(f'{path.name}: {len(outputs[0].splitlines())} identical records sha256={hashlib.sha256(outputs[0]).hexdigest()}', flush=True)
