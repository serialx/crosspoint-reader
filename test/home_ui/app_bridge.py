"""Home input and pop delivery through production Rust and native bridge callbacks."""
from pathlib import Path
import hashlib
import json
import signal
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
ownership = (ROOT / 'src/activities/OwnedLegacyActivity.h').read_text()
source = source.replace('// LEGACY_OWNER', ownership[ownership.index('struct OwnedLegacyActivity {'):])
source = source.replace('// OWNED_TEXT', method(manager, 'std::string owned('))
source = source.replace('// FILE_CONSTRUCTOR', method((ROOT/'src/activities/home/FileBrowserActivity.cpp').read_text(), 'FileBrowserActivity::FileBrowserActivity('))
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp); (out/'bridge.cpp').write_text(source)
    def compile_bridge(name, override=None):
        executable = out / name
        subprocess.run(['c++','-std=c++20','-pthread','-UNDEBUG','-DXP_FFI_HOST_FAULT_INJECTION',
                        *(['-I'+str(override)] if override is not None else []),
                        '-I'+str(ROOT/'src/activities'), '-I'+str(ROOT/'test/ui_runtime'),
                        '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(out/'bridge.cpp'),
                        str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(executable)],check=True)
        return executable

    compile_bridge('bridge')
    if sys.argv[2]=='results':
        subprocess.run([str(out/'bridge')],check=True)
        header_path = ROOT / 'src/activities/ActivityResultView.h'
        production = header_path.read_bytes()
        mutations = [
            ('clipping-end-offset', b'value.endOffset,', b'value.startOffset,',
             b'view.value.clipping.end_offset == value.endOffset'),
            ('progress-offset-presence', b'value.hasSavedProgress, value.hasVisibleTextOffset',
             b'value.hasSavedProgress, false',
             b'view.value.progress.has_offset == value.hasVisibleTextOffset'),
        ]
        try:
            for name, before, after, assertion in mutations:
                assert production.count(before) == 1, (name, 'mutation site must match once')
                changed = production.replace(before, after, 1)
                override = out / name
                override.mkdir()
                (override / header_path.name).write_bytes(changed)
                executable = compile_bridge(name + '-bridge', override)
                result = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                assert result.returncode == -signal.SIGABRT, (name, result.returncode, result.stderr)
                assert assertion in result.stderr, (name, 'failure did not come from the typed payload check', result.stderr)
                print(json.dumps({
                    'test_name': 'app_result_slots', 'mutant': name, 'replacement_count': 1,
                    'source': str(header_path.relative_to(ROOT)),
                    'original_source_sha256': hashlib.sha256(production).hexdigest(),
                    'mutant_source_sha256': hashlib.sha256(changed).hexdigest(),
                    'returncode': result.returncode,
                    'assertion': assertion.decode(),
                    'stderr_sha256': hashlib.sha256(result.stderr).hexdigest(),
                }), flush=True)
        finally:
            assert header_path.read_bytes() == production, 'production result-view header changed'
        print('app_result_slots: all 13 typed variants with cancelled false/true; two production header mutants rejected')
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
