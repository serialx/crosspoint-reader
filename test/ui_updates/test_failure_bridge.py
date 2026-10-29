"""Exercise the actual adapter rejection hooks and family arena/effect methods."""
from pathlib import Path
import re,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def method(source, name):
    match=re.search(r'^[\w:<>&*]+(?:\s+[\w:<>&*]+)*\s+'+re.escape(name)+r'\(',source,re.M)
    if not match:raise ValueError(name)
    begin=source.index('{',match.start());depth=1;i=begin+1
    while depth:
        if source[i]=='{':depth+=1
        elif source[i]=='}':depth-=1
        i+=1
    return source[match.start():i]

def main(library):
    adapter=(ROOT/'src/activities/RustActivityAdapter.cpp').read_text()
    family=(ROOT/'src/activities/settings/UpdateUiActivity.cpp').read_text()
    methods=[method(adapter,'RustActivityAdapter::'+name)for name in ['paintRustScene','render']]
    methods += [method(family,'UpdateUiActivity::'+name)for name in ['render','reserve','rustPublished','rustSceneFailed','updateRust','applyRustEffect']]
    source=(HERE/'failure_bridge.cpp').read_text().replace('// METHODS','\n'.join(methods))
    with tempfile.TemporaryDirectory()as directory:
        cpp=Path(directory)/'bridge.cpp';exe=Path(directory)/'bridge';cpp.write_text(source)
        subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(cpp),str(ROOT/'test/cmake/xp_ffi_host.cpp'),library,'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main(sys.argv[1])
