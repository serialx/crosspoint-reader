#!/usr/bin/env python3
"""Capture only with the original C++ dump; verify with the facade dump."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
import posixpath
import html.parser

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

class Text(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
    def handle_data(self, data):
        self.parts.append(data)

def cases():
    inputs = [b'', b'plain ASCII', b'\0tail', b'A\xcc\x81\0tail']
    # Every composition entry, chains, all algorithmic Hangul pairs.
    entries = json.loads((HERE / 'compose-inputs.json').read_text())
    for base, mark in entries:
        inputs.append(chr(base).encode() + chr(mark).encode())
        inputs.append(b'X' + chr(base).encode() + chr(mark).encode() + b'\xcc\x81Z')
    for l in range(0x1100, 0x1113):
        for v in range(0x1161, 0x1176):
            for t in [0, *range(0x11a8, 0x11c3)]:
                inputs.append((''.join(map(chr, [l,v] + ([t] if t else [])))).encode())
    for lead in range(256):
        for tail in [b'', b'x', b'\x80', b'\x80x', b'\x80\x80', b'\x80\x80x', b'\x80\x80\x80']:
            inputs.extend([bytes([lead]) + tail, b'A\xcc\x81' + bytes([lead]) + tail])
    for cp in [0, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff]:
        inputs.append(chr(cp).encode())
    for path in sorted((ROOT / 'test/epubs').glob('*.epub')):
        with zipfile.ZipFile(path) as z:
            container = ET.fromstring(z.read('META-INF/container.xml'))
            opf = next(e.attrib['full-path'] for e in container.iter() if e.tag.endswith('rootfile'))
            package = ET.fromstring(z.read(opf))
            manifest = {e.attrib['id']: e.attrib['href'] for e in package.iter() if e.tag.endswith('}item')}
            for e in package.iter():
                if not e.tag.endswith('}itemref'):
                    continue
                name = posixpath.normpath(posixpath.join(posixpath.dirname(opf), manifest[e.attrib['idref']]))
                parser = Text()
                parser.feed(z.read(name).decode('utf8'))
                # Keep every text node intact; no whitespace/visibility filtering.
                inputs.extend(s.encode() for s in parser.parts)
    for folder in ['korean_line_breaking', 'token_boundary', 'combining_marks']:
        for path in sorted((ROOT / 'test' / folder).glob('*.cpp')):
            for s in re.findall(r'(?:u8)?"([^"\n]*)"', path.read_text()):
                if any(ord(c) > 127 for c in s):
                    inputs.append(s.encode())
    return sorted(set(inputs))

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--dump', required=True)
    p.add_argument('--capture', action='store_true')
    args = p.parse_args()
    inputs = cases()
    ranges = subprocess.check_output([args.dump])
    data = subprocess.check_output([args.dump, 'cases'], input=b''.join(s.hex().encode()+b'\n' for s in inputs))
    rows = data.splitlines()
    assert len(rows) == len(inputs)
    # Whole-result SHA-256 per input keeps all spine text under the fixture budget.
    records = [s.hex().encode() + b':' + row + b'\n' for s, row in zip(inputs, rows)]
    output = ranges + b'CASES\n' + b''.join(hashlib.sha256(b''.join(records[i:i+32])).hexdigest().encode()+b'\n' for i in range(0, len(records), 32))
    golden = HERE / 'text-goldens.gz'
    if args.capture:
        assert not golden.exists(), 'Never overwrite the C++ baseline'
        golden.write_bytes(gzip.compress(output, mtime=0))
    else:
        assert gzip.decompress(golden.read_bytes()) == output, 'Text golden mismatch'
    print(f'{len(inputs)} byte-exact output digests; {len(ranges.splitlines())} exhaustive codepoint ranges; {golden.stat().st_size} compressed bytes')

if __name__ == '__main__':
    main()
