#!/usr/bin/env python3
"""Capture package parser behavior before a port; verify without updating goldens."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import quote
import zipfile

ROOT = Path(__file__).resolve().parent
CHUNKS = (1, 7, 64, 512, 1024, 4096)


def synthetic():
    container = '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
    opf = '''<package><metadata><title> A  title </title><creator>One</creator><creator> Two </creator><language>en</language><identifier scheme="ISBN">urn:isbn:123</identifier><meta name="cover" content="cover"/></metadata><manifest><item id="one" href="dir/../a%20b.xhtml" media-type="application/xhtml+xml"/><item id="one" href="second.xhtml"/><item id="cover" href="cover.png" media-type="IMAGE/PNG"/><item id="css" href="a.css" media-type="text/css"/><item id="nav" href="nav.xhtml" properties="nav"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine><itemref idref="one"/><itemref/><itemref idref="absent"/></spine><guide><reference type="start" href="dir/../a%20b.xhtml#x"/></guide></package>'''
    nav = '<html><body><nav epub:type="toc"><ol><li><a href="dir/../a%20b.xhtml#x%20y"> A <span>label</span> </a><ol><li><a href="second.xhtml">Nested</a></li></ol></li></ol></nav></body></html>'
    ncx = '<ncx><navMap><navPoint><navLabel><text> A label </text></navLabel><content src="dir/../a%20b.xhtml#x%20y"/><navPoint><navLabel><text>Nested</text></navLabel><content src="second.xhtml"/></navPoint></navPoint></navMap></ncx>'
    encryption = '<encryption><x:EncryptionMethod x:Algorithm="prefix-aes256-cbc-suffix"/></encryption>'
    files = {'META-INF/container.xml': container, 'OPS/book.opf': opf, 'OPS/nav.xhtml': nav,
             'OPS/toc.ncx': ncx, 'META-INF/encryption.xml': encryption, 'OPS/a b.xhtml': '<html/>',
             'OPS/second.xhtml': '<html/>', 'OPS/cover.png': 'image', 'OPS/a.css': 'p{}'}
    yield 'edges', {k: v.encode() for k, v in files.items()}
    for encoding in ('utf-8-sig', 'utf-16', 'utf-16-be'):
        yield encoding, {k: (v.encode(encoding) if k.endswith(('.xml', '.opf', '.xhtml', '.ncx')) else v.encode())
                         for k, v in files.items()}
    variants = [('missing-rootfile', 'META-INF/container.xml', '<container><rootfiles/></container>'),
                ('aes128', 'META-INF/encryption.xml', encryption.replace('aes256', 'aes128')),
                ('font-only', 'META-INF/encryption.xml', encryption.replace('aes256-cbc', 'font-obfuscation')),
                ('case-sensitive', 'META-INF/encryption.xml', encryption.replace('aes256', 'AES256')),
                ('metadata-only', 'OPS/book.opf', '<package><metadata><title>Only metadata</title></metadata></package>'),
                ('long-label', 'OPS/nav.xhtml', nav.replace(' A <span>label</span> ', 'X' * 4097)),
                ('long-title', 'OPS/book.opf', opf.replace(' A  title ', 'x' * 511 + ' y z'))]
    for path in ('META-INF/container.xml', 'OPS/book.opf', 'OPS/nav.xhtml', 'OPS/toc.ncx', 'META-INF/encryption.xml'):
        variants.append(('truncated-' + Path(path).name, path, files[path][:-9]))
    for name, path, value in variants:
        changed = {k: v.encode() for k, v in files.items()}
        changed[path] = value.encode()
        yield name, changed
    yield from unicode_synthetic()


def unicode_synthetic():
    """New expectations come only from the 8f71e528 C++ serializer."""
    nfd = '\u1100\u1161\u11a8 \u304b\u3099 Cafe\u0301'

    def long_href(size):
        prefix, suffix = ('d' * 80 + '/') * 5, '\u1100\u1161\u11a8-\u304b\u3099.xhtml'
        return prefix + 'x' * (size - len((prefix + suffix).encode())) + suffix

    cases = [
        ('unicode-nfd', nfd, ['\u1100\u1161\u11a8.xhtml', '\u304b\u3099.xhtml', '\uac01.xhtml'], False, 'utf-8'),
        ('unicode-at512', 'x' * 503 + '\u1100\u1161\u11a8', [long_href(512)], False, 'utf-8'),
        ('unicode-over512', 'x' * 504 + '\u1100\u1161\u11a8', [long_href(513)], False, 'utf-8'),
        ('unicode-combining512', 'x' * 510 + 'e\u0301', ['\u304b\u3099.xhtml'], False, 'utf-8'),
        ('unicode-escaped', nfd, ['\u1100\u1161\u11a8.xhtml', '\u65e5\u672c\u8a9e.xhtml'], True, 'utf-8'),
        ('unicode-utf16', nfd + ' \U0001f4d6', ['\u65e5\u672c\u8a9e-\U0001f4d6.xhtml'], False, 'utf-16'),
    ]
    for name, title, paths, escaped, encoding in cases:
        hrefs = [quote(path) if escaped else path for path in paths]
        anchor = quote(nfd) if escaped else nfd
        labels = [nfd + ' ' + title for _ in paths]
        manifest = ''.join(f'<item id="i{i}" href="{href}"/>' for i, href in enumerate(hrefs))
        spine = ''.join(f'<itemref idref="i{i}"/>' for i in range(len(paths)))
        nav = ''.join(f'<li><a href="{href}#{anchor}">{label}</a></li>'
                      for href, label in zip(hrefs, labels))
        ncx = ''.join(f'<navPoint><navLabel><text>{label}</text></navLabel>'
                      f'<content src="{href}#{anchor}"/></navPoint>' for href, label in zip(hrefs, labels))
        files = {
            'META-INF/container.xml': '<container><rootfiles><rootfile full-path="OPS/book.opf" '
                                      'media-type="application/oebps-package+xml"/></rootfiles></container>',
            'OPS/book.opf': f'<package><metadata><title>{title}</title><creator>{nfd}</creator>'
                            f'<meta name="calibre:series" content="{title}"/></metadata><manifest>{manifest}'
                            '<item id="nav" href="nav.xhtml" properties="nav"/>'
                            '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                            f'</manifest><spine>{spine}</spine></package>',
            'OPS/nav.xhtml': f'<html><body><nav epub:type="toc"><ol>{nav}</ol></nav></body></html>',
            'OPS/toc.ncx': f'<ncx><navMap>{ncx}</navMap></ncx>',
        }
        files.update(('OPS/' + path, '<html/>') for path in paths)
        yield name, {path: text.encode(encoding) for path, text in files.items()}


def capture(executable, extracted, work):
    results = {}
    for mode in ('nav', 'ncx'):
        for chunk in CHUNKS:
            output = work / f'{mode}-{chunk}'
            process = subprocess.run([str(executable.resolve()), str(extracted), str(output), str(chunk), mode],
                                     capture_output=True)
            if process.returncode < 0:
                raise RuntimeError(f'oracle crashed: {process.returncode}: {process.stderr.decode(errors="replace")}')
            results[f'{mode}-{chunk}'] = {'status': process.returncode,
                                         'files': {p.name: p.read_bytes().hex() for p in sorted(output.iterdir()) if p.is_file()}}
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--epub', type=Path, action='append')
    parser.add_argument('--goldens', type=Path, default=ROOT / 'goldens')
    parser.add_argument('--skip-synthetic', action='store_true')
    parser.add_argument('action', choices=('record', 'verify'))
    args = parser.parse_args()
    cases = []
    for epub in args.epub or sorted((ROOT.parent / 'epubs').glob('*.epub')):
        with zipfile.ZipFile(epub) as archive:
            files = {n: archive.read(n) for n in archive.namelist() if not n.endswith('/')}
        cases.append((epub.stem, files, hashlib.sha256(epub.read_bytes()).hexdigest()))
    if not args.skip_synthetic:
        cases.extend(('synthetic-' + name, files, None) for name, files in synthetic())
    for name, files, sha in cases:
        with tempfile.TemporaryDirectory(prefix='package-oracle-') as temp:
            work = Path(temp)
            extracted = work / 'book'
            for relative, data in files.items():
                path = extracted / relative
                if not path.resolve().is_relative_to(extracted.resolve()):
                    raise ValueError(relative)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            result = {'source_sha256': sha, 'schedules': capture(args.oracle, extracted, work)}
        golden = args.goldens / (name + '.json.gz')
        if args.action == 'record':
            golden.parent.mkdir(parents=True, exist_ok=True)
            golden.write_bytes(gzip.compress(json.dumps(result, sort_keys=True).encode(), mtime=0))
        elif result != json.loads(gzip.decompress(golden.read_bytes())):
            raise AssertionError(f'package behavior changed: {name}')
        print(f'{args.action}: {name}: {len(result["schedules"])} schedules', flush=True)
    print(f'{len(cases)} cases, {len(cases) * len(CHUNKS) * 2} schedules')


if __name__ == '__main__':
    main()
