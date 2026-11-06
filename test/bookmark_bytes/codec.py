#!/usr/bin/env python3
"""Compare the dormant Rust encoder with the immutable D0 serialized bytes."""
import argparse
import json
from pathlib import Path
import subprocess
from run import corpus

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--binary', type=Path)
    source.add_argument('--build', type=Path)
    args = parser.parse_args()
    if args.build:
        command = ['cargo', '--offline', '--locked', '--manifest-path',
                   str(HERE / 'rust/Cargo.toml'), '--target-dir', str(args.build)]
        subprocess.run([command[0], 'build', *command[1:], '--bin', 'bookmark-codec'], check=True)
        subprocess.run([command[0], 'test', *command[1:], '--lib'], check=True)
        args.binary = args.build / 'debug/bookmark-codec'
    golden = json.loads((HERE / 'golden.json').read_text())
    cases, wire, expected = [], [], []
    for case in corpus():
        if case['action'] != 'save' or any(key in case for key in
                                         ('fail', 'failures', 'write_limit')):
            continue
        cases.append(case['id'])
        for row in case['records']:
            wire.append('\t'.join(str(row[key]) for key in
                                  ('xpath', 'summary', 'name', 'bits', 'si', 'pc', 'pp'))
                        + '\t' + (str(row['vo']) if row['has_vo'] else '-'))
        wire.append('save')
        reference = golden[case['id']]
        if not reference['ok']:
            expected.append('error:NameTooLong')
        else:
            physical_path = bytes.fromhex(reference['path']).split(b'\0', 1)[0].hex()
            expected.append(next(row['bytes'] for row in reference['files']
                                 if row['path'] == physical_path))
    actual = subprocess.check_output([str(args.binary)], input='\n'.join(wire)+'\n',
                                     text=True).splitlines()
    assert len(actual) == len(expected), (len(actual), len(expected))
    for name, want, got in zip(cases, expected, actual):
        assert want == got, f'{name}: expected {want}, got {got}'
    print(f'{len(cases)} raw D0 encodings match; no JSON normalization')

    cases, wire, expected = [], [], []
    for case in corpus():
        if case['action'] != 'load' or not case['files'] or any(
                key in case for key in ('fail', 'read_limit')):
            continue
        cases.append(case['id'])
        wire.append('load\t' + case['files'][0]['bytes'])
        reference = golden[case['id']]
        if not reference['ok']:
            expected.append(None)
            continue
        rows = ['ok']
        for row in reference['records']:
            rows.append('\t'.join(str(row[key]) for key in
                                  ('xpath', 'summary', 'name', 'bits', 'si', 'pc', 'pp'))
                        + '\t' + (str(row['vo']) if row['has_vo'] else '-'))
        expected.append(';'.join(rows))
    actual = subprocess.check_output([str(args.binary)], input='\n'.join(wire)+'\n',
                                     text=True).splitlines()
    assert len(actual) == len(expected)
    for name, want, got in zip(cases, expected, actual):
        assert got.startswith('error:') if want is None else got == want, (
            f'{name}: expected {want}, got {got}')
    print(f'{len(cases)} D0 decodings match exact bytes, float bits, defaults and offsets')


if __name__ == '__main__':
    main()
