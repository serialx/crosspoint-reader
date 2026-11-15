#!/usr/bin/env python3
"""Frozen ImageBlock pixels and diagnostic transitions across cache/failure paths."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

results={}
for variant in range(10):
    for orientation in range(4):
        for mode in range(6):
            with tempfile.TemporaryDirectory() as tmp:
                value=subprocess.run([sys.argv[1],tmp,str(variant),str(orientation),str(mode)],capture_output=True,
                                     env={**os.environ,'IMAGE_DIAGNOSTIC_LOGS':'1'},check=True)
                results[f'{variant}/{orientation}/{mode}']=[hashlib.sha256(value.stdout).hexdigest(),value.stderr.decode().replace(tmp,'ROOT')]
path=Path(__file__).with_name('image-block-goldens.json.gz')
if '--capture' in sys.argv:
    with gzip.open(path,'wt') as out: json.dump(results,out,sort_keys=True)
else:
    with gzip.open(path,'rt') as inp: expected=json.load(inp)
    # The old fixture forced every decoder lookup to fail. The shipping path
    # reaches JPEGDEC for this one-byte source; pixels and failure suppression
    # still match the frozen record, and the real failure log is checked exactly.
    for key, record in expected.items():
        if key.startswith('7/'):
            old = 'ERR IMG No decoder found for image: ROOT/image.jpg\n'
            new = ('DBG IMG Using JPEG decoder\n'
                   'DBG JPG Decoding JPEG: ROOT/image.jpg\n'
                   'ERR JPG Failed to open JPEG (err=4): ROOT/image.jpg\n'
                   'ERR IMG Failed to decode image: ROOT/image.jpg\n')
            assert record[1].count(old) == 1
            record[1] = record[1].replace(old, new)
    bad=[key for key in expected.keys()|results.keys() if expected.get(key)!=results.get(key)]
    for key in bad[:4]: print(key,'\nold',expected.get(key),'\nnew',results.get(key))
    if bad: raise SystemExit(f'{len(bad)} differences')
print(f'{len(results)} ImageBlock cases passed')
