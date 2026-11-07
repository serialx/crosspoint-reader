#!/usr/bin/env python3
"""Compare resource-owner values and physical traces; optionally freeze parent hashes."""
import argparse
import difflib
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('executable');p.add_argument('--capture-parent',type=Path)
p.add_argument('--expected',type=Path);p.add_argument('--failures',type=Path);a=p.parse_args()
here=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('fixtures',here.parent/'xtc_diff/generate.py')
fixtures=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixtures)
expected=None
if a.expected:
    expected={row['case']:row for row in json.loads(gzip.decompress(a.expected.read_bytes()))}
if a.capture_parent is None and a.expected is None:
    default=here/'native_oracle/parent-results.txt.gz'
    expected={row['case']:row for row in json.loads(gzip.decompress(default.read_bytes()))}
records=[]
with tempfile.TemporaryDirectory(prefix='xtc-owner-') as tmp:
    path=Path(tmp)/'book.xtc'
    for depth in (1,2):
        for length in (0,15,16,22,23,29,30,31,63,127):
            data=bytearray(fixtures.book(depth))
            data[56:183]=b'\0'*127;data[56:56+length]=b'T'*length
            data[184:247]=b'\0'*63;data[184:184+min(length,63)]=b'A'*min(length,63)
            data[256:336]=b'\0'*80;data[256:256+min(length,80)]=b'C'*min(length,80)
            variants=[data,data[:20],data[:-1],b'NOPE'+data[4:],fixtures.book(depth,legacy=True)]
            for variant_index,variant in enumerate(variants):
                path.write_bytes(variant)
                clean=subprocess.check_output([a.executable,path,'base','0']) if expected is None else None
                failure=0
                # Cover every physical-operation index, including any longer error path.
                limit=int(clean.rsplit(b'calls ',1)[1]) if clean else expected[f'{depth}:{length}:{variant_index}:0']['calls']
                while failure<=limit:
                    case=f'{depth}:{length}:{variant_index}:{failure}'
                    before=clean if failure==0 else None
                    if expected is None and before is None:
                        before=subprocess.check_output([a.executable,path,'base',str(failure)])
                    after=subprocess.check_output([a.executable,path,'candidate',str(failure)])
                    if before is not None:
                        calls=int(before.rsplit(b'calls ',1)[1]);limit=max(limit,calls)
                        row=dict(case=case,fixture_sha256=hashlib.sha256(variant).hexdigest(),sha256=hashlib.sha256(before).hexdigest(),bytes=len(before),calls=calls)
                    else:row=expected[case]
                    limit=max(limit,row['calls'])
                    matches=(hashlib.sha256(variant).hexdigest()==row['fixture_sha256'] and
                             hashlib.sha256(after).hexdigest()==row['sha256'] and len(after)==row['bytes'])
                    if not matches:
                        directory=a.failures or Path(tmp);directory.mkdir(parents=True,exist_ok=True)
                        (directory/(case.replace(':','-')+'-candidate.txt')).write_bytes(after)
                        if before is not None:(directory/(case.replace(':','-')+'-parent.txt')).write_bytes(before)
                        print(f'mismatch {case}',flush=True)
                        if before is not None: print(''.join(list(difflib.unified_diff(before.decode().splitlines(True),after.decode().splitlines(True)))[:30]),flush=True)
                        raise SystemExit(1)
                    records.append(row);failure+=1
if expected is not None:
    assert len(records)==len(expected), (len(records),len(expected))
if a.capture_parent:
    a.capture_parent.write_bytes(gzip.compress(json.dumps(records,sort_keys=True,separators=(',',':')).encode(),mtime=0))
print(f'{len(records)} exact value/read/log/page/stream/reopen pairs; host allocation profiles not compared')
