"""Run the USB facade and adapter against rejected and delayed physical frames."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import prepare
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
source = (HERE/'usb_input.cpp').read_text()
production = (ROOT/'src/activities/network/UsbDriveActivity.cpp').read_text()
methods = ['loop', 'render', 'run', 'service']
source = source.replace('// PRODUCTION_METHODS', '\n'.join(prepare.extract(production, 'UsbDriveActivity', m) for m in methods))
header = (ROOT/'src/activities/network/UsbDriveActivity.h').read_text()
hook = header.split('  void rustSceneFailed() override {', 1)[1].split('}', 1)[0]
source += '\nvoid UsbDriveActivity::rustSceneFailed() {' + hook + '}\n'
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    for filename in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        shutil.copyfile(ROOT/'src/activities'/filename, out/filename)
    stub = (ROOT/'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('return paintOk; }', 'return acceptScene; }')
    stub = stub.replace('  std::atomic<bool> refreshing', '  bool acceptScene = true;\n  std::atomic<bool> refreshing')
    stub = stub.replace('  uint8_t touchFlags', '  bool home = false;\n  bool wasHomeGesture() const {return home;}\n  uint8_t touchFlags')
    (out/'Activity.h').write_text(stub)
    (out/'input.cpp').write_text(source)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-I'+str(out), '-I'+str(ROOT/'test/ui_runtime/stubs'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'), str(out/'input.cpp'), str(out/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1], '-o', str(out/'input')], check=True)
    subprocess.run([str(out/'input')], check=True)
