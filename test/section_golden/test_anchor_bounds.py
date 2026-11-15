#!/usr/bin/env python3
"""Exercise the buffered TOC reader and direct anchor reader on the same cuts."""
import argparse
import json
import struct
from pathlib import Path

from test_codec_coverage import fixture, invoke


def observations(binary):
    data, _ = fixture()
    anchors, paragraphs = struct.unpack_from('<II', data, 28)
    cases = {'complete': data}
    for end in range(anchors, paragraphs + 1):
        cases[f'cut-{end - anchors}'] = data[:end]
    for length in (0, 1, 255, 65535, 0x7fffffff, 0xffffffff):
        changed = bytearray(data)
        struct.pack_into('<I', changed, anchors + 2, length)
        cases[f'length-{length}'] = bytes(changed)
    for version in (0, 1, 67, 69, 213, 215, 255):
        cases[f'version-{version}'] = bytes([version]) + data[1:]
    results = {name: invoke(binary, value, '--lookups') for name, value in cases.items()}
    assert 'toc\t3\t4' in results['complete']
    for name, lines in results.items():
        if name.startswith('version-'):
            assert lines == ['section\t0\t0'], name
        elif name.startswith('length-') and int(name[7:]) >= 255:
            assert all(line.endswith('\t0') for line in lines if line.startswith('toc\t')), name
            assert not any(line.startswith(('direct\t', 'find\t')) and not line.endswith('\tnone')
                           for line in lines), name
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', type=Path, required=True)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    actual = observations(args.dump.resolve())
    if args.capture:
        with args.capture.open('x') as file:
            json.dump(actual, file, indent=2)
            file.write('\n')
    if args.compare:
        assert actual == json.loads(args.compare.read_text()), 'anchor outcomes changed'
    print(f'PASS {len(actual)} anchor and version cases')
