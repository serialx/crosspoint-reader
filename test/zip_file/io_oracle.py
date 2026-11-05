#!/usr/bin/env python3
"""Compare ZIP memory/stream bytes, return values and HAL/sink call order."""
import argparse
import gzip
import io
from pathlib import Path
import subprocess
import tempfile
import zipfile
parser = argparse.ArgumentParser()
parser.add_argument('executable')
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
archive = io.BytesIO()
with zipfile.ZipFile(archive, 'w') as output:
    for name, kind in [('stored', zipfile.ZIP_STORED), ('deflated', zipfile.ZIP_DEFLATED)]:
        entry = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
        entry.compress_type = kind
        output.writestr(entry, b'hello ZIP\x00\xff\n' * 7)
data = archive.getvalue()
variants = [data, data[:-1], data[:22], data[:100], b'NOPE' + data[4:]]
actual = bytearray()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'book.zip'
    for number, variant in enumerate(variants):
        path.write_bytes(variant)
        actual.extend(f'fixture {number}\n'.encode())
        actual.extend(subprocess.check_output([args.executable, path], timeout=30))
assert b'result 1 84' in actual, 'valid memory extraction was not exercised'
assert b'write 84 84' in actual, 'valid stream output was not exercised'
golden = Path(__file__).with_name('io-golden.txt.gz')
if args.capture:
    golden.write_bytes(gzip.compress(bytes(actual), mtime=0))
elif bytes(actual) != gzip.decompress(golden.read_bytes()):
    raise SystemExit('ZIP bytes, results or I/O order differ from 0b70268b')
print(f'ZIP oracle: {actual.count(b"result ")} cases, {len(actual)} exact bytes')
