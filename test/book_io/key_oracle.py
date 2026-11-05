#!/usr/bin/env python3
"""BookKey wire bytes and platform-call order with deterministic crypto leaves."""
import argparse
import gzip
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser()
parser.add_argument('executable')
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
actual = subprocess.check_output([args.executable])
golden = Path(__file__).with_name('key-golden.txt.gz')
if args.capture:
    golden.write_bytes(gzip.compress(actual, mtime=0))
elif actual != gzip.decompress(golden.read_bytes()):
    raise SystemExit('BookKey bytes, result, or platform-call ordering differ from 0b70268b')
print(f'BookKey oracle: {actual.count(b"case ")} cases, {len(actual)} exact bytes')
