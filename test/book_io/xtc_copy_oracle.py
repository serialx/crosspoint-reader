#!/usr/bin/env python3
"""Check successful unscaled cover copies against original C++ 8d534a99."""
import gzip
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('fixtures', here.parent / 'xtc_diff/generate.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
output = bytearray()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'book.xtc'
    for depth in [1, 2]:
        for legacy in [False, True]:
            for width, height in [(9, 11), (16, 17)]:
                path.write_bytes(fixtures.book(depth, width, height, legacy))
                output += f'fixture {depth} {width} {height} {legacy}\n'.encode()
                output += subprocess.check_output([sys.argv[1], str(path)], timeout=20)
assert output == gzip.decompress((here / 'xtc-copy-golden.txt.gz').read_bytes()), 'Unscaled XTC copy trace differs'
print(f'PASS {output.count(b"copy case")} original-C++ copy cases, {len(output)} exact bytes')
