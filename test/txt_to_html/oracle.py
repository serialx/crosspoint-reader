#!/usr/bin/env python3
"""Compare exact TXT bytes and callback order with the pre-port capture."""
import argparse
import gzip
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("executable")
parser.add_argument("--capture", action="store_true")
args = parser.parse_args()
actual = subprocess.check_output([args.executable])
golden = Path(__file__).with_name("stream-golden.txt.gz")
if args.capture:
    golden.write_bytes(gzip.compress(actual, mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes())
    if actual != expected:
        raise SystemExit("TXT stream bytes or I/O trace differ from 0b70268b")
print(f"TXT oracle: {actual.count(b'case ')} cases, {len(actual)} exact bytes")
