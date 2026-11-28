"""Extract public PNG IDAT streams; system zlib supplies the expected bytes."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
import tarfile
import zlib

ROOT = Path(__file__).resolve().parent
out = bytearray()
rows = []
with tarfile.open(ROOT.parent / "image_golden/corpus.tar.gz") as archive:
    for member in sorted(archive.getmembers(), key=lambda entry: entry.name):
        if not member.isfile():
            continue
        png = archive.extractfile(member).read()
        if not png.startswith(b"\x89PNG\r\n\x1a\n"):
            continue
        pos, pitch = 8, 0
        idat = bytearray()
        while pos + 12 <= len(png):
            size = struct.unpack_from(">I", png, pos)[0]
            tag, chunk = png[pos + 4:pos + 8], png[pos + 8:pos + 8 + size]
            if len(chunk) != size:
                break
            if tag == b"IHDR":
                width, _, depth, color, _, _, _ = struct.unpack(">IIBBBBB", chunk)
                pitch = (width * depth * {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color] + 7) // 8 + 1
            if tag == b"IDAT":
                idat.extend(chunk)
            pos += size + 12
        if not idat:
            continue
        try:
            plain = zlib.decompress(idat)
        except zlib.error:
            continue
        rows.append(dict(name=member.name, pitch=pitch, compressed=len(idat),
                         output=len(plain), sha256=hashlib.sha256(idat).hexdigest()))
        for data in [member.name.encode(), idat, plain]:
            out += struct.pack("<I", len(data)) + data
        out += struct.pack("<II", pitch, zlib.crc32(idat))
(ROOT / "fixtures/png-idat.bin.gz").write_bytes(gzip.compress(out, mtime=0))
(ROOT / "fixtures/png-idat.json").write_text(json.dumps(rows, indent=2) + "\n")
print(f"{len(rows)} IDAT streams, {len(out)} expanded bytes; zlib {zlib.ZLIB_RUNTIME_VERSION}")
