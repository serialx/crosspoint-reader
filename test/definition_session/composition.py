"""Actual App generic pop and actual C++ result bridge, delivering to the typed Rust owner."""
import argparse,json,subprocess,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'test'),str(ROOT/'test/reader_lifecycle')]
from rust_host import rust_link_flags
from test_app_results import block
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--archive');a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
manager=(ROOT/'src/activities/ActivityManager.cpp').read_text()
bridge=block(manager,'[](void* activity, XpAppResultSink sink, void* context)').replace('auto result =','ResultLifetime lifetime;\n            auto result =')
s=Path(__file__).with_name('composition.cpp.in').read_text().replace('// GENERIC_BRIDGE',bridge)
cpp=a.evidence/'composition.cpp';cpp.write_text(s);binary=a.evidence/'composition'
cmd=['c++','-std=c++20','-O1','-g','-fsanitize=address,undefined',str(cpp),'-I',str(ROOT/'src'),'-I',str(ROOT/'test/ui_runtime'),*rust_link_flags(a.archive),'-o',str(binary)]
r=subprocess.run(cmd,text=True,capture_output=True);(a.evidence/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:print(r.stderr[-5000:]);raise SystemExit(r.returncode)
records=[]
for size in [0,1,15,4096]:
 for cancelled in [0,1]:
  r=subprocess.run([str(binary),str(size),str(cancelled)],capture_output=True,text=True);(a.evidence/f'run-{size}-{cancelled}.log').write_text(r.stdout+r.stderr);records.append({'length':size,'cancelled':cancelled,'exit':r.returncode})
  if r.returncode:print(r.stderr[-4000:]);break
 if r.returncode:break
(a.evidence/'results.json').write_text(json.dumps({'cases':records,'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'scope':'Real App generic pop, real native stack-owned ActivityResult and view, typed Rust Definition App owner and synthetic Page resources; real native ActivityResult outlives the single copy.'},indent=2)+'\n')
assert len(records)==8 and all(r['exit']==0 for r in records),records
print('8 actual-App/C++ generic result compositions passed')
