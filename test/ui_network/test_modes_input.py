"""Keep the frozen native input oracle; Rust owner/App tests cover the candidate."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import prepare
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for frozen in [True]:
    source = (HERE/'modes_input.cpp').read_text()
    native = HERE/'native_oracle'
    production = (native/'NetworkModeSelectionActivity.cpp.txt' if frozen else ROOT/'src/activities/network/NetworkModeSelectionActivity.cpp').read_text()
    methods = ['rustSceneFailed', 'onModeSelected', 'onCancel', 'rustInterest', 'updateRust', 'applyRustEffect', 'rustPublished', 'rustSceneCurrent']
    source = source.replace('// PRODUCTION_METHODS', '\n'.join(prepare.extract(production, 'NetworkModeSelectionActivity', m) for m in methods))
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        shutil.copyfile(native/'RustActivityAdapter.h.txt' if frozen else ROOT/'src/activities/RustActivityAdapter.h', out/'RustActivityAdapter.h')
        shutil.copyfile(native/'RustActivityAdapter.cpp.txt' if frozen else ROOT/'src/activities/RustActivityAdapter.cpp', out/'RustActivityAdapter.cpp')
        stub = (ROOT/'test/ui_runtime/stubs/Activity.h').read_text()
        stub = stub.replace('return paintOk; }', 'return acceptScene; }')
        stub = stub.replace('  std::atomic<bool> refreshing', '  bool acceptScene = true;\n  std::atomic<bool> refreshing')
        (out/'Activity.h').write_text(stub)
        (out/'input.cpp').write_text(source)
        subprocess.run(['c++', '-std=c++20', '-pthread', '-I'+str(out), '-I'+str(ROOT/'test/ui_runtime/stubs'),
                        '-I'+str(ROOT/'rust/crates/xp-ffi/include'), str(out/'input.cpp'), str(out/'RustActivityAdapter.cpp'),
                        str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1], '-o', str(out/'input')], check=True)
        subprocess.run([str(out/'input')], check=True)
