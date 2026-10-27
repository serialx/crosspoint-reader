"""Deterministic PNG boundary corpus; no Pillow or private input required."""
import struct
import zlib


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def png(width=7, height=5, depth=8, color=2, interlace=0, method=0, filtering=0):
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color, 1)
    pitch = (width * channels * depth + 7) // 8
    # Bound malformed-dimension generation; only the header needs those values.
    rows = b''.join(b'\0' + bytes((x * 17 + y * 31) % 256 for x in range(min(pitch, 16416)))
                    for y in range(min(height, 5)))
    header = struct.pack('>IIBBBBB', width, height, depth, color, method, filtering, interlace)
    palette = chunk(b'PLTE', bytes(range(256)) * 3) + chunk(b'tRNS', bytes(range(256))) if color == 3 else b''
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + palette + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def cases():
    result = {}
    for color in (0, 2, 3, 4, 6):
        for depth in (1, 2, 4, 8, 16):
            result[f'format-{color}-{depth}'] = png(depth=depth, color=color)
    for color in (0, 2, 3, 4, 6):
        base = png(color=color)
        for n in range(len(base)):
            result[f'truncate-{color}-{n}'] = base[:n]
        for i in range(len(base)):
            value = bytearray(base)
            value[i] ^= 1 << (i % 8)
            result[f'flip-{color}-{i}'] = bytes(value)
    for w in (0, 1, 2048, 2730, 2731, 8191, 8192, 8208, 16384, 32767, 32768, 0xffffffff):
        result[f'width-{w}'] = png(width=w, height=1)
    for h in (0, 1, 3072, 32767, 32768, 0xffffffff):
        result[f'height-{h}'] = png(height=h)
    for color in (1, 5, 7, 255):
        result[f'color-{color}'] = png(color=color)
    for depth in (0, 3, 5, 7, 9, 255):
        result[f'depth-{depth}'] = png(depth=depth)
    for interlace in (1, 2, 255):
        result[f'interlace-{interlace}'] = png(interlace=interlace)
    result['compression'] = png(method=1)
    result['filter-method'] = png(filtering=1)
    base = png()
    for kind in (b'IHDR', b'PLTE', b'tRNS', b'IDAT', b'IEND', b'ABCD', b'abCD'):
        for size in (0, 1, 2, 6, 13, 255, 256, 768, 769, 1024, 2048, 0x7fffffff, 0x80000000, 0xffffffff):
            data = struct.pack('>I', size) + kind + bytes(min(size, 2048)) + b'\0' * 4
            result[f'chunk-{kind.decode()}-{size}'] = base[:33] + data + base[33:]
    for filter_byte in (0, 1, 2, 3, 4, 5, 255):
        rows = b''.join(bytes([filter_byte]) + bytes((x * 19 + y * 31) % 256 for x in range(21)) for y in range(5))
        result[f'filter-{filter_byte}'] = base[:33] + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    for name, data in {'invalid-history': '789c03020000030001', 'distance-two': '789c7304420000000000',
                       'reserved-distance': '789c033e0000000000', 'dictionary': '782000000001'}.items():
        result[name] = base[:33] + chunk(b'IDAT', bytes.fromhex(data)) + chunk(b'IEND', b'')
    header = struct.pack('>IIBBBBB', 1, 1000, 8, 0, 0, 0, 0)
    result['aspect-tall'] = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
                             + chunk(b'IDAT', zlib.compress(b'\0\x80' * 1000)) + chunk(b'IEND', b''))
    return result
