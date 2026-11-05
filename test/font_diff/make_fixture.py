#!/usr/bin/env python3
"""Write a public, synthetic four-style cpfont v4; no external font data."""
import struct
import sys
from pathlib import Path


def generate(path):
    intervals = [(0x20, 0x7e), (0x2018, 0x2018), (0xfb00, 0xfb01),
                 (0xfb03, 0xfb03), (0xfffd, 0xfffd), (0x10000, 0x10000)]
    pairs = [(ord('f') << 16 | ord('f'), 0xfb00),
             (ord('f') << 16 | ord('i'), 0xfb01), (0xfb00 << 16 | ord('i'), 0xfb03)]
    toc, sections = bytearray(), bytearray()
    for style in range(4):
        ivs, glyphs, bitmap = bytearray(), bytearray(), bytearray()
        count = 0
        for first, last in intervals:
            ivs += struct.pack('<III', first, last, count)
            for cp in range(first, last + 1):
                advance = 0 if cp == ord('Z') else (100 + cp % 37 + style)
                glyphs += struct.pack('<BBHhhH2xI', 4, 4, advance, -1, 4 + style, 4, len(bitmap))
                bitmap += bytes([cp % 256]) * 4
                count += 1
        left = struct.pack('<HBHB', ord('A'), 1, ord('V'), 2)
        right = struct.pack('<HBHB', ord('A'), 2, ord('V'), 1)
        kern = struct.pack('<bbbb', -8 - style, 0, 7, -128)
        lig = b''.join(struct.pack('<II', *p) for p in pairs)
        toc += struct.pack('<B3xIIBhhHHBBBI4x', style, len(intervals), count,
                           12, 8, -2, 2, 2, 2, 2, len(pairs), 160 + len(sections))
        sections += ivs + glyphs + left + right + kern + lig + bitmap
    Path(path).write_bytes(struct.pack('<8sHHB19x', b'CPFONT\0\0', 4, 1, 4) + toc + sections)


if __name__ == '__main__':
    generate(sys.argv[1])
