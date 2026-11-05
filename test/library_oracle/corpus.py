#!/usr/bin/env python3
"""Build a host-only metadata manifest; private book bytes never enter the repo."""
import pathlib
import sys
import xml.etree.ElementTree as ET
import zipfile

for root in map(pathlib.Path, sys.argv[1:]):
    paths = [root] if root.is_file() else sorted(root.rglob('*.epub'))
    for path in paths:
        try:
            with zipfile.ZipFile(path) as archive:
                container = ET.fromstring(archive.read('META-INF/container.xml'))
                opf = next(e.attrib['full-path'] for e in container.iter() if e.tag.endswith('rootfile'))
                package = ET.fromstring(archive.read(opf))
                metadata = next(e for e in package.iter() if e.tag.endswith('}metadata') or e.tag == 'metadata')
                title = next((''.join(e.itertext()) for e in metadata if e.tag.endswith('}title')), '')
                author = next((''.join(e.itertext()) for e in metadata if e.tag.endswith('}creator')), '')
            virtual = '/Books/' + path.name
            print(virtual.encode().hex(), path.stat().st_size, 1, title.encode().hex() or '-', author.encode().hex() or '-')
        except (KeyError, StopIteration, ET.ParseError, zipfile.BadZipFile) as error:
            print(f'{path}: {error}', file=sys.stderr)
