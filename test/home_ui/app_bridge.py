"""Home input and pop delivery through production Rust and native bridge callbacks."""
from pathlib import Path
import subprocess
import sys
import tempfile
from input_oracle import method

ROOT = Path(__file__).resolve().parents[2]
manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
source = (Path(__file__).with_suffix('.cpp')).read_text()
for marker, signature in [('FACTORY', '[](const XpAppLaunch* launch)'),
                          ('WITH_RESULT', '[](void* activity, XpAppResultSink sink'),
                          ('DELIVERY', '[](void* parent, const void* result)')]:
    source = source.replace('// ' + marker, method(manager, signature))
source = source.replace('// LEGACY_OWNER', 'struct OwnedLegacyActivity' + manager.split('struct OwnedLegacyActivity', 1)[1].split('XpAppText text', 1)[0])
source = source.replace('// OWNED_TEXT', method(manager, 'std::string owned('))
source = source.replace('// FILE_CONSTRUCTOR', method((ROOT/'src/activities/home/FileBrowserActivity.cpp').read_text(), 'FileBrowserActivity::FileBrowserActivity('))
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp); (out/'bridge.cpp').write_text(source)
    subprocess.run(['c++','-std=c++20','-pthread','-UNDEBUG','-DXP_FFI_HOST_FAULT_INJECTION',
                    '-I'+str(ROOT/'src/activities'), '-I'+str(ROOT/'test/ui_runtime'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(out/'bridge.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(out/'bridge')],check=True)
    if sys.argv[2]=='results':
        subprocess.run([str(out/'bridge')],check=True)
    else:
        cases=0
        for grid in [0,1]:
            for books in [0,1]:
                for plugins in [0,1]:
                    for servers in [0,1,2]:
                        for menu in [1,2,3,4,5]:
                            if menu==3 and not(plugins or servers): continue
                            for touch in [0,1]:
                                args=[grid,books,plugins,servers,menu,touch]
                                subprocess.run([str(out/'bridge'),'factory',*map(str,args)],check=True)
                                cases+=1
        print(f'{cases} Home factory paths passed')
