#!/usr/bin/env python3
"""Capture/replay EPUB lifecycle results, HAL traces, logs, and persisted bytes."""
import argparse
import base64
import gzip
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def synthetic(cover="png", duplicates=0):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, body in {
            'META-INF/container.xml': '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
            'OPS/book.opf': f'<package><metadata><title>Book</title><creator>Author</creator><language>en</language></metadata><manifest><item id="cover" href="cover.{cover}" properties="cover-image" media-type="image/{cover}"/><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" properties="nav" media-type="application/xhtml+xml"/><item id="css" href="style.css" media-type="text/css"/></manifest><spine><itemref idref="a"/></spine></package>',
            'OPS/nav.xhtml': '<html><body><nav epub:type="toc"><ol><li><a href="a.xhtml">Chapter</a></li></ol></nav></body></html>',
            'OPS/a.xhtml': '<html><body><p>Hello</p></body></html>',
            'OPS/style.css': 'p { margin-top: 4px; }',
            f'OPS/cover.{cover}': 'test-codec-input',
        }.items():
            if name == 'OPS/book.opf' and duplicates:
                items = ''.join(f'<item id="css{i}" href="{("style" if i % 2 else "duplicate")}.css" media-type="text/css"/>' for i in range(duplicates))
                body = body.replace('</manifest>', items + '</manifest>')
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, body)
        if duplicates:
            info = zipfile.ZipInfo('OPS/duplicate.css', (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, 'p { margin-top: 4px; }')
    return output.getvalue()


def run(binary, data, fail_at=0, options=None):
    with tempfile.TemporaryDirectory(prefix='book-io-') as directory:
        root = Path(directory)
        (root / 'book.epub').write_bytes(data)
        result = subprocess.run([str(binary), 'book.epub', str(fail_at)], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20,
                                env={**os.environ, **(options or {})})
        files = {str(p.relative_to(root)): base64.b64encode(p.read_bytes()).decode()
                 for p in sorted((root / 'cache').rglob('*')) if p.is_file()}
        return {'status': result.returncode, 'trace': result.stdout.decode(errors='replace'), 'files': files}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--golden', type=Path, default=Path(__file__).parent / 'goldens')
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    binary = args.oracle.resolve()
    data = synthetic()
    normal = run(binary, data)
    steps = int(re.search(r'steps (\d+)', normal['trace'])[1])
    options = {
        'protected': {'BOOK_PROTECTED': '1'},
        'protected-short': {'BOOK_PROTECTED': '1', 'BOOK_PLAIN_SIZE': '6'},
        'protected-long': {'BOOK_PROTECTED': '1', 'BOOK_PLAIN_SIZE': '8'},
        'protected-overflow': {'BOOK_PROTECTED': '1', 'BOOK_PLAIN_SIZE': str(2**64 - 1)},
        'protected-decrypt-failure': {'BOOK_PROTECTED': '1', 'BOOK_DECRYPT_FAIL': '1'},
        'protected-key-failure': {'BOOK_KEY_ERROR': '1'},
        'codec-failure': {'BOOK_CODEC_FAIL': '1'},
        'clear-cache': {'BOOK_CLEAR_CACHE': '1'},
        'clear-cache-fail': {'BOOK_CLEAR_CACHE': '1', 'BOOK_REMOVE_FAIL': '1'},
    }
    allocations = int(re.search(r'allocations (\d+)', normal['trace'])[1])
    options.update({f'cpp-allocation-{i}': {'BOOK_CPP_FAIL': str(i)} for i in range(1, allocations + 1)})
    cases = [('synthetic', data, 0)]
    cases += [(key, data, 0) for key in options]
    cases += [(f'cover-{kind}', synthetic(kind), 0) for kind in ['jpg', 'gif', 'svg']]
    cases += [(f'css-duplicates-{count}', synthetic(duplicates=count), 0) for count in [2, 7, 32]]
    cases += [(f'fault-{i}', data, i) for i in range(1, steps + 1)]
    gif = synthetic('gif')
    gif_steps = int(re.search(r'steps (\d+)', run(binary, gif)['trace'])[1])
    cases += [(f'gif-fault-{i}', gif, i) for i in range(1, gif_steps + 1)]
    cases += [(p.name, p.read_bytes(), 0) for p in sorted((ROOT / 'test/epubs').glob('*.epub'))]
    cases += [(f'truncate-{i}', data[:i], 0) for i in range(len(data) + 1)]
    if args.record:
        actual = {key: run(binary, value, fault, options.get(key)) for key, value, fault in cases}
        args.golden.mkdir(parents=True, exist_ok=True)
        items = list(actual.items())
        for offset in range(0, len(items), 128):
            with gzip.open(args.golden / f'{offset // 128:02d}.json.gz', 'wt') as file:
                json.dump(dict(items[offset:offset + 128]), file, sort_keys=True)
        print(f'recorded {len(actual)} cases; {steps + gif_steps} individually failed HAL operations')
        return
    expected = {}
    for shard in sorted(args.golden.glob('*.json.gz')):
        with gzip.open(shard, 'rt') as file:
            captured = json.load(file)
        assert expected.keys().isdisjoint(captured), 'duplicate captured case'
        expected.update(captured)
    assert set(expected) == {key for key, _, _ in cases}, 'case inventory differs'
    for key, value, fault in cases:
        actual = run(binary, value, fault, options.get(key))
        if actual != expected[key]:
            Path('/tmp/book-io-actual.json').write_text(json.dumps(actual, indent=2))
            Path('/tmp/book-io-expected.json').write_text(json.dumps(expected[key], indent=2))
            raise SystemExit(f'mismatch: {key}; details /tmp/book-io-{{actual,expected}}.json')
    print(f'PASS {len(cases)} cases; {steps + gif_steps} individually failed HAL operations')


if __name__ == '__main__':
    main()
