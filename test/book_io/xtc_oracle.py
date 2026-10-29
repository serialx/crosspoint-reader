#!/usr/bin/env python3
"""Freeze XTC parser/file failures and cover bytes from 0b70268b."""
import argparse
import gzip
import importlib.util
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('executable')
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('xtc_fixtures', here.parent / 'xtc_diff/generate.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
result = bytearray()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'book.xtc'
    for depth in (1, 2):
        for width, height in ((9, 11), (16, 17)):
            for legacy in (False, True):
                original = fixtures.book(depth, width, height, legacy)
                variants = [original, original[:20], original[:-1], b'NOPE' + original[4:]]
                for number, data in enumerate(variants):
                    path.write_bytes(data)
                    result.extend(f'fixture {depth} {width} {height} {legacy} {number}\n'.encode())
                    result.extend(subprocess.check_output([args.executable, path], timeout=20))
golden = here / 'xtc-golden.txt.gz'
if args.capture:
    golden.write_bytes(gzip.compress(bytes(result), mtime=0))
elif bytes(result) != gzip.decompress(golden.read_bytes()):
    raise SystemExit('XTC result, I/O trace, or output bytes differ from 0b70268b')
print(f'XTC oracle: {result.count(b"case ")} cases, {len(result)} exact bytes')
