#!/usr/bin/env python3
"""Frozen production GIF consumer outcomes; capture only from the pinned C++ base."""
import argparse
import concurrent.futures
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
SMALL = bytes.fromhex('47494638396108000400810000000000555555aaaaaaffffff21f90401000003002c000000000800040000081500010410304020c181051102206830614286030302003b')


def fixtures():
    result = {'small': SMALL}
    for n in range(len(SMALL)):
        result[f'truncate-{n:03}'] = SMALL[:n]
    for pos in range(len(SMALL)):
        for bit in range(8):
            b = bytearray(SMALL)
            b[pos] ^= 1 << bit
            result[f'flip-{pos:03}-{bit}'] = bytes(b)
    for code in list(range(16)) + [31, 127, 255]:
        b = bytearray(SMALL)
        b[43] = code
        result[f'code-{code}'] = bytes(b)
    for at in (6, 8, 34, 36, 38, 40):
        for value in (0, 1, 2, 3071, 3072, 3073, 32767, 32768, 65535):
            b = bytearray(SMALL)
            struct.pack_into('<H', b, at, value)
            result[f'dimension-{at}-{value}'] = bytes(b)
    for disposition in range(8):
        b = bytearray(SMALL)
        b[28] = 1 | disposition << 2
        result[f'disposal-{disposition}'] = bytes(b)
    # Clear, literal 0, EOI (9-bit codes): intentionally incomplete frame.
    for data in (b'\x00\x01\x04\x04', b'\x01\x01', b'\x00\x01\xff\x0f'):
        result[f'lzw-{data.hex()}'] = SMALL[:44] + bytes([len(data)]) + data + b'\0;'
    b = bytearray(SMALL)
    b[42] = 0x81
    result['local-palette'] = b[:43] + SMALL[13:25] + b[43:]
    for label in (1, 0xfe, 0xff, 0x42):
        result[f'extension-{label}'] = SMALL[:25] + bytes([0x21, label, 3, 1, 2, 3, 0]) + SMALL[25:]
    with tarfile.open(ROOT.parent / 'image_golden/corpus.tar.gz') as archive:
        for entry in archive.getmembers():
            if entry.name.endswith('.gif'):
                result[entry.name] = archive.extractfile(entry).read()
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('bmp', type=Path)
    parser.add_argument('frame', type=Path)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'goldens.json.gz')
    parser.add_argument('--differences', type=Path)
    args = parser.parse_args()
    corpus = fixtures()
    with tempfile.TemporaryDirectory(prefix='gif-oracle-') as tmp:
        directory = Path(tmp)
        for name, data in corpus.items():
            (directory / (name + '.gif')).write_bytes(data)
        jobs = [(name, mode) for name in corpus for mode in range(8)]
        def run(job):
            name, mode = job
            path = directory / (name + '.gif')
            command = [str(args.bmp.resolve()), str(path), str(mode)] if mode < 7 else [str(args.frame.resolve()), str(path), str(directory / (name + '.pxc')), '0', '0']
            try:
                p = subprocess.run(command, capture_output=True, timeout=2)
                status = p.returncode
                data = p.stdout
            except subprocess.TimeoutExpired as e:
                status, data = 'timeout', e.stdout or b''
            return f'{name}/{mode}', {'exit': status, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'prefix': data[:44 if mode < 7 else 64].hex()}
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            cases = dict(pool.map(run, jobs))
    document = {'revision': 'da609bbfbb3f4a901823582fc0378aaf2ef3a55e', 'corpus': {k: hashlib.sha256(v).hexdigest() for k, v in corpus.items()}, 'cases': cases}
    if args.capture:
        args.output.write_bytes(gzip.compress(json.dumps(document, sort_keys=True).encode(), mtime=0))
    else:
        old = json.loads(gzip.decompress(args.output.read_bytes()))
        assert old['corpus'] == document['corpus']
        assert old['cases'].keys() == cases.keys()
        differences = {k: {'old': old['cases'][k], 'new': v} for k, v in cases.items() if old['cases'][k] != v}
        if args.differences:
            args.differences.write_text(json.dumps(differences, indent=2))
        if differences:
            raise SystemExit(f'{len(differences)}/{len(cases)} differ: {list(differences)[:12]}')
    print(f'{len(corpus)} fixtures; {len(cases)} outcomes')


if __name__ == '__main__':
    main()
