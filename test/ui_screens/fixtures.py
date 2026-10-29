"""Small public, synthetic fixtures generated outside the source tree."""
import importlib.util
import struct
import zipfile
import zlib
from pathlib import Path


def epub(path):
    files = {
        'mimetype': 'application/epub+zip',
        'META-INF/container.xml': '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'book.opf': '''<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">ui-oracle</dc:identifier><dc:title>UI Oracle</dc:title><dc:creator>CrossPoint Test</dc:creator><dc:language>en</dc:language></metadata><manifest><item id="nav" href="nav.xhtml" properties="nav" media-type="application/xhtml+xml"/><item id="one" href="one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="two.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="one"/><itemref idref="two"/></spine></package>''',
        'nav.xhtml': '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol><li><a href="one.xhtml">First chapter</a></li><li><a href="two.xhtml">Last chapter</a></li></ol></nav></body></html>',
        'one.xhtml': '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><h1>Oracle</h1><p>Oracle reader screen with a <a epub:type="noteref" href="two.xhtml#note">footnote</a> and <a epub:type="noteref" href="two.xhtml#note2">second note</a>.</p>' + '<p>Oracle text for selection, bookmarks, clippings and dictionary lookup. This deterministic paragraph has no private content.</p>' * 8 + '</body></html>',
        'two.xhtml': '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><h1>Last chapter</h1><aside epub:type="footnote" id="note"><p>Oracle footnote definition.</p></aside><p>The end of the test book.</p><aside epub:type="footnote" id="note2"><p>Second oracle note.</p></aside></body></html>',
    }
    with zipfile.ZipFile(path, 'w') as archive:
        for name, value in files.items():
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED
            archive.writestr(info, value)


def bitmap(path, overlay=False):
    width, height = 96, 128
    channels = 4 if overlay else 3
    size = width * height * channels
    header = struct.pack('<2sIHHI', b'BM', 54 + size, 0, 0, 54)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, channels * 8, 0, size, 2835, 2835, 0, 0)
    data = bytearray()
    for y in range(height):
        for x in range(width):
            value = ((x // 24 + y // 32) % 4) * 85
            data.extend([value] * 3)
            if overlay:
                data.append(128 if x < width // 2 else 255)
    path.write_bytes(header + data)


def cover_png(path):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    width, height = 96, 128
    rows = b''.join(b'\0' + bytes(((x // 24 + y // 32) % 4) * 85 for x in range(width))
                    for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def prepare(case, kind):
    root = case / 'assets'
    root.mkdir()
    if kind.startswith('entry-'):
        name = kind.removeprefix('entry-')
        allowed = {'details-plain', 'details-html', 'details-cover', 'frontlight-mono', 'frontlight-warm',
                   'unknown', 'oversized', 'settings-home-button'}
        update_cases = {f'upd-{family}-{phase}' for family, phases in
                        [('ota', range(1, 7)), ('sd', range(6)), ('cache', range(4)), ('font', range(1, 7))]
                        for phase in phases}
        if name not in allowed | update_cases:
            raise ValueError(f'Unknown activity fixture: {kind}')
        selector = root / 'ui-oracle-case.txt'
        selector.write_text('x' * 32 if name == 'oversized' else name)
        assets = [(selector, '/.crosspoint')]
        if name == 'details-cover':
            cover_png(root / 'OracleCover.png')
            assets.append((root / 'OracleCover.png', '/'))
        return assets
    if kind == 'empty':
        return []
    if kind == 'sleep-overlay':
        bitmap(root / 'sleep-overlay.bmp', overlay=True)
        return [(root / 'sleep-overlay.bmp', '/')]
    if kind == 'plugin':
        plugin = root / 'oracle'
        plugin.mkdir()
        (plugin / 'manifest.json').write_text('{"title":"Oracle Plugin","description":"Offline UI fixture"}\n')
        (plugin / 'README.md').write_text('Oracle plugin instructions.\n\nThis synthetic plugin has no executable code or network requests.\n')
        return [(plugin, '/.crosspoint/plugins')]
    if kind in ('reader', 'reader-next', 'files', 'library'):
        epub(root / 'Oracle.epub')
    if kind in ('reader', 'reader-next'):
        dictionary = root / 'Oracle'
        dictionary.mkdir()
        words = sorted(set('oracle reader screen with a footnote and second note text for selection bookmarks clippings and dictionary lookup this deterministic paragraph has no private content'.split()))
        idx = bytearray()
        definitions = bytearray()
        for word in words:
            definition = (word + ': a deterministic test definition.').encode()
            idx.extend(word.encode() + b'\0' + struct.pack('>II', len(definitions), len(definition)))
            definitions.extend(definition)
        (dictionary / 'Oracle.idx').write_bytes(idx)
        (dictionary / 'Oracle.dict').write_bytes(definitions)
        (dictionary / 'Oracle.ifo').write_text("StarDict's dict ifo file\nversion=2.4.2\nbookname=Oracle\nwordcount=" + str(len(words)) + '\nidxfilesize=' + str(len(idx)) + '\nsametypesequence=m\n')
        assets = [(root / 'Oracle.epub', '/'), (dictionary, '/.dictionaries')]
        if kind == 'reader-next':
            epub(root / 'ZZNext.epub')
            assets.append((root / 'ZZNext.epub', '/'))
        return assets
    if kind in ('image', 'sleep-custom'):
        if kind == 'sleep-custom':
            bitmap(root / 'sleep.bmp')
            return [(root / 'sleep.bmp', '/')]
        bitmap(root / 'Image.bmp')
    if kind == 'broken-book':
        (root / 'Broken.epub').write_bytes(b'not a zip')
    if kind == 'firmware':
        (root / 'Invalid.bin').write_bytes(b'not firmware')
    if kind == 'xtc':
        spec = importlib.util.spec_from_file_location('xtc_fixture', Path(__file__).resolve().parents[1] / 'xtc_diff/generate.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        (root / 'Oracle.xtc').write_bytes(module.book(width=480, height=800))
    return [(p, '/') for p in sorted(root.iterdir())]
