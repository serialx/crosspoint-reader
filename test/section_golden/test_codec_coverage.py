#!/usr/bin/env python3
"""Focused adapter coverage, compared with the frozen 6dafb158 C++ reader."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest

from read_oracle import HERE, Fields


def fixture(partial=False, spacing=0):
    with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        member = next(m for m in archive if m.isfile() and '/0/' in m.name)
        header = bytearray(archive.extractfile(member).read()[:44])
    text = '“한'.encode()
    cap = struct.pack('<HBhhiHBB', 1, 4, -123, -456, -17, 513, 2, len(text)) + text + bytes(4)
    bodies = [cap] + [bytes(6)] * (2 if partial else 3)
    data = header
    page_offsets = []
    for body in bodies:
        page_offsets.append(len(data))
        data += body
    lut = len(data)
    data += struct.pack('<' + 'I' * len(bodies), *page_offsets)
    anchors = len(data)
    entries = [('start', 0), ('middle', 1), ('duplicate', 2)] + ([] if partial else [('end', 3)])
    data += struct.pack('<H', len(entries))
    for name, page in entries:
        data += struct.pack('<I', len(name)) + name.encode() + struct.pack('<H', page)
    paragraphs = len(data)
    values = [0, 3, 3, 7][:len(bodies)]
    data += struct.pack('<H' + 'H' * len(values), len(values), *values)
    items = len(data)
    data += struct.pack('<' + 'H' * len(bodies), *[0, 0, 2, 5][:len(bodies)])
    visible = len(data)
    data += struct.pack('<' + 'I' * len(bodies), *[0, 10, 10, 30][:len(bodies)])
    data[0] = 214 if partial else 68
    data[20] = spacing & 255
    struct.pack_into('<HIIIII', data, 22, len(bodies), lut, anchors, paragraphs, items, visible)
    if partial:
        data += struct.pack('<II', 100, 200)
    return bytes(data), cap


def invoke(exe, data, mode, setting=0):
    with tempfile.TemporaryDirectory(prefix='codec-coverage-') as temp:
        source = Path(temp) / 'input.bin'
        source.write_bytes(data)
        result = subprocess.run([str(exe), mode, str(source), temp, str(setting), '1'],
                                capture_output=True, text=True, check=True, timeout=5)
        return result.stdout.splitlines()


def observations(exe):
    return {
        'drop-cap': invoke(exe, fixture()[0], '--read'),
        'negative-spacing': invoke(exe, fixture(spacing=-3)[0], '--read', 3),
        'spacing-mismatch': invoke(exe, fixture(spacing=-3)[0], '--read'),
        'final-lookups': invoke(exe, fixture()[0], '--lookups'),
        'partial-lookups': invoke(exe, fixture(partial=True)[0], '--lookups'),
    }


class CodecCoverage(unittest.TestCase):
    def test_drop_cap_non_ascii_signed_round_trip(self):
        self.assertEqual(ACTUAL['drop-cap'], EXPECTED['drop-cap'])
        wire = next(line.split('\t')[1] for line in ACTUAL['drop-cap'] if line.startswith('wire\t'))
        self.assertEqual(bytes.fromhex(wire), fixture()[1])
        cap = Fields(bytes.fromhex(wire)).page()['elements'][0]
        self.assertEqual((cap['x'], cap['y'], cap['fontId']), (-123, -456, -17))
        self.assertEqual(bytes.fromhex(cap['text']).decode(), '“한')

    def test_negative_header_character_spacing(self):
        for name in ('negative-spacing', 'spacing-mismatch'):
            self.assertEqual(ACTUAL[name], EXPECTED[name])
        self.assertEqual(ACTUAL['negative-spacing'][0], 'section\t1\t4')
        self.assertEqual(ACTUAL['spacing-mismatch'], ['section\t0\t0'])

    def test_lookup_duplicates_and_partial_boundary(self):
        for name in ('final-lookups', 'partial-lookups'):
            self.assertEqual(ACTUAL[name], EXPECTED[name])
            self.assertIn('offset-first\t10\t1', ACTUAL[name])
            self.assertIn('offset-last\t10\t2', ACTUAL[name])
            self.assertIn('paragraph-page\t3\t1', ACTUAL[name])
            self.assertIn('list-page\t0\t0', ACTUAL[name])
            self.assertIn('toc\t2\t3', ACTUAL[name])
        self.assertIn('offset-last\t11\tnone', ACTUAL['partial-lookups'])
        self.assertIn('offset-last\t11\t2', ACTUAL['final-lookups'])
        self.assertIn('visible\t3\tnone', ACTUAL['partial-lookups'])
        self.assertIn('visible\t3\t30', ACTUAL['final-lookups'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, default=HERE / 'goldens/codec-coverage.json')
    parser.add_argument('--capture', action='store_true')
    args = parser.parse_args()
    ACTUAL = observations(args.dump.resolve())
    if args.capture:
        with args.baseline.open('x') as output:
            json.dump(ACTUAL, output, indent=2)
            output.write('\n')
    else:
        EXPECTED = json.loads(args.baseline.read_text())
        unittest.main(argv=[__file__])
