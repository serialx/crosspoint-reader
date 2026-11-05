#!/usr/bin/env python3
"""Public EPUB text and the hand-written Arabic regression inputs, as hex bytes."""
import ast
import argparse
import runpy
from pathlib import Path
import re
import zipfile
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
ap.add_argument('--text-oracle', type=Path, help='optional P5-T0 test/text_golden/run.py')
args = ap.parse_args()


class Text(HTMLParser):
    def __init__(self):
        super().__init__()
        self.chunks = []

    def handle_data(self, data):
        for line in data.splitlines():
            words = line.split()
            for i in range(0, len(words), 12):
                self.chunks.append(' '.join(words[i:i + 12]).encode())


cases = set()
src = (ROOT / 'test/minibidi_arabic/MiniBidiArabicTest.cpp').read_text()
for match in re.findall(r'shapeVisual\(\{([^}]+)\}', src):
    codepoints = [ast.literal_eval(x.strip()) for x in match.split(',')]
    cases.add(''.join(chr(x) if isinstance(x, int) else x for x in codepoints).encode())
for epub in sorted((ROOT / 'test').rglob('*.epub')):
    with zipfile.ZipFile(epub) as archive:
        for name in archive.namelist():
            if name.lower().endswith(('.xhtml', '.html', '.htm')):
                parser = Text()
                parser.feed(archive.read(name).decode('utf-8', errors='replace'))
                cases.update(parser.chunks)
if args.text_oracle:
    oracle = runpy.run_path(str(args.text_oracle))
    words, contexts, _, _ = oracle['corpus']()
    cases.update(oracle['packed'](word) for word in words)
    cases.update(oracle['packed'](value) for value in oracle['SYNTHETIC'].values())
    cases.update((chr(a) + ' ' + chr(b)).encode() for a, b in contexts)
args.output.write_text(''.join(s.hex() + '\n' for s in sorted(cases)))
print(f'Bidi corpus: {len(cases)} unique strings')
