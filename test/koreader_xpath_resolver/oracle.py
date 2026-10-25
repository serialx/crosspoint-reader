#!/usr/bin/env python3
"""Capture with the unmodified C++ resolver; verify without rewriting expectations."""
import argparse
import ast
import gzip
import hashlib
import json
from pathlib import Path
import posixpath
import re
import subprocess
import tempfile
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
GOLDEN = HERE / 'goldens.json.gz'


def inputs():
    source = (HERE / 'KOReaderXPathResolverTest.cpp').read_text()
    token = r'//[^\n]*|R"\((.*?)\)"|"((?:[^"\\]|\\.)*)"'
    pieces = list(re.finditer(token, source, re.S))
    cases = []
    last_end = -1
    for match in pieces:
        if match.group(0).startswith('//'):
            continue
        if match.group(1) is not None:
            value = match.group(1).encode()
        else:
            value = ast.literal_eval('b"' + match.group(2) + '"')
        if cases and source[last_end:match.start()].strip() == '':
            cases[-1] += value
        else:
            cases.append(value)
        last_end = match.end()
    seen = set()
    for data in cases:
        if (data.startswith(b'<') or not data) and data not in seen:
            seen.add(data)
            yield f'existing/{len(seen):02}', data, 0, (1, 7, 512, 1024)
    special = {
        'hidden': '<html><body><p>A<script><rp><![CDATA[hidden]]><?p x?><!--c--></rp></script>B<style><b>x</b></style>C<rt><i>x</i></rt>D</p></body></html>',
        'boundaries': '<html><body> \n<p>  <?pi x?>\t<!--c-->\n<![CDATA[ ]]><![CDATA[가é😀]]>  <a/>\r\n&#32;Z&amp;Q</p> \t</body></html>',
        'prefix-case': '<x:html xmlns:x="u"><x:body><x:p>A</x:p><p>B</p><P>C</P><x:li>D</x:li></x:body></x:html>',
        'early-stop': '<html><body><p>A</p><broken &',
        'early-empty': '<html><body><p/><broken &',
        'whitespace-entities': '<html><body><p> &#32;&#10;A\n B&#13;C\r\nD</p></body></html>',
        'declared-entity': '<!DOCTYPE html [<!ENTITY a "hello">]><html><body><p>A&a;B</p></body></html>',
        'large-text': '<html><body><p>' + ('a é中😀 \n' * 240) + '<!--c-->tail</p></body></html>',
        'chunk-whitespace': '<html><body><p>' + ' ' * 1010 + 'abc\n   def</p></body></html>',
        'cdata-chunks': '<html><body><p><![CDATA[' + ('x ] é\r\n' * 220) + ']]>tail</p></body></html>',
    }
    for name, value in special.items():
        yield 'hand/' + name, value.encode(), 2, (1, 7, 512, 1024)
    for length in (990, 1000, 1001, 1008, 1009, 1010, 1011, 1012, 1015, 1024, 2000, 2048, 4096, 8192):
        data = ('<html><body><p x="' + 'a' * length + '">   A</p><p>B</p></body></html>').encode()
        yield f'hand/long-attribute-{length}', data, 0, (1, 7, 512, 1024)
    for encoding in ('utf-16le', 'utf-16be', 'iso-8859-1'):
        declared = 'UTF-16' if encoding.startswith('utf-16') else 'ISO-8859-1'
        text = f'<?xml version="1.0" encoding="{declared}"?><html><body><p> a\r\né<!--c--> z</p></body></html>'
        data = text.encode(encoding)
        if encoding == 'utf-16le':
            data = b'\xff\xfe' + data
        elif encoding == 'utf-16be':
            data = b'\xfe\xff' + data
        yield 'hand/' + encoding, data, 0, (1, 7, 512, 1024)
    for epub in sorted((REPO / 'test/epubs').glob('*.epub')):
        with zipfile.ZipFile(epub) as z:
            root = ET.fromstring(z.read('META-INF/container.xml'))
            opf = next(e.attrib['full-path'] for e in root.iter() if e.tag.endswith('rootfile'))
            package = ET.fromstring(z.read(opf))
            manifest = {e.attrib['id']: e.attrib for e in package.iter() if e.tag.endswith('}item')}
            for index, item in enumerate(e for e in package.iter() if e.tag.endswith('}itemref')):
                entry = manifest[item.attrib['idref']]
                if entry.get('media-type') != 'application/xhtml+xml':
                    continue
                path = posixpath.normpath(posixpath.join(posixpath.dirname(opf), urllib.parse.unquote(entry['href'].split('#')[0])))
                yield f'{epub.name}/{index}/{path}', z.read(path), index, (1024,)


def queries(data):
    paragraphs = sorted(set(range(11)) | {16, 32, 64, 128, 256, 1024, 65535})
    offsets = sorted(set(range(40)) | {63, 64, 127, 128, 255, 256, 511, 512, 1023, 1024, 1025, 2047, 2048, 4294967295} |
                     {len(data) * n // 16 for n in range(17)})
    return ([f'p {n}' for n in paragraphs] + [f'o {n}' for n in offsets] +
            [f'g {n}' for n in ('-1', '0', '0.001', '0.01', '0.1', '0.25', '0.5', '0.75', '0.99', '1', '1.1')])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--oracle', required=True)
    parser.add_argument('mode', choices=['record', 'verify'])
    args = parser.parse_args()
    expected = json.loads(gzip.decompress(GOLDEN.read_bytes())) if args.mode == 'verify' else None
    actual = []
    failures = 0
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'input.xhtml'
        for name, data, spine, chunks in inputs():
            path.write_bytes(data)
            q = queries(data)
            for chunk in chunks:
                run = subprocess.run([args.oracle, str(path), str(spine), str(chunk)],
                                     input='\n'.join(q) + '\n', text=True, capture_output=True, check=True)
                results = run.stdout.splitlines()
                assert len(results) == len(q), (name, run.stdout, run.stderr)
                row = dict(name=name, sha256=hashlib.sha256(data).hexdigest(), spine=spine, chunk=chunk, queries=q, results=results)
                if expected is not None and row != expected[len(actual)]:
                    old = expected[len(actual)]
                    assert {k: v for k, v in row.items() if k != 'results'} == {k: v for k, v in old.items() if k != 'results'}, name
                    for query, before, after in zip(q, old['results'], results):
                        if before != after:
                            if failures < 30:
                                print(f'{name} chunk={chunk} {query}: {before!r} -> {after!r}')
                            failures += 1
                actual.append(row)
    if expected is None:
        GOLDEN.write_bytes(gzip.compress(json.dumps(actual, separators=(',', ':')).encode(), mtime=0))
    else:
        assert len(actual) == len(expected)
    print(f'{len(actual)} input schedules, {sum(len(r["queries"]) for r in actual)} results, {failures} differences; fixture {GOLDEN.stat().st_size} bytes')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
