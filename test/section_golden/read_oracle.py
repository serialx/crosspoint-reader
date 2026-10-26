#!/usr/bin/env python3
"""Capture production C++ page reads, including malformed-cache outcomes."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent

class Fields:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        result = self.data[self.pos:self.pos + n]
        if len(result) != n:
            raise ValueError('short canonical page')
        self.pos += n
        return result

    def number(self, fmt):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))[0]

    def string(self):
        return self.take(self.number('I')).hex()

    def record(self, names, formats):
        return {name: self.number(fmt) for name, fmt in zip(names.split(), formats.split(), strict=True)}

    def page(self):
        result = {'elements': []}
        for _ in range(self.number('H')):
            el = self.record('tag x y', 'B h h')
            tag = el['tag']
            if tag == 1:
                el.update(self.record('wordCount focus textBytes paragraphStartWord', 'H B H H'))
                n, focus = el['wordCount'], el['focus']
                el['ranges'] = [self.record('start end', 'I I') for _ in range(n)]
                el['offsets'] = [self.number('H') for _ in range(n)]
                el['xpos'] = [self.number('h') for _ in range(n)]
                if focus:
                    el['focusSuffixX'] = [self.number('H') for _ in range(n)]
                el['styles'] = self.take(n).hex()
                if focus:
                    el['focusBoundary'] = self.take(n).hex()
                el['text'] = self.take(el['textBytes']).hex()
                el['ruby'] = [self.string() for _ in range(n)]
                el['blockStyle'] = self.record('alignment textAlignDefined marginTop marginBottom marginLeft marginRight paddingTop paddingBottom paddingLeft paddingRight textIndent textIndentDefined isRtl directionDefined characterSpacing fontId', 'B B h h h h h h h h h B B B b i')
                el['hasWordFonts'] = self.number('B')
                if el['hasWordFonts']:
                    el['wordFonts'] = [self.number('i') for _ in range(3)]
                    el['wordFontSlots'] = self.take(n).hex()
            elif tag == 2:
                el['imagePath'], el['srcPath'] = self.string(), self.string()
                el.update(self.record('width height', 'h h'))
            elif tag == 3:
                el.update(self.record('width thickness', 'H B'))
            elif tag == 4:
                el.update(self.record('fontId scale256 style length', 'i H B B'))
                el['text'] = self.take(el['length']).hex()
            elif tag == 5:
                el.update(self.record('width height', 'H H'))
                el['sides'] = [self.record('width style', 'B B') for _ in range(4)]
                el['shaded'] = self.number('B')
            else:
                raise ValueError(f'unknown tag {tag}')
            result['elements'].append(el)
        result['footnotes'] = [{'number': self.take(32).hex(), 'href': self.take(256).hex()} for _ in range(self.number('H'))]
        result['links'] = []
        for _ in range(self.number('H')):
            link = {'href': self.take(256).hex()}
            link.update(self.record('x y width height', 'h h h h'))
            result['links'].append(link)
        if self.pos != len(self.data):
            raise ValueError('canonical page trailing data')
        return result


def run(exe, name, data, pages, probe=False):
    with tempfile.TemporaryDirectory(prefix='section-read-') as temp:
        source = Path(temp) / 'input.bin'
        source.write_bytes(data)
        try:
            p = subprocess.run([str(exe), '--probe' if probe else '--read', str(source), temp, name.split('/')[-2], str(pages)], capture_output=True, timeout=2)
            status, output = p.returncode, p.stdout
        except subprocess.TimeoutExpired as error:
            status, output = 'timeout', error.stdout or b''
        lines = output.decode('ascii').splitlines()
        if probe:
            return {'exit': status, 'results': [line for line in lines if line.startswith(('section\t', 'page\t'))]}
        if status:
            raise ValueError(f'{name}: failed read {status}')
        records = []
        for line in lines:
            kind, _, value = line.partition('\t')
            if kind == 'wire':
                records.append(Fields(bytes.fromhex(value)).page())
            elif kind == 'text':
                records[-1]['getTextFromSectionFile'] = value
            elif kind == 'visible':
                visible = int(value)
            elif kind == 'page' and value.split('\t')[-1] != '1':
                raise ValueError(f'{name}: rejected page')
            if kind == 'wire':
                records[-1]['visibleTextOffset'] = visible
        return records


def mutations(data):
    for size in range(0, len(data), 64):
        yield f'truncate:{size}', data[:size]
    yield 'version', bytes([data[0] ^ 1]) + data[1:]
    pages = struct.unpack_from('<H', data, 22)[0]
    lut = struct.unpack_from('<I', data, 24)[0]
    for page in range(pages):
        pos = struct.unpack_from('<I', data, lut + page * 4)[0]
        for value in (0, len(data) + 64, 0xffffffff):
            copy = bytearray(data)
            struct.pack_into('<I', copy, lut + page * 4, value)
            yield f'lut:{page}:{value}', bytes(copy)
        copy = bytearray(data)
        struct.pack_into('<H', copy, pos, 0xffff)
        yield f'element-count:{page}', bytes(copy)
    copy = bytearray(data)
    struct.pack_into('<H', copy, 22, 0xffff)
    yield 'page-count', bytes(copy)


def partial_mutations(data):
    partial = bytes([214]) + data[1:] + struct.pack('<II', 100, 200)
    yield 'partial-valid', partial
    yield 'partial-short-watermark', partial[:-1]
    empty = bytearray(partial)
    struct.pack_into('<H', empty, 22, 0)
    yield 'partial-empty', bytes(empty)


def apply_exceptions(expected, path):
    exceptions = json.loads(path.read_text())
    seen = set()
    with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        for case in exceptions['cases']:
            name, mutation, page = case['section'], case['mutation'], case['rejected_page']
            key = (name, mutation, page)
            if key in seen:
                raise ValueError(f'duplicate exception: {key}')
            seen.add(key)
            data = archive.extractfile(name).read()
            cut = int(mutation.removeprefix('truncate:'))
            lut = struct.unpack_from('<I', data, 24)[0]
            if not (lut + page * 4 < cut < lut + (page + 1) * 4):
                raise ValueError(f'exception is not a short LUT entry: {key}')
            record = expected[name][mutation]
            if record['exit'] != 0:
                raise ValueError(f'exception was not an ordinary C++ result: {key}')
            before, after = f'page\t{page}\t1', f'page\t{page}\t0'
            if record['results'].count(before) != 1:
                raise ValueError(f'exception does not name one accepted C++ page: {key}')
            record['results'][record['results'].index(before)] = after
    return expected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--partial', action='store_true')
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--exceptions', type=Path)
    parser.add_argument('--limit', type=int)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not args.verify and args.output.exists():
        parser.error('capture refuses an existing output')
    if args.exceptions and (not args.verify or not args.mutations or args.limit):
        parser.error('exceptions require full mutation verification')
    exe = args.dump.resolve()
    results = {}
    baseline_records = json.loads(gzip.decompress(args.output.read_bytes())) if args.verify else None
    if args.exceptions:
        baseline_records = apply_exceptions(baseline_records, args.exceptions)
    with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        members = [m for m in archive if m.isfile()]
        if args.limit:
            members = members[:args.limit]
        for member in members:
            data = archive.extractfile(member).read()
            pages = struct.unpack_from('<H', data, 22)[0]
            if args.mutations or args.partial:
                def check(case):
                    label, mutated = case
                    return label, run(exe, member.name, mutated, pages, True)
                with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                    records = dict(pool.map(check, partial_mutations(data) if args.partial else mutations(data)))
            else:
                records = run(exe, member.name, data, pages)
            if baseline_records is not None and baseline_records.get(member.name) != records:
                expected = baseline_records.get(member.name)
                if isinstance(records, dict) and isinstance(expected, dict):
                    differences = {key: {'expected': expected.get(key), 'actual': value}
                                   for key, value in records.items() if expected.get(key) != value}
                    raise ValueError(f'{member.name}: {json.dumps(differences)}')
                raise ValueError(f'read oracle mismatch: {member.name}')
            results[member.name] = records
            print(f'{member.name}: {len(records)} records', flush=True)
    payload = json.dumps(results, sort_keys=True, separators=(',', ':')).encode() + b'\n'
    if args.verify:
        baseline = json.dumps(baseline_records, sort_keys=True, separators=(',', ':')).encode() + b'\n'
        if baseline != payload:
            old = json.loads(baseline)
            for name, records in results.items():
                if old.get(name) != records:
                    raise ValueError(f'read oracle mismatch: {name}')
            raise ValueError('read oracle section set mismatch')
    else:
        args.output.write_bytes(gzip.compress(payload, mtime=0))
    print(f'{len(results)} sections; {len(payload)} raw bytes; {args.output.stat().st_size} gzip bytes')

if __name__ == '__main__':
    main()
