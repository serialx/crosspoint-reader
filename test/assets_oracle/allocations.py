#!/usr/bin/env python3
"""Every allocation failure must preserve a frozen outcome and release Rust owners."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
results={}
stats={}
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    with tarfile.open(Path(__file__).parents[1]/'image_golden/corpus.tar.gz') as archive:
        archive.extractall(root,filter='data')
    for image in sorted(root.glob('*37x29*.jpg')):
        for variant in [0,4,7,8]:
            values=[]
            counts=[]
            for fail in range(12):
                result=subprocess.run([sys.argv[1],'jpeg',str(image),str(variant),str(2**64-1)],capture_output=True,check=True,
                    env={**os.environ,'IMAGE_DIAGNOSTIC_LOGS':'1','ASSET_FAIL_ALLOC':str(fail)})
                logs,allocation=result.stderr.decode().rsplit('ALLOC ',1)
                attempts,live=map(int,allocation.split())
                if live: raise RuntimeError(f'leaked {live} Rust owners')
                values.append((hashlib.sha256(result.stdout).hexdigest(),logs))
                counts.append(attempts)
            key=f'{image.name}/{variant}'
            results[key]=sorted(set(values))
            stats[key]=counts
path=Path(__file__).with_name('allocation-goldens.json.gz')
if '--capture' in sys.argv:
    with gzip.open(path,'wt') as out: json.dump(results,out,sort_keys=True)
else:
    with gzip.open(path,'rt') as source: expected=json.load(source)
    actual=json.loads(json.dumps(results))
    bad=[key for key in expected.keys()|actual.keys() if expected.get(key)!=actual.get(key)]
    for key in bad[:3]: print(key,'\nold',expected.get(key),'\nnew',actual.get(key))
    if bad: raise SystemExit(f'{len(bad)} differing allocation outcome sets')
print(json.dumps(stats,sort_keys=True))
print(f'{len(results)} allocation outcome sets passed; all Rust owners released')
