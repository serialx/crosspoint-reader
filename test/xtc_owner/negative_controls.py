#!/usr/bin/env python3
"""Strict Miri witnesses must pass controls and reject each deliberately stale loan."""
import argparse
import json
import os
from pathlib import Path
import subprocess
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
manifest=Path(__file__).resolve().parent/'rust/Cargo.toml'
rows=[]
for name in ('control','negative_moved_short_metadata','negative_chapter_loan_after_close','negative_metadata_loan_after_drop'):
    cmd=['cargo','+nightly','miri','test','--manifest-path',str(manifest),'--offline','--locked']
    if name!='control':cmd.append(name)
    cmd+=['--','--test-threads=1']
    if name!='control':cmd.append('--ignored')
    r=subprocess.run(cmd,env=dict(os.environ,MIRIFLAGS='-Zmiri-strict-provenance'),capture_output=True,text=True)
    text=r.stdout+r.stderr;(a.out/(name+'.log')).write_text(text)
    okay=r.returncode==0 if name=='control' else r.returncode!=0 and 'Undefined Behavior' in text and ('freed' in text or 'dangling' in text)
    rows.append(dict(name=name,command=cmd,returncode=r.returncode,expected='pass' if name=='control' else 'Miri UB',matched=okay))
    print(name, 'PASS' if okay else 'FAIL',flush=True)
    if not okay:
        (a.out/'RESULT.json').write_text(json.dumps(rows,indent=2)+'\n');raise SystemExit(1)
(a.out/'RESULT.json').write_text(json.dumps(rows,indent=2)+'\n')
