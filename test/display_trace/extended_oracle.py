#!/usr/bin/env python3
"""Freeze or verify extended Rust controller, allocation and diagnostic traces."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import subprocess

from oracle import cases, compare

HERE = Path(__file__).resolve().parent


def scenarios():
    for buffers in ('single', 'dual'):
        for variant in range(4):
            yield f'x3-{buffers}-{variant}', [f'x3_driver_trace_{buffers}', str(variant)]
    for name, profile, controller, buffers in cases():
        if not controller.startswith('uc8') or controller == 'uc8253':
            continue
        faults = [-1, 0, -2] + ([1, 2, 3, 4, 5] if controller == 'uc8279c' else [])
        for fault in faults:
            yield f'{name}-fail-{fault}', [f'display_trace_{profile}_{buffers}', controller,
                                        '--ultrachip', '--compact', str(fault)]
    for profile, count in (('sticky', 2), ('picco', 2), ('papermono', 8)):
        for failure in range(-1, count + 1):
            yield f'{profile}-fail-{failure}', [f'retained_trace_{profile}', str(failure)]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('operation', choices=('record', 'verify'))
    parser.add_argument('--bin-dir', type=Path, required=True)
    parser.add_argument('--goldens', type=Path, default=HERE / 'extended-goldens')
    args = parser.parse_args()
    rows = []
    passed = True
    if args.operation == 'record':
        args.goldens.mkdir(parents=True, exist_ok=True)
    for name, command in scenarios():
        actual = subprocess.check_output([str(args.bin_dir.resolve() / command[0]), *command[1:]], timeout=120)
        path = args.goldens / (name + '.trace.xz')
        if args.operation == 'record':
            compressed = lzma.compress(actual)
            if len(compressed) > 1_000_000:
                raise ValueError(f'{name}: compressed fixture exceeds 1 MB')
            path.write_bytes(compressed)
        else:
            passed &= compare(lzma.decompress(path.read_bytes()), actual, name)
        rows.append({'case': name, 'sha256': hashlib.sha256(actual).hexdigest(), 'bytes': len(actual)})
    if args.operation == 'record':
        (args.goldens / 'manifest.json').write_text(json.dumps({
            'reader': 'e39d25c24223b7b1872885046db2bb7be330c420',
            'sdk': '62a72e7c735210f09631e1908c4e9d046a617371', 'cases': rows}, indent=2) + '\n')
    print(f'{len(rows)} extended traces: {"PASS" if passed else "FAIL"}')
    return int(not passed)


if __name__ == '__main__':
    raise SystemExit(main())
