#!/usr/bin/env python3
"""Frozen upstream logs plus optional paired resource and corpus checks."""
import argparse
import json
import gzip
import hashlib
import subprocess
from pathlib import Path
import struct
import tarfile
from cases import run

HERE = Path(__file__).resolve().parent

def corpus():
    rows = []
    with tarfile.open(HERE.parent / 'section_golden/goldens/sections.tar.gz') as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.endswith('.bin'): continue
            data = archive.extractfile(member).read()
            count, lut = struct.unpack_from('<HI', data, 22)
            starts = list(struct.unpack_from(f'<{count}I', data, lut)) + [lut]
            for i in range(count):
                rows.append({'name': f'{member.name}:{i}', 'hex': data[starts[i]:starts[i+1]].hex()})
    return rows

def mutations(pages):
    rows=[]
    for page in pages:
        original=bytes.fromhex(page['hex'])
        if len(original)<3 or original[:2]==b'\0\0': continue
        fields=[('unknown-tag',2,b'\xff')]
        if original[2]==1:
            words,focus,text=struct.unpack_from('<HBH',original,7)
            if words:
                fields.extend([
                    ('word-count',7,struct.pack('<H',10001)),
                    ('paragraph-start',12,struct.pack('<H',words)),
                    ('text-size',10,b'\0\0'),
                    ('first-offset',14+words*8,b'\1\0'),
                    ('terminator',14+words*(13+(3 if focus else 0))+text-1,b'x'),
                ])
                if words>1: fields.append(('second-offset',14+words*8+2,b'\0\0'))
        for name,at,value in fields:
            data=bytearray(original);data[at:at+len(value)]=value
            rows.append({'name':page['name']+':'+name,'hex':data.hex()})
    return rows

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('binary', type=Path)
    ap.add_argument('--baseline', type=Path)
    ap.add_argument('--reference', type=Path)
    ap.add_argument('--report', type=Path)
    args=ap.parse_args()
    golden=json.loads((HERE/'goldens.json').read_text())
    valid=corpus()
    # Every removed page allocation is now one fallible allocation. Exercise it
    # for each valid page and require the same requests as the migration base.
    oom=[dict(c,oom=1) for c in valid]
    mutated=mutations(valid)
    frozen=json.loads(gzip.decompress((HERE/'mutations.json.gz').read_bytes()))['records']
    assert len(frozen)==len(mutated)
    for c,f in zip(mutated,frozen):
        assert c['name']==f['name'] and hashlib.sha256(bytes.fromhex(c['hex'])).hexdigest()==f['sha256']
    exceptions=json.loads((HERE/'exceptions.json').read_text())
    cases=golden+valid+oom+mutated+exceptions
    summary={}
    for mode in ['memory','file']:
        actual=run(args.binary.resolve(),cases,mode)
        for c,r in zip(golden,actual):
            assert r['logs']==c['logs'] and r['accepted']==c['accepted'], (mode,c['name'],r,c)
        for c,r in zip(valid,actual[len(golden):]):
            assert r['accepted'] and not r['logs'], (mode,c['name'],r)
        for c,r in zip(oom,actual[len(golden)+len(valid):len(golden)+len(valid)+len(oom)]):
            assert not r['accepted'] and r['logs']==['ERR\tPGE\tDeserialization failed: could not allocate Page'], (mode,c['name'],r)
        for c,r,f in zip(mutated,actual[len(golden)+len(valid)+len(oom):],frozen):
            assert r['accepted']==f['accepted'] and r['logs']==f['logs'], (mode,c['name'],r,f)
        for c,r in zip(exceptions,actual[-len(exceptions):]):
            assert not r['accepted'] and r['logs']==c['candidate'][mode], (mode,c['name'],r)
        if args.baseline:
            base=run(args.baseline.resolve(),cases,mode)
            for c,a,b in zip(cases,actual,base):
                assert all(a[k]==b[k] for k in ['accepted','attempts','requested','io']), (mode,c['name'],a,b)
        if args.reference:
            reference=run(args.reference.resolve(),golden+valid+mutated+exceptions,mode)
            selected=actual[:len(golden)+len(valid)]+actual[len(golden)+len(valid)+len(oom):]
            for c,a,b in zip(golden+valid+mutated+exceptions,selected,reference):
                if 'reference' in c:
                    assert not a['accepted'] and not b['accepted'] and b['logs']==c['reference'] and a['logs']==c['candidate'][mode], (mode,c['name'],a,b)
                else:
                    assert a['accepted']==b['accepted'] and a['logs']==b['logs'], (mode,c['name'],a,b)
        summary[mode]={'focused':len(golden),'valid_pages':len(valid),'oom_pages':len(oom),'mutations':len(mutated),'exceptions':len(exceptions),'allocation_records':len(actual) if args.baseline else 0}
    short=[c for c in exceptions if c.get('reader_required')]
    for c,r in zip(short,run(args.binary.resolve(),short,'reader')):
        assert not r['accepted'] and r['logs']==c['candidate']['file']+['ERR\tERS\tFailed to load page from SD - clearing section cache'], (c['name'],r)
    metadata=subprocess.run([str(args.binary.resolve()),'reader-metadata'],capture_output=True,text=True,check=True)
    assert metadata.stdout.splitlines()==['ERR\tERS\tFailed to copy page metadata - clearing section cache']
    summary['reader']={'truncated_pages_reported':len(short),'metadata_failure':1}
    if args.report: args.report.write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))
if __name__=='__main__': main()
