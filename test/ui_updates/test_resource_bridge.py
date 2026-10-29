"""Run the production row rebuild and bridge with deterministic task interleaving."""
from pathlib import Path
import argparse,re,subprocess,tempfile
from test_failure_bridge import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('library');p.add_argument('--revision');args=p.parse_args()
def read(path):
 return subprocess.check_output(['git','show',args.revision+':'+path],cwd=ROOT,text=True) if args.revision else (ROOT/path).read_text()
font=read('src/activities/settings/FontDownloadActivity.cpp');bridge=read('src/activities/settings/UpdateUiActivity.cpp');adapter=read('src/activities/RustActivityAdapter.cpp')
names=['rebuildRowItems','rebuildGroupRowItems','rebuildFamilyRowItems','showDownloadAllRow','showUpdateAllRow','specialRowCount','isDownloadAllRow','isUpdateAllRow','listItemCount','familyIndexFromList','groupMemberCount','buildFilteredIndices','enterGroup','totalDownloadSize','totalUpdateSize','formatSize','prepareRows','row','prepareText','fontConfirmLabel']
if not args.revision:names+=['releaseText','setFamilyStatus']
methods=[method(font,'FontDownloadActivity::'+n)for n in names]
declarations=[]
for m in methods:
 signature=m[:m.index('{')].replace('FontDownloadActivity::','').strip()
 if 'formatSize(' in signature:signature='static '+signature
 if any(n+'(' in signature for n in ['enterGroup','prepareRows','row','prepareText','releaseText']):signature+=' override'
 declarations.append(signature+';')
methods=[m.replace('const int listSize = listItemCount();','const int listSize = listItemCount(); if(pauseRows)rowsGate.stop();') for m in methods]
methods += [method(bridge,'UpdateUiActivity::'+n)for n in ['rustPublished','rustSceneFailed','updateRust']]
if args.revision:
 methods += [method(adapter,'RustActivityAdapter::'+n).replace('RustActivityAdapter::','UpdateUiActivity::')for n in ['render','paintRustScene']]
 methods=[m.replace('xp_ui_updates_service','xp_ui_updates_notify')for m in methods]
else:methods += [method(bridge,'UpdateUiActivity::render')]
s=(HERE/'resource_bridge.cpp').read_text().replace('// DECLARATIONS','\n'.join(declarations)).replace('// METHODS','\n'.join(methods))
if args.revision:s=s[:s.index('  pauseRows = false;')]+' puts("baseline race not reproduced");\n}\n'
with tempfile.TemporaryDirectory()as directory:
 cpp=Path(directory)/'bridge.cpp';exe=Path(directory)/'bridge';cpp.write_text(s)
 subprocess.run(['c++','-std=c++20','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(cpp),str(ROOT/'test/cmake/xp_ffi_host.cpp'),args.library,'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=15)
