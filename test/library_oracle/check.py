#!/usr/bin/env python3
"""Compare every serialized byte with the pre-port C++ result."""
import gzip
import pathlib
import subprocess
import sys

expected = gzip.decompress(pathlib.Path(__file__).with_name('golden.bin.gz').read_bytes())
actual = subprocess.check_output([sys.argv[1]])
if actual != expected:
    at = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), min(len(actual), len(expected)))
    raise SystemExit(f'Library oracle differs at byte {at}: got {len(actual)} bytes, expected {len(expected)}')
print(f'Library oracle: {len(actual)} bytes identical')
