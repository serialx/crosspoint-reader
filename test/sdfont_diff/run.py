"""Compare the frozen 9738db01 loader and registry with the production adapters."""
import argparse
import gzip
import hashlib
import pathlib
import struct
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=pathlib.Path, required=True)
parser.add_argument('--private-font-dir', type=pathlib.Path)
args = parser.parse_args()


def compare(kind, paths):
    outputs = [subprocess.check_output([str(args.build_dir / f'{kind}_{v}'), *map(str, paths)])
               for v in ('oracle', 'candidate')]
    if outputs[0] != outputs[1]:
        a, b = [x.splitlines() for x in outputs]
        mismatch = next((i for i, pair in enumerate(zip(a, b)) if pair[0] != pair[1]), min(len(a), len(b)))
        raise SystemExit(f'{kind}: mismatch at record {mismatch}: {a[mismatch:mismatch+1]} / {b[mismatch:mismatch+1]}')
    print(f'{kind}: {len(outputs[0].splitlines())} identical records; sha256={hashlib.sha256(outputs[0]).hexdigest()}')
    if kind == 'sdalloc':
        print(outputs[0].decode(), end='')


with tempfile.TemporaryDirectory(prefix='sdfont-diff-') as directory:
    root = pathlib.Path(directory)
    paths = []
    for source in sorted((HERE.parent / 'text_golden/fonts').glob('*.cpfont.gz')):
        dest = root / source.stem
        dest.write_bytes(gzip.decompress(source.read_bytes()))
        paths.append(dest)
    # Small fixtures exercise every-byte truncation, above-BMP and wrapping interval spans.
    for first, last, count in [(65, 66, 2), (0x10000, 0x10001, 2), (0, 0xffffffff, 0), (1, 0, 1)]:
        data = bytearray(108)
        data[:8] = b'CPFONT\0\0'
        struct.pack_into('<H', data, 8, 4)
        data[12] = 1
        struct.pack_into('<II', data, 36, 1, count)
        struct.pack_into('<I', data, 56, 64)
        struct.pack_into('<III', data, 64, first, last, 0)
        dest = root / f'synthetic-{first}-{last}.cpfont'
        dest.write_bytes(data)
        paths.append(dest)
    if args.private_font_dir:
        seen = set()
        for source in sorted(args.private_font_dir.rglob('*.cpfont')):
            digest = hashlib.sha256(source.read_bytes()).digest()
            if digest not in seen:
                seen.add(digest)
                paths.append(source)
        print(f'private: {len(seen)} unique fonts')
    compare('sdfont', paths)
    compare('sdalloc', paths)
compare('sdname', [])
