#!/usr/bin/env python3
"""Capture/replay EPUB lifecycle results, HAL traces, logs, and persisted bytes."""
import argparse
import base64
import gzip
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def synthetic():
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, body in {
            'META-INF/container.xml': '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
            'OPS/book.opf': '<package><metadata><title>Book</title><creator>Author</creator><language>en</language></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" properties="nav" media-type="application/xhtml+xml"/><item id="css" href="style.css" media-type="text/css"/></manifest><spine><itemref idref="a"/></spine></package>',
            'OPS/nav.xhtml': '<html><body><nav epub:type="toc"><ol><li><a href="a.xhtml">Chapter</a></li></ol></nav></body></html>',
            'OPS/a.xhtml': '<html><body><p>Hello</p></body></html>',
            'OPS/style.css': 'p { margin-top: 4px; }',
        }.items():
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, body)
    return output.getvalue()


def run(binary, data, fail_at=0):
    with tempfile.TemporaryDirectory(prefix='book-io-') as directory:
        root = Path(directory)
        (root / 'book.epub').write_bytes(data)
        result = subprocess.run([str(binary), 'book.epub', str(fail_at)], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        files = {str(p.relative_to(root)): base64.b64encode(p.read_bytes()).decode()
                 for p in sorted((root / 'cache').rglob('*')) if p.is_file()}
        return {'status': result.returncode, 'trace': result.stdout.decode(errors='replace'), 'files': files}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--golden', type=Path, default=Path(__file__).parent / 'golden.json.gz')
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    binary = args.oracle.resolve()
    data = synthetic()
    normal = run(binary, data)
    steps = int(re.search(r'steps (\d+)', normal['trace'])[1])
    cases = [('synthetic', data, 0)]
    cases += [(f'fault-{i}', data, i) for i in range(1, steps + 1)]
    cases += [(p.name, p.read_bytes(), 0) for p in sorted((ROOT / 'test/epubs').glob('*.epub'))]
    cases += [(f'truncate-{i}', data[:i], 0) for i in range(len(data) + 1)]
    if args.record:
        actual = {key: run(binary, value, fault) for key, value, fault in cases}
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        with gzip.open(args.golden, 'wt') as file:
            json.dump(actual, file, sort_keys=True)
        print(f'recorded {len(actual)} cases; {steps} individually failed HAL operations')
        return
    with gzip.open(args.golden, 'rt') as file:
        expected = json.load(file)
    assert set(expected) == {key for key, _, _ in cases}, 'case inventory differs'
    for key, value, fault in cases:
        actual = run(binary, value, fault)
        if actual != expected[key]:
            Path('/tmp/book-io-actual.json').write_text(json.dumps(actual, indent=2))
            Path('/tmp/book-io-expected.json').write_text(json.dumps(expected[key], indent=2))
            raise SystemExit(f'mismatch: {key}; details /tmp/book-io-{{actual,expected}}.json')
    print(f'PASS {len(cases)} cases; {steps} individually failed HAL operations')


if __name__ == '__main__':
    main()
