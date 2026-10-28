#!/usr/bin/env python3
import gzip
import pathlib
import subprocess
import sys
archive = pathlib.Path(__file__).with_name('reverse-golden.txt.gz')
actual = subprocess.check_output([sys.argv[1]])
if len(sys.argv) > 2 and sys.argv[2] == 'capture':
    archive.write_bytes(gzip.compress(actual, mtime=0))
else:
    expected = gzip.decompress(archive.read_bytes())
    if actual != expected:
        for i, (a, b) in enumerate(zip(actual.splitlines(), expected.splitlines())):
            if a != b:
                print(f'first difference at {i + 1}: {a!r} != {b!r}')
                break
        raise SystemExit('reverse mapping golden mismatch')
    print(f'Reverse mapping: {len(actual.splitlines())} mutation cases identical')
