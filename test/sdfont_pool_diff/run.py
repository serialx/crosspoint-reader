"""Compare cache operation traces with the frozen da609bbf C++ implementation."""
import argparse
import gzip
import hashlib
import pathlib
import json
import os
import re
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
        outputs, cases, relocation_rows = [], [], []
        for variant in ('reference', 'candidate'):
            command = [str(args.build_dir.resolve() / f'sdpool_{variant}'), str(path)]
            if args.texts:
                command.append(str(args.texts))
            output = subprocess.check_output(command)
            # Allocation events remain ordered; only diagnostic log order is canonicalized.
            records, logs, by_case, moves = [], [], {}, []
            name = None

            def flush():
                records.extend(sorted(logs))
                logs.clear()

            def finish():
                flush()
                if name is not None:
                    if name in by_case:
                        raise SystemExit(f'duplicate case: {name}')
                    by_case[name] = b''.join(records)

            for line in output.splitlines(keepends=True):
                if line.startswith(b'CASE '):
                    finish()
                    name = line.decode().strip()[5:]
                    records.clear()
                elif line.startswith(b'MOVE '):
                    match = re.fullmatch(rb'MOVE (\d+) style=([0-3]) table=(advance|intervals|glyphs) bytes=(\d+) copy=(\d+) (ok|failed)\n', line)
                    if not match or not 0 < int(match[4]) <= 8192 or int(match[5]) > int(match[4]):
                        raise SystemExit(f'invalid move record: {line!r}')
                    moves.append(dict(case=name, ordinal=int(match[1]), style=int(match[2]),
                                      table=match[3].decode(), bytes=int(match[4]),
                                      copied=int(match[5]), result=match[6].decode()))
                elif line.startswith(b'RELOCATIONS '):
                    match = re.fullmatch(rb'RELOCATIONS requests=(\d+) failed=(\d+)\n', line)
                    rows = [row for row in moves if row['case'] == name]
                    if not match or [row['ordinal'] for row in rows] != list(range(1, int(match[1]) + 1)):
                        raise SystemExit(f'missing/extra relocation records: {name}')
                    if sum(row['result'] == 'failed' for row in rows) != int(match[2]):
                        raise SystemExit(f'relocation failure mismatch: {name}')
                elif line.startswith(b'LOG '):
                    logs.append(line)
                else:
                    flush()
                    records.append(line)
                    if b' state=' in line:
                        for row in moves:
                            if row['case'] == name and 'checkpoint' not in row:
                                row['checkpoint'] = line.split()[0].decode()
            finish()
            outputs.append(output)
            cases.append(by_case)
            relocation_rows.append(moves)
        reference, candidate = cases
        move_cases = [name for name in candidate if name.startswith('relocation-oom ')]
        expected_moves = sum(row['case'] == 'baseline' for row in relocation_rows[1])
        failure_count = 0 if os.getenv('SDPOOL_BASE_ONLY') else expected_moves
        assert move_cases == [f'relocation-oom {i}' for i in range(1, failure_count + 1)]
        assert [name for name in candidate if name not in move_cases] == list(reference)
        for name, actual in candidate.items():
            expected = reference['baseline' if name in move_cases else name]
            if expected != actual:
                lines = [part.splitlines() for part in (expected, actual)]
                first = next((i for i, pair in enumerate(zip(*lines)) if pair[0] != pair[1]), min(map(len, lines)))
                raise SystemExit(f'{path.name}, {name}: mismatch at line {first}: {lines[0][first:first+1]} / {lines[1][first:first+1]}')
        if args.save:
            args.save.mkdir(parents=True, exist_ok=True)
            for variant, output in zip(('reference', 'candidate'), outputs):
                (args.save / f'{index:02d}.{variant}.trace.gz').write_bytes(gzip.compress(output, mtime=0))
            (args.save / f'{index:02d}.relocations.json').write_text(json.dumps({
                'font': path.name,
                'counts': {name: sum(row['case'] == name for row in relocation_rows[1]) for name in candidate},
                'moves': relocation_rows[1],
            }, indent=2) + '\n')
        print(f'{path.name}: {len(reference)} historical cases equal, {len(move_cases)} added move failures; '
              f'baseline moves={expected_moves}, all moves={len(relocation_rows[1])}; '
              f'reference sha256={hashlib.sha256(outputs[0]).hexdigest()}', flush=True)
        for row in relocation_rows[1]:
            if row['case'] == 'baseline':
                print(f'  {row}', flush=True)
