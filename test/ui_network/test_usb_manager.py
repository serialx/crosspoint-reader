"""Exercise rejected USB preparation through the real manager loop and navigation."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import prepare

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
source = (HERE / 'usb_manager.cpp').read_text()
methods = []
for cls, filename, names in [
    ('UsbDriveActivity', 'network/UsbDriveActivity.cpp', ['onEnter', 'onExit', 'loop', 'render', 'run', 'service']),
    ('ActivityManager', 'ActivityManager.cpp', ['loop', 'exitActivity', 'replaceActivity', 'pushActivity',
                                               'popActivity', 'goHome', 'requiresExclusiveStorageLoop']),
]:
    production = (ROOT / 'src/activities' / filename).read_text()
    methods.extend(prepare.extract(production, cls, name) for name in names)
source = source.replace('// PRODUCTION_METHODS', '\n\n'.join(methods))
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    for filename in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        shutil.copyfile(ROOT / 'src/activities' / filename, out / filename)
    header = (ROOT / 'src/activities/network/UsbDriveActivity.h').read_text()
    (out / 'UsbDriveActivity.h').write_text(header.replace('activities/RustActivityAdapter.h', 'RustActivityAdapter.h'))
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('return paintOk; }', 'return acceptScene; }')
    stub = stub.replace('  std::atomic<bool> refreshing', '  bool acceptScene = true;\n  std::atomic<bool> refreshing')
    stub = stub.replace('  uint8_t touchFlags', '''  bool home = false;
  bool wasHomeGesture() const { return home; }
  void resetHomeButtonInput() { home = false; }
  bool consumeSuppressedRelease() { return false; }
  bool hasTouch() { return false; }
  bool wasScreenTapped(int&, int&) { return false; }
  bool lightPanel = false;
  bool wasLightPanelGesture() { return lightPanel; }
  uint8_t touchFlags''')
    stub = stub.replace('#include <mutex>', '#include <mutex>\n#include <functional>')
    stub = stub.replace('  RenderLock() { renderMutex.lock(); }', '  bool locked = true;\n  RenderLock() { renderMutex.lock(); }')
    stub = stub.replace('  ~RenderLock() { renderMutex.unlock(); }', '''  void unlock() { if (locked) { locked = false; renderMutex.unlock(); } }
  ~RenderLock() { unlock(); }''')
    stub = stub.replace('class Activity {\n protected:', 'using ActivityResult = int;\nclass Activity {\n public:')
    stub = stub.replace('  void requestUpdate() {}', '''  ActivityResult result = 0;
  std::function<void(ActivityResult)> resultHandler;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual bool isHomeActivity() const { return false; }
  virtual bool isReaderActivity() const { return false; }
  virtual bool handleHomeGesture() { return false; }
  virtual bool preventAutoSleep() { return false; }
  virtual bool requiresExclusiveStorageLoop() const { return false; }
  static void finish();
  void requestUpdate();
  void requestUpdateAndWait();''')
    (out / 'Activity.h').write_text(stub)
    (out / 'manager.cpp').write_text(source)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-I' + str(out),
                    '-I' + str(ROOT / 'test/ui_runtime/stubs'), '-I' + str(ROOT / 'rust/crates/xp-ffi/include'),
                    str(out / 'manager.cpp'), str(out / 'RustActivityAdapter.cpp'),
                    str(ROOT / 'test/cmake/xp_ffi_host.cpp'), sys.argv[1], '-o', str(out / 'manager')], check=True)
    failures = []
    for stage in ['build', 'paint']:
        for action in ['back', 'power', 'home', 'back-before', 'back-between', 'power-before',
                       'power-between', 'home-before', 'home-between', 'panel-before', 'panel-between',
                       'active', 'active-io', 'active-suspend', 'active-back', 'active-power', 'active-home']:
            result = subprocess.run([str(out / 'manager'), stage, action])
            if result.returncode:
                failures.append(f'{stage}/{action}')
    if failures:
        sys.exit('USB manager failures: ' + ', '.join(failures))
