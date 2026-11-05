#!/usr/bin/env python3
"""TXT metadata transaction and companion-cover order."""
import argparse
import gzip
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser()
parser.add_argument('executable')
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
actual = subprocess.check_output([args.executable])
golden = Path(__file__).with_name('txt-facade-golden.txt.gz')
if args.capture:
    golden.write_bytes(gzip.compress(actual, mtime=0))
elif actual != gzip.decompress(golden.read_bytes()):
    raise SystemExit('TXT facade bytes, result, or platform-call ordering differ from 0b70268b')
print(f'TXT facade oracle: {actual.count(b"case ")} cases, {len(actual)} exact bytes')
