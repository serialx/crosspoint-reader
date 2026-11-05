#!/usr/bin/env python3
"""Generate small synthetic XTC/XTCH files; no private fixture content."""
import argparse
import pathlib
import struct


def book(depth=1, width=9, height=11, legacy=False):
    chapters = bytearray()
    for name, start, end in [(b'First', 1, 1), (b'Last', 2, 3)]:
        chapters += struct.pack('<80sHH12x', name, start, end)
    table = 48 if legacy else 256 + len(chapters)
    data_offset = table + 3 * 16
    header = struct.pack('<4sBBHBBBBIQQQQII', b'XTCH' if depth == 2 else b'XTC\0',
                         1, 0, 3, 0, not legacy, 0, not legacy, 1,
                         56 if not legacy else 0, table, data_offset, 0, 256, 0)
    prefix = bytearray(header[:48] if legacy else header + bytes(200))
    if not legacy:
        prefix[56:56+15] = b'Synthetic title'
        prefix[184:184+16] = b'Synthetic author'
        prefix += chapters
    entries = bytearray()
    pages = bytearray()
    size = width * ((height + 7) // 8) * 2 if depth == 2 else ((width + 7) // 8) * height
    for index in range(3):
        payload = bytes((i * 37 + index * 11) % 256 for i in range(size))
        page = struct.pack('<4sHHBBIQ', b'XTH\0' if depth == 2 else b'XTG\0', width, height, 0, 0, size, 0) + payload
        entries += struct.pack('<QIHH', data_offset + len(pages), len(page), width, height)
        pages += page
    return bytes(prefix + entries + pages)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=pathlib.Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    for depth in (1, 2):
        for width, height in ((0, 0), (0, 9), (9, 0), (1, 1), (8, 8), (9, 11), (16, 17)):
            for legacy in (False, True):
                name = f'synthetic-{depth}-{width}-{height}-{int(legacy)}.xtc'
                (args.directory / name).write_bytes(book(depth, width, height, legacy))

    for depth in (1, 2):
        data = bytearray(book(depth))
        table = struct.unpack_from('<Q', data, 24)[0]
        data[table + 16:table + 32] = data[table:table + 16]
        (args.directory / f'synthetic-overlap-{depth}.xtc').write_bytes(data)


if __name__ == '__main__':
    main()
