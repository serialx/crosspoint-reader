#!/usr/bin/env python3
"""Original XTC cache, title, path and wrapping-progress decisions."""
import argparse
import gzip
import importlib.util
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path)
p.add_argument('--record',action='store_true')
a=p.parse_args()
here=Path(__file__).parent
spec=importlib.util.spec_from_file_location('fixtures',here.parent/'xtc_diff/generate.py')
fixtures=importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
output=bytearray()
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'book.xtc'
    for legacy in [False,True]:
        path.write_bytes(fixtures.book(legacy=legacy))
        output+=subprocess.check_output([str(a.binary.resolve()),str(path),'residual'])
golden=here/'xtc-residual-golden.txt.gz'
if a.record:
    golden.write_bytes(gzip.compress(output,mtime=0))
else:
    assert output==gzip.decompress(golden.read_bytes()),'XTC residual trace differs'
print(f'PASS {len(output.splitlines())} original XTC residual trace lines')
