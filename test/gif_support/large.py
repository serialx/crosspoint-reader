#!/usr/bin/env python3
"""Generate large, public synthetic GIF workloads (Pillow required only here)."""
import argparse
from pathlib import Path
import random
import zipfile
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
rng = random.Random(702)
for name, width, height, interlace in [('photo', 1600, 2400, False), ('interlaced', 1200, 1600, True), ('lines', 3000, 900, False), ('pattern', 1200, 1600, False)]:
    image = Image.new('P', (width, height))
    image.putpalette([component for i in range(256) for component in (i, (i * 3) % 256, (i * 7) % 256)])
    if name == 'photo':
        data = bytes(((x // 6 + y // 9 + rng.randrange(16)) & 255) for y in range(height) for x in range(width))
    elif name == 'lines':
        data = bytes(255 if (x // 13 + y // 11) % 7 else 0 for y in range(height) for x in range(width))
    else:
        data = bytes((x // 4 + y // 3) & 255 for y in range(height) for x in range(width))
    image.frombytes(data)
    image.save(a.output / (name + '.gif'), interlace=interlace, optimize=False)
with zipfile.ZipFile(a.output / 'gif-benchmark.epub', 'w') as z:
    z.writestr('mimetype', 'application/epub+zip')
    z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    names = ['photo', 'interlaced', 'lines', 'pattern']
    manifest = ''.join(f'<item id="p{i}" href="{n}.xhtml" media-type="application/xhtml+xml"/><item id="g{i}" href="{n}.gif" media-type="image/gif"/>' for i,n in enumerate(names))
    spine = ''.join(f'<itemref idref="p{i}"/>' for i in range(len(names)))
    z.writestr('book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>GIF benchmark</dc:title><dc:identifier id="id">p7-gif</dc:identifier><dc:language>en</dc:language></metadata><manifest>{manifest}</manifest><spine>{spine}</spine></package>')
    for name in names:
        z.writestr(name + '.xhtml', f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{name}</title></head><body><p>{name}</p><img src="{name}.gif"/></body></html>')
        z.write(a.output / (name + '.gif'), name + '.gif')
