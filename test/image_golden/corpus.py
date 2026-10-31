#!/usr/bin/env python3
"""Generate public test images; Pillow is needed only when changing the corpus."""
import hashlib
import io
from pathlib import Path
import struct
import sys
import tarfile
import zipfile
import zlib
from PIL import Image, ImageFile
ImageFile.MAXBLOCK = 16 * 1024 * 1024


def png(width, height, depth, color, raw, extra=b'', interlace=0):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, interlace))
            + extra + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def generate():
    images = {}
    for depth in (1, 2, 4, 8, 16):
        w, h = 19, 23
        rows = bytearray()
        for y in range(h):
            rows.append(0)
            samples = [(x * 7 + y * 11) % (1 << depth) for x in range(w)]
            if depth == 16:
                rows.extend(b''.join(struct.pack('>H', s * 97 % 65536) for s in samples))
            else:
                bits = ''.join(f'{s:0{depth}b}' for s in samples)
                bits += '0' * (-len(bits) % 8)
                rows.extend(int(bits[i:i+8], 2) for i in range(0, len(bits), 8))
        images[f'gray-{depth}.png'] = png(w, h, depth, 0, rows)
    for size in ((1, 1), (3, 7), (37, 29), (801, 601)):
        w, h = size
        rgb = Image.new('RGB', size)
        rgb.putdata([((x * 31 + y * 7) % 256, (x * 17 + y * 23) % 256, (x * 3 + y * 53) % 256)
                     for y in range(h) for x in range(w)])
        if w > 100:
            rgb = rgb.resize((51, 39)).resize(size)
        for subsampling in (0, 2):
            for progressive in (False, True):
                out = io.BytesIO()
                rgb.save(out, format='JPEG', quality=83, subsampling=subsampling, progressive=progressive)
                images[f'rgb-{w}x{h}-{subsampling}-{int(progressive)}.jpg'] = out.getvalue()
        if w > 100: continue
        for mode in ('RGB', 'RGBA', 'L', 'LA', 'P'):
            im = rgb.convert(mode)
            if mode in ('RGBA', 'LA'):
                im.putalpha(Image.frombytes('L', size, bytes((x * 19 + y * 37) % 256 for y in range(h) for x in range(w))))
            out = io.BytesIO()
            im.save(out, format='PNG')
            images[f'{mode}-{w}x{h}.png'] = out.getvalue()
        for bits in (1, 2, 4, 8):
            im = rgb.quantize(colors=1 << bits)
            out = io.BytesIO()
            im.save(out, format='PNG', bits=bits, transparency=bytes(range(1 << bits)))
            images[f'indexed-{bits}-{w}x{h}.png'] = out.getvalue()
        for interlaced in (False, True):
            for transparent in (False, True):
                out = io.BytesIO()
                rgb.save(out, format='GIF', interlace=interlaced, **({'transparency': 0} if transparent else {}))
                images[f'gif-{w}x{h}-{int(interlaced)}-{int(transparent)}.gif'] = out.getvalue()
        out = io.BytesIO()
        rgb.save(out, format='GIF', save_all=True, append_images=[rgb.transpose(Image.Transpose.FLIP_LEFT_RIGHT)], duration=100, loop=0)
        images[f'animated-{w}x{h}.gif'] = out.getvalue()
        out = io.BytesIO()
        rgb.convert('L').save(out, format='JPEG')
        images[f'gray-{w}x{h}.jpg'] = out.getvalue()
    im = Image.new('CMYK', (37, 29))
    im.putdata([((x * 31) % 256, (y * 53) % 256, (x * 7 + y * 11) % 256, (x * 3 + y * 19) % 256)
                for y in range(29) for x in range(37)])
    out = io.BytesIO()
    im.save(out, format='JPEG', quality=83)
    images['cmyk-37x29.jpg'] = out.getvalue()
    # Decoder rejection and header validation are observable outputs too.
    images['interlace.png'] = png(1, 1, 8, 0, b'\0\x7f', interlace=1)
    images['oversize.png'] = png(32768, 1, 8, 0, b'\0\0')
    for ext in ('png', 'jpg', 'gif'):
        images[f'truncated.{ext}'] = b'\x89PNG\r\n' if ext == 'png' else b'GIF89a' if ext == 'gif' else b'\xff\xd8\xff\xe1\x00\x20'
    root = Path(__file__).resolve().parents[1] / 'epubs'
    seen = set()
    for epub in sorted(root.glob('*.epub')):
        with zipfile.ZipFile(epub) as book:
            for name in book.namelist():
                ext = Path(name).suffix.lower()
                if ext not in ('.png', '.jpg', '.jpeg', '.gif'): continue
                data = book.read(name)
                digest = hashlib.sha256(data).hexdigest()
                if digest in seen: continue
                seen.add(digest)
                images[f'epub-{digest[:16]}{ext}'] = data
    return images


if __name__ == '__main__':
    with tarfile.open(sys.argv[1], 'w:gz') as archive:
        for name, data in sorted(generate().items()):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
