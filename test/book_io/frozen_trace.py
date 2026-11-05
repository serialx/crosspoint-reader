#!/usr/bin/env python3
"""Compare an isolated production facade with its pre-port trace."""
import argparse
import gzip
from pathlib import Path
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('binary', type=Path)
p.add_argument('golden', type=Path)
p.add_argument('--record', action='store_true')
a = p.parse_args()
result = subprocess.check_output([str(a.binary.resolve())])
if a.record:
    with gzip.open(a.golden, 'wb') as f:
        f.write(result)
else:
    with gzip.open(a.golden, 'rb') as f:
        assert result == f.read(), 'production trace differs from pre-port C++'
print(f'PASS {len(result.splitlines())} trace lines')
