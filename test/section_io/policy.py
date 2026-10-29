#!/usr/bin/env python3
"""Compare book layout signatures, link hits, dimensions, tags and entities."""
import argparse
import gzip
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--record', action='store_true')
args = parser.parse_args()
root = Path(__file__).parent
actual = subprocess.check_output([str(args.binary.resolve())], input=(root / 'entities.txt').read_bytes())
golden = root / 'policy.txt.gz'
if args.record:
    with gzip.open(golden, 'wb') as output:
        output.write(actual)
else:
    with gzip.open(golden, 'rb') as source:
        expected = source.read()
    if actual != expected:
        Path('/tmp/book-policy-actual.txt').write_bytes(actual)
        raise SystemExit('policy mismatch; actual at /tmp/book-policy-actual.txt')
print(f'PASS {len(actual.splitlines())} policy observations')
