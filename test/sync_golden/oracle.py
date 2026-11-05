#!/usr/bin/env python3
import gzip
import pathlib
import subprocess
import sys
p=pathlib.Path(__file__).with_name('golden.txt.gz')
actual=subprocess.check_output([sys.argv[1]])
if len(sys.argv)>2 and sys.argv[2]=='capture':
    p.write_bytes(gzip.compress(actual,mtime=0))
else:
    expected=gzip.decompress(p.read_bytes())
    if actual!=expected:
        a=actual.splitlines();e=expected.splitlines()
        for i,(x,y) in enumerate(zip(a,e)):
            if x!=y:
                print(f'first difference line {i+1}: actual={x!r} expected={y!r}')
                break
        raise SystemExit(f'golden mismatch ({len(actual)} / {len(expected)} bytes)')
    print(f'Sync golden: {len(actual.splitlines())} lines identical')
