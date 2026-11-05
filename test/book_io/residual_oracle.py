#!/usr/bin/env python3
"""Frozen original-C++ package decisions, with file traces and cache bytes."""
import argparse
import gzip
import io
import json
from pathlib import Path
import re
import zipfile
from run import run, synthetic


def package(guide, *, nav=True, cover=False, css='', root='OPS/'):
    source = zipfile.ZipFile(io.BytesIO(synthetic()))
    entries = {name: source.read(name) for name in source.namelist()}
    opf = entries['OPS/book.opf'].decode()
    if not cover:
        opf = opf.replace('properties="cover-image"', '')
    if not nav:
        opf = opf.replace('properties="nav"', '').replace('<spine>', '<spine toc="ncx">')
        opf = opf.replace('</manifest>', '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>')
    opf = opf.replace('</package>', '<guide><reference type="cover" href="cover/page.xhtml"/></guide></package>')
    entries['OPS/book.opf'] = opf.encode()
    entries['OPS/cover/page.xhtml'] = guide.encode()
    entries['OPS/toc.ncx'] = b'<ncx><navMap><navPoint><navLabel><text>Chapter</text></navLabel><content src="a.xhtml"/></navPoint></navMap></ncx>'
    entries['OPS/style.css'] = css.encode()
    entries['OPS/extra.CSS'] = b'p { color: black; }'
    entries['outside.css'] = b'p { color: white; }'
    entries['OPS/cover.png'] = b'test-codec-input'
    entries['OPS/cover/image one.JPG'] = b'test-codec-input'
    if not root:
        entries = {name.removeprefix('OPS/'): body.replace(b'OPS/book.opf', b'book.opf') for name, body in entries.items()}
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, body in entries.items():
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, body)
    return output.getvalue()


def cases():
    guides = ['', '<img src="../cover.png">', '<svg><image xlink:href="image%20one.JPG"/></svg>',
              '<img src="../cover.png"><svg><image xlink:href="image%20one.JPG"/></svg>',
              '<image xlink:href="nested.svg"><img src="../cover.png">',
              '<image xlink:href="nested.svg"><image xlink:href="../cover.png">',
              '<img src="bad.bmp"><img src="../cover.png">',
              '<img src="bad.png?query"><img src="../cover.png">',
              "<img src='../cover.png'>", '<img src="unterminated', '<img src="">']
    result = []
    for index, guide in enumerate(guides):
        for nav in [False, True]:
            result.append((f'guide-{index}-nav-{nav}', package(guide, nav=nav), 0, {}))
    for size in [128 * 1024 - 1, 128 * 1024, 128 * 1024 + 1]:
        result.append((f'css-{size}', package('', css=' ' * size), 0, {}))
    result.append(('root-opf', package(guides[1], root=''), 0, {}))
    result.append(('cover-precedence', package(guides[2], cover=True), 0, {}))
    for heap in [65535, 65536, 65537]:
        result.append((f'heap-{heap}', package(''), 0, {'BOOK_FREE_HEAP': str(heap)}))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', required=True, type=Path)
    parser.add_argument('--golden', type=Path, default=Path(__file__).with_name('residual-goldens'))
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    binary = args.oracle.resolve()
    inputs = cases()
    probe = inputs[2][1]
    steps = int(re.search(r'steps (\d+)', run(binary, probe)['trace'])[1])
    inputs += [(f'guide-fault-{n}', probe, n, {}) for n in range(1, steps + 1)]
    if args.record:
        expected = {name: run(binary, data, fault, options) for name, data, fault, options in inputs}
        args.golden.mkdir(parents=True, exist_ok=True)
        rows = list(expected.items())
        for i in range(0, len(rows), 64):
            with gzip.open(args.golden / f'{i // 64:02d}.json.gz', 'wt') as output:
                json.dump(dict(rows[i:i + 64]), output, sort_keys=True)
    else:
        expected = {}
        for shard in sorted(args.golden.glob('*.json.gz')):
            with gzip.open(shard, 'rt') as source:
                part = json.load(source)
            assert expected.keys().isdisjoint(part)
            expected.update(part)
        assert set(expected) == {row[0] for row in inputs}
        for name, data, fault, options in inputs:
            actual = run(binary, data, fault, options)
            if actual != expected[name]:
                Path('/tmp/book-residual-actual.json').write_text(json.dumps(actual, indent=2))
                Path('/tmp/book-residual-expected.json').write_text(json.dumps(expected[name], indent=2))
                raise SystemExit(f'mismatch: {name}')
    print(f'PASS {len(inputs)} original-C++ package cases, including {steps} HAL faults')


if __name__ == '__main__':
    main()
