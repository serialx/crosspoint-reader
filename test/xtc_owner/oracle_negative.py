#!/usr/bin/env python3
"""Changing one expected semantic digest must be rejected without touching the golden."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
p=argparse.ArgumentParser();p.add_argument('executable');a=p.parse_args()
here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xtc-owner-negative-') as directory:
    path=Path(directory)/'mutated.txt.gz'
    rows=json.loads(gzip.decompress((here/'native_oracle/parent-results.txt.gz').read_bytes()))
    rows[0]['sha256']='0'*64
    path.write_bytes(gzip.compress(json.dumps(rows).encode(),mtime=0))
    r=subprocess.run([sys.executable,str(here/'run.py'),a.executable,'--expected',str(path)],capture_output=True,text=True)
    assert r.returncode!=0 and 'mismatch 1:0:0:0' in r.stdout, (r.returncode,r.stdout,r.stderr)
print('One altered expected digest rejected; immutable native output file unchanged')
