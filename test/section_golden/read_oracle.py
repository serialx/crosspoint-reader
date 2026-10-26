#!/usr/bin/env python3
"""Capture production C++ page reads, including malformed-cache outcomes."""
import argparse
import atexit
from concurrent.futures import ThreadPoolExecutor
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import threading

HERE = Path(__file__).resolve().parent
WORKER = threading.local()
SERVERS = []


@atexit.register
def close_servers():
    for server in SERVERS:
        server.stdin.close()
        server.wait(timeout=5)


def isolated_probe(exe, mode, source, temp, setting, pages):
    # The server forks a fresh child for each decode; decoder state never survives.
    if not hasattr(WORKER, 'servers'):
        WORKER.servers = {}
    if exe not in WORKER.servers:
        WORKER.servers[exe] = subprocess.Popen([str(exe), '--serve'], stdin=subprocess.PIPE,
                                             stdout=subprocess.PIPE, text=True)
        SERVERS.append(WORKER.servers[exe])
    server = WORKER.servers[exe]
    server.stdin.write('\n'.join(map(str, (mode, source, temp, setting, pages))) + '\n')
    server.stdin.flush()
    lines = []
    while True:
        line = server.stdout.readline()
        if not line:
            raise ValueError('oracle server exited before child status')
        if line.startswith('exit\t'):
            return int(line.split()[1]), ''.join(lines).encode('ascii')
        lines.append(line)


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


def run(exe, name, data, pages, probe=False, offsets=False):
    with tempfile.TemporaryDirectory(prefix='section-read-') as temp:
        source = Path(temp) / 'input.bin'
        source.write_bytes(data)
        try:
            mode = '--offsets' if offsets else '--header' if probe and len(data) < 44 else '--probe' if probe else '--read'
            if probe:
                status, output = isolated_probe(exe, mode, source, temp, name.split('/')[-2], pages)
            else:
                p = subprocess.run([str(exe), mode, str(source), temp, name.split('/')[-2], str(pages)],
                                   capture_output=True, timeout=2)
                status, output = p.returncode, p.stdout
        except subprocess.TimeoutExpired as error:
            status, output = 'timeout', error.stdout or b''
        lines = output.decode('ascii').splitlines()
        if probe:
            keys = ('section\t', 'page\t', 'retained\t', 'visible\t', 'lookup\t') if offsets else ('section\t', 'page\t', 'retained\t')
            return {'exit': status, 'results': [line for line in lines if line.startswith(keys)]}
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


def record_boundaries(data):
    """Walk v68 records from xp-core/src/section/mod.rs, including payload ends."""
    f = Fields(data)
    boundaries = {0}

    def take(n):
        boundaries.add(f.pos)
        value = f.take(n)
        boundaries.add(f.pos)
        return value

    def number(fmt):
        return struct.unpack('<' + fmt, take(struct.calcsize('<' + fmt)))[0]

    def string():
        take(number('I'))

    take(44)
    pages = struct.unpack_from('<H', data, 22)[0]
    lut, anchors, paragraphs, items, visible = struct.unpack_from('<IIIII', data, 24)
    for page in range(pages):
        f.pos = struct.unpack_from('<I', data, lut + page * 4)[0]
        for _ in range(number('H')):
            tag = number('B')
            if tag == 1:
                take(4)  # Position
                words, focus, text, _ = struct.unpack('<HBHH', take(7))
                take(words * (13 + (3 if focus else 0)) + text)
                for _ in range(words):
                    string()
                take(28)  # BlockStyle
                if number('B'):
                    take(12 + words)
            elif tag == 2:
                take(4)
                string()
                string()
                take(4)
            elif tag == 3:
                take(7)
            elif tag == 4:
                cap = take(12)
                take(cap[-1])
            elif tag == 5:
                take(17)
            else:
                raise ValueError(f'unknown element tag {tag}')
        for _ in range(number('H')):
            take(32)
            take(256)
        for _ in range(number('H')):
            take(256)
            take(8)
        end = struct.unpack_from('<I', data, lut + (page + 1) * 4)[0] if page + 1 < pages else lut
        if f.pos != end:
            raise ValueError(f'page boundary mismatch: {f.pos} != {end}')
    f.pos = lut
    for _ in range(pages):
        take(4)
    if f.pos != anchors:
        raise ValueError('anchor map boundary mismatch')
    for _ in range(number('H')):
        string()
        take(2)
    if f.pos != paragraphs:
        raise ValueError('paragraph table boundary mismatch')
    count = number('H')
    for _ in range(count):
        take(2)
    if f.pos != items:
        raise ValueError('list-item table boundary mismatch')
    for _ in range(count):
        take(2)
    if f.pos != visible:
        raise ValueError('visible table boundary mismatch')
    for _ in range(pages):
        take(4)
    if f.pos != len(data):
        raise ValueError('section boundary mismatch')
    return boundaries


def truncation_sizes(data):
    cuts = set(range(0, len(data), 64)) | set(range(min(65, len(data))))
    for boundary in record_boundaries(data):
        cuts.update(n for n in (boundary - 1, boundary, boundary + 1) if 0 <= n < len(data))
    return sorted(cuts)


def mutations(data):
    for size in truncation_sizes(data):
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


HEADER_FIELDS = {'page-count': (22, 'H'), 'page-lut': (24, 'I'), 'anchors': (28, 'I'),
                 'paragraphs': (32, 'I'), 'list-items': (36, 'I'), 'visible': (40, 'I')}


def header_mutations(data):
    """Mutate every table offset and the u16 page count in final/partial headers."""
    for kind, source in (('final', data), ('partial', bytes([214]) + data[1:] + struct.pack('<II', 100, 200))):
        for field, (pos, fmt) in HEADER_FIELDS.items():
            old = struct.unpack_from('<' + fmt, source, pos)[0]
            mask = 0xffff if fmt == 'H' else 0xffffffff
            values = {n & mask for n in (0, 1, old - 1, old + 1, 0x7fffffff, 0x80000000,
                                        0xfffffffc, 0xfffffffd, 0xfffffffe, 0xffffffff,
                                        len(source) - 1, len(source), len(source) + 1)}
            for value in sorted(values):
                copy = bytearray(source)
                struct.pack_into('<' + fmt, copy, pos, value)
                yield f'header:{kind}:{field}:{value}', bytes(copy)
        # Review trigger: trailer wraps to the header, independently of real LUTs.
        if kind == 'partial':
            for value in range(0xfffffffc, 0x100000000):
                copy = bytearray(source)
                struct.pack_into('<H', copy, 22, 1)
                struct.pack_into('<II', copy, 36, 44, value)
                yield f'header:partial:trailer-wrap:{value}', bytes(copy)


def mutation_labels(data):
    return [label for label, _ in mutations(data)]


def labels_hash(labels):
    return hashlib.sha256('\n'.join(labels).encode()).hexdigest()


def pack_mutations(records, headers=False):
    """Lossless runs omit repeated outcomes and derivable mutation labels."""
    packed = {}
    with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        for member in archive:
            if not member.isfile():
                continue
            data = archive.extractfile(member).read()
            labels = [label for label, _ in header_mutations(data)] if headers else mutation_labels(data)
            section = records[member.name]
            if set(labels) != set(section):
                raise ValueError(f'mutation set mismatch: {member.name}')
            runs = []
            for label in labels:
                record = section[label]
                if runs and runs[-1][1] == record:
                    runs[-1][0] += 1
                else:
                    runs.append([1, record])
            packed[member.name] = {'labels_sha256': labels_hash(labels), 'runs': runs}
    if set(packed) != set(records):
        raise ValueError('mutation section set mismatch')
    return {'encoding': 'header-mutation-runs-v1' if headers else 'mutation-runs-v1', 'sections': packed}


def unpack_mutations(packed):
    if packed.get('encoding') not in ('mutation-runs-v1', 'header-mutation-runs-v1'):
        return packed
    records = {}
    with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        for member in archive:
            if not member.isfile():
                continue
            data = archive.extractfile(member).read()
            labels = ([label for label, _ in header_mutations(data)]
                      if packed['encoding'] == 'header-mutation-runs-v1' else mutation_labels(data))
            section = packed['sections'][member.name]
            if labels_hash(labels) != section['labels_sha256']:
                raise ValueError(f'mutation sampling changed: {member.name}')
            values = []
            for count, record in section['runs']:
                if not isinstance(count, int) or count <= 0 or len(values) + count > len(labels):
                    raise ValueError('invalid mutation run length')
                # Exceptions mutate one result; runs must not alias each other.
                values.extend({'exit': record['exit'], 'results': list(record['results'])} for _ in range(count))
            if len(values) != len(labels):
                raise ValueError('mutation run count mismatch')
            records[member.name] = dict(zip(labels, values, strict=True))
    if set(records) != set(packed['sections']):
        raise ValueError('mutation section set mismatch')
    return records


def exception_cases(exceptions):
    for case in exceptions['cases']:
        if 'mutations' in case:
            if case['class'] != 'checked-offset-arithmetic':
                raise ValueError('grouped mutations require the checked-offset class')
            for name in case.get('sections', [case.get('section')]):
                for mutation in case['mutations']:
                    yield {'class': case['class'], 'section': name, 'mutation': mutation}
        elif 'truncations' in case:
            if case['class'] != 'truncated-header':
                raise ValueError('grouped truncations require the header class')
            for cut in case['truncations']:
                yield {'class': case['class'], 'section': case['section'], 'mutation': f'truncate:{cut}'}
        else:
            yield case


def checked_offset_result(data, mutation, record):
    """Prove cfc874ca Section.cpp:247,892,1149 wraps, and only change its effects."""
    mutated = dict(header_mutations(data)).get(mutation)
    _, kind, field, _ = mutation.split(':')
    if mutated is None or field not in ('visible', 'trailer-wrap') or (field == 'trailer-wrap' and kind != 'partial'):
        raise ValueError('exception is not a sampled visible-offset mutation')
    count = struct.unpack_from('<H', mutated, 22)[0]
    li, visible = struct.unpack_from('<II', mutated, 36)
    pages = struct.unpack_from('<H', data, 22)[0]
    if kind == 'partial':
        # The uint32_t assignment at line 247 wraps before the trailer size check.
        total = visible + count * 4
        trailer = total & 0xffffffff
        if not (count > 0 and li >= 44 and visible > li and total > 0xffffffff and trailer + 8 <= len(mutated)):
            raise ValueError('partial exception does not prove a readable wrapped trailer')
    before = [f'section\t1\t{count}']
    after = list(before)
    wrapped = False
    for page in range(pages):
        total = visible + page * 4
        position = total & 0xffffffff
        old = (struct.unpack_from('<I', mutated, position)[0]
               if page < count and visible >= 44 and position + 4 <= len(mutated) else None)
        new = old
        if old is not None and total > 0xffffffff:
            # Lines 892/1149 assign to uint32_t before checking file bounds.
            new = None
            wrapped = True
        for results, value in ((before, old), (after, new)):
            results.append(f'lookup\t{page}\t{value if value is not None else "none"}')
            results.append(f'page\t{page}\t{int(page < count)}')
            if page < count:
                results.append(f'visible\t{page}\t{value if value is not None else 0}')
    before.append('retained\t1')
    after.append('retained\t1')
    if kind == 'partial':
        after = ['section\t0\t0', 'retained\t0']
    elif not wrapped:
        raise ValueError('finalized exception has no readable wrapped entry')
    if record != {'exit': 0, 'results': before}:
        raise ValueError('baseline does not match the proven wrapped reads')
    return {'exit': 0, 'results': after}


def apply_exceptions(expected, path, headers=False):
    exceptions = json.loads(path.read_text())
    seen = set()
    with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        for case in exception_cases(exceptions):
            name, mutation = case['section'], case['mutation']
            category = case.get('class', 'short-lut')
            if category not in ('truncated-header', 'short-lut', 'checked-offset-arithmetic'):
                raise ValueError(f'unknown exception class: {category}')
            if (category == 'checked-offset-arithmetic') != headers:
                continue
            page = case.get('rejected_page')
            key = (name, mutation, page)
            if key in seen:
                raise ValueError(f'duplicate exception: {key}')
            seen.add(key)
            data = archive.extractfile(name).read()
            if category == 'checked-offset-arithmetic':
                expected[name][mutation] = checked_offset_result(data, mutation, expected[name][mutation])
                continue
            if not mutation.startswith('truncate:'):
                raise ValueError(f'exception is not a truncation: {key}')
            cut = int(mutation.removeprefix('truncate:'))
            record = expected[name][mutation]
            if record['exit'] != 0:
                raise ValueError(f'exception was not an ordinary C++ result: {key}')
            if category == 'truncated-header':
                # Settings end at 22; pageCount starts initialized to zero.
                count = int.from_bytes(data[22:cut][:2].ljust(2, b'\0'), 'little')
                if (data[0] != 68 or not 22 <= cut < 44 or page is not None or
                        record['results'] != [f'section\t1\t{count}', 'retained\t1']):
                    raise ValueError(f'exception is not an accepted truncated finalized header: {key}')
                record['results'] = ['section\t0\t0', 'retained\t0']
            elif category == 'short-lut':
                lut = struct.unpack_from('<I', data, 24)[0]
                count = struct.unpack_from('<H', data, 22)[0]
                if not (isinstance(page, int) and 0 <= page < count and
                        lut + page * 4 < cut < lut + (page + 1) * 4):
                    raise ValueError(f'exception is not a short LUT entry: {key}')
                before, after = f'page\t{page}\t1', f'page\t{page}\t0'
                if record['results'].count(before) != 1:
                    raise ValueError(f'exception does not name one accepted C++ page: {key}')
                record['results'][record['results'].index(before)] = after
            else:
                raise ValueError(f'unknown exception class: {category}')
    return expected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--mutations', action='store_true')
    modes.add_argument('--partial', action='store_true')
    modes.add_argument('--header-mutations', action='store_true')
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--exceptions', type=Path)
    parser.add_argument('--limit', type=int)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not args.verify and args.output.exists():
        parser.error('capture refuses an existing output')
    if args.exceptions and (not args.verify or not (args.mutations or args.header_mutations) or args.limit):
        parser.error('exceptions require full mutation verification')
    exe = args.dump.resolve()
    results = {}
    baseline_records = unpack_mutations(json.loads(gzip.decompress(args.output.read_bytes()))) if args.verify else None
    if args.exceptions:
        baseline_records = apply_exceptions(baseline_records, args.exceptions, args.header_mutations)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool, tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
        members = [m for m in archive if m.isfile()]
        if args.limit:
            members = members[:args.limit]
        for member in members:
            data = archive.extractfile(member).read()
            pages = struct.unpack_from('<H', data, 22)[0]
            if args.mutations or args.partial or args.header_mutations:
                def check(case):
                    label, mutated = case
                    return label, run(exe, member.name, mutated, pages, True, args.header_mutations)
                sampler = header_mutations if args.header_mutations else partial_mutations if args.partial else mutations
                records = dict(pool.map(check, sampler(data)))
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
        if (args.mutations or args.header_mutations) and not args.limit:
            payload = json.dumps(pack_mutations(results, args.header_mutations), sort_keys=True, separators=(',', ':')).encode() + b'\n'
        args.output.write_bytes(gzip.compress(payload, mtime=0))
    print(f'{len(results)} sections; {len(payload)} raw bytes; {args.output.stat().st_size} gzip bytes')

if __name__ == '__main__':
    main()
