"""Compare cache operation traces with the frozen da609bbf C++ implementation."""
import argparse
import gzip
import hashlib
import pathlib
import subprocess
import struct
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=pathlib.Path, required=True)
parser.add_argument('--font', type=pathlib.Path, action='append', default=[])
parser.add_argument('--private-font-dir', type=pathlib.Path)
parser.add_argument('--texts', type=pathlib.Path)
parser.add_argument('--save', type=pathlib.Path)
args = parser.parse_args()


def synthetic(styles, first=65, replacement=True):
    count = 7
    data = bytearray(32 + 32 * len(styles))
    data[:8] = b'CPFONT\0\0'
    struct.pack_into('<H', data, 8, 4)
    data[12] = len(styles)
    for slot, style in enumerate(styles):
        intervals = [(first, first + count - 2, 0), (0xfffd, 0xfffd, count - 1)] if replacement else [(first, first + count - 1, 0)]
        toc = 32 + slot * 32
        data[toc] = style
        struct.pack_into('<II', data, toc + 4, len(intervals), count)
        data[toc + 12] = 8
        struct.pack_into('<hh', data, toc + 13, 8, -2)
        has_kern = first == 65
        struct.pack_into('<HHBBB', data, toc + 17, 2 if has_kern else 0, 2 if has_kern else 0,
                         2 if has_kern else 0, 2 if has_kern else 0, 1 if has_kern else 0)
        struct.pack_into('<I', data, toc + 24, len(data))
        for interval in intervals:
            data.extend(struct.pack('<III', *interval))
        for glyph in range(count):
            data.extend(struct.pack('<BBHhhH2xI', 8, 8, (glyph + 8) * 16, -1, 8, 8, (count - 1 - glyph) * 8))
        if has_kern:
            for cp, cls in [(65, 1), (67, 2), (66, 1), (68, 2)]:
                data.extend(struct.pack('<HB', cp, cls))
            data.extend(bytes([253, 0, 4, 251]))
            data.extend(struct.pack('<II', 69 << 16 | 70, 65))
        for glyph in reversed(range(count)):
            data.extend(bytes([glyph * 23]) * 8)
    return data

with tempfile.TemporaryDirectory(prefix='sdpool-') as temp:
    paths = []
    for source in sorted((HERE.parent / 'text_golden/fonts').glob('*.cpfont.gz')):
        dest = pathlib.Path(temp) / source.stem
        dest.write_bytes(gzip.decompress(source.read_bytes()))
        paths.append(dest)
    for name, data in [('styles', synthetic([3, 1, 2, 0])), ('above-bmp', synthetic([2], 0x10000, False))]:
        dest = pathlib.Path(temp) / (name + '.cpfont')
        dest.write_bytes(data)
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
