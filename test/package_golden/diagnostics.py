#!/usr/bin/env python3
"""Compare formatted package diagnostics with the frozen upstream C++ oracle."""
import argparse
import collections
import gzip
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent

def cases(repo):
    spec = importlib.util.spec_from_file_location('package_cases', repo / 'test/package_golden/run.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for epub in sorted((repo / 'test/epubs').glob('*.epub')):
        with zipfile.ZipFile(epub) as archive:
            yield epub.stem, {n: archive.read(n) for n in archive.namelist() if not n.endswith('/')}, module.CHUNKS
    for name, files in module.synthetic():
        yield 'synthetic-' + name, files, module.CHUNKS
    base = {
        'META-INF/container.xml': b'<container><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'book.opf': b'<package><manifest><item id="a" href="a"/><item id="nav" href="nav" properties="nav"/><item href="ncx" media-type="application/x-dtbncx+xml"/></manifest><spine><itemref idref="a"/></spine></package>',
        'nav': b'<html><body><nav type="toc"><ol><li><a href="a">A</a></li></ol></nav></body></html>',
        'ncx': b'<ncx><navMap><navPoint><navLabel><text>A</text></navLabel><content src="a"/></navPoint></navMap></ncx>',
        'a': b'<html/>',
    }
    events = dict(base)
    metadata = '<meta name="cover" content="a"/>' + ''.join(f'<meta property="belongs-to-collection" id="c{i}">Collection</meta>' for i in range(9))
    events['book.opf'] = ('<package><metadata>' + metadata + '</metadata><manifest><item id="a" href="a" media-type="text/html"/><item href="ncx" media-type="application/x-dtbncx+xml"/><item href="duplicate" media-type="application/x-dtbncx+xml"/></manifest><spine><itemref idref="a"/></spine><guide><reference type="cover" href="a"/></guide></package>').encode()
    yield 'diagnostic-events', events, module.CHUNKS
    large = dict(base)
    large['book.opf'] = ('<package><manifest>' + ''.join(f'<item id="i{i}" href="a"/>' for i in range(405)) + '</manifest><spine>' + ''.join(f'<itemref idref="i{i}"/>' for i in range(405)) + '</spine></package>').encode()
    yield 'diagnostic-large', large, (1, 1024)
    for path in ('META-INF/container.xml', 'book.opf', 'nav', 'ncx'):
        data = base[path]
        for length in range(len(data)):
            changed = dict(base)
            changed[path] = data[:length]
            yield 'truncate-' + path.replace('/', '_') + '-' + str(length), changed, (7, 1024)
        for offset in range(0, len(data), 7):
            changed = dict(base)
            changed[path] = data[:offset] + bytes([data[offset] ^ 128]) + data[offset + 1:]
            yield 'bitflip-' + path.replace('/', '_') + '-' + str(offset), changed, (7, 1024)

def capture(oracle, repo):
    result = {}
    for name, files, chunks in cases(repo):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            root = work / 'input'
            root.mkdir()
            for relative, data in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            for mode in ('nav', 'ncx'):
                for chunk in chunks:
                    output = work / f'{mode}-{chunk}'
                    process = subprocess.run([str(oracle.resolve()), str(root), str(output), str(chunk), mode], capture_output=True)
                    if process.returncode < 0:
                        raise RuntimeError(f'{name}: crashed: {process.stderr.decode(errors="replace")}')
                    logs = [line for line in process.stderr.decode().splitlines() if line.startswith(('ERR|', 'INF|', 'DBG|'))]
                    result[f'{name}/{mode}-{chunk}'] = {'status': process.returncode, 'logs': logs}
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--repo', type=Path, default=ROOT.parents[1])
    parser.add_argument('--golden', type=Path, default=ROOT / 'goldens/diagnostics.json.gz')
    parser.add_argument('--exceptions', type=Path, default=ROOT / 'diagnostic-exceptions.json')
    parser.add_argument('--record', action='store_true', help='Capture only from an exported 116195b4 oracle')
    args = parser.parse_args()
    actual = capture(args.oracle, args.repo)
    if args.record:
        with gzip.open(args.golden, 'wt') as output:
            json.dump(actual, output, sort_keys=True)
    else:
        with gzip.open(args.golden, 'rt') as source:
            expected = json.load(source)
        exceptions = json.loads(args.exceptions.read_text())
        used = set()
        differences = []
        for key in sorted(expected.keys() | actual.keys()):
            old, new = expected.get(key, {}), actual.get(key, {})
            a, b = collections.Counter(old.get('logs', [])), collections.Counter(new.get('logs', []))
            if a != b or old.get('status') != new.get('status'):
                difference = {'case': key, 'old_status': old.get('status'), 'new_status': new.get('status'), 'missing': list((a-b).elements()), 'extra': list((b-a).elements())}
                if exceptions.get(key, {}).get('difference') == difference:
                    used.add(key)
                else:
                    differences.append(difference)
        if used != set(exceptions):
            raise SystemExit(f'Stale exceptions: {set(exceptions) - used}')
        if differences:
            print(json.dumps(differences, indent=2))
            raise SystemExit(f'{len(differences)} diagnostic schedules differ')
    if not args.record:
        print(f'{len(used)} explicitly approved XML classification exceptions')
    print(f'{len(actual)} schedules, {sum(len(v["logs"]) for v in actual.values())} formatted records')

if __name__ == '__main__':
    main()
