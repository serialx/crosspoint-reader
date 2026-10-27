"""A fixed-Huffman match cannot read history before the first output byte."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data)))


png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 0, 0, 0, 0))
       + chunk(b"IDAT", bytes.fromhex("78 9c 03 02 00 00 03 00 01"))
       + chunk(b"IEND", b""))
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "invalid-history.png"
    path.write_bytes(png)
    for crc in (0, 1):
        result = subprocess.run([sys.argv[1], str(path), "1", str(crc)],
                                check=True, capture_output=True, text=True, timeout=10)
        actual = json.loads(result.stdout)
        assert actual["open"] == 0, actual
        assert actual["decode"] == 2, actual  # PNG_DECODE_ERROR
        assert actual["rows"] == 0, actual
        print(f"CRC={crc}: rejected before emitting any row")
