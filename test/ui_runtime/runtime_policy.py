"""Compare frozen native policy with the active production adapter and storage."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ORACLE = Path(__file__).with_name('native_oracle')
manifest = json.loads((ORACLE / 'manifest.json').read_text())
for entry in manifest['snapshots']:
    data = (ORACLE / entry['file']).read_bytes()
    assert len(data) == entry['bytes']
    assert hashlib.sha256(data).hexdigest() == entry['sha256']
assert manifest['total_bytes'] == sum(x['bytes'] for x in manifest['snapshots'])

def replace_once(source, before, after):
    assert source.count(before) == 1, before
    return source.replace(before, after)

with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    def build_run(name, sources):
        executable = out / name
        subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                        '-I'+str(ROOT/'test/ui_runtime/stubs'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'),
                        *map(str, sources), str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1],
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=30)
    header = (ORACLE/'RustActivityAdapter.h.txt').read_text()
    source = (ORACLE/'RustActivityAdapter.cpp.txt').read_text()
    for route in ['native', 'wire']:
        h, cpp = ((ROOT/'src/activities/RustActivityAdapter.h').read_text(),
                  (ROOT/'src/activities/RustActivityAdapter.cpp').read_text()) if route == 'wire' else (header, source)
        (out/'RustActivityAdapter.h').write_text(h)
        (out/'RustActivityAdapter.cpp').write_text(cpp)
        build_run('adapter-'+route, [ROOT/'test/ui_runtime/adapter.cpp', out/'RustActivityAdapter.cpp'])

    for script in ['rejection_families.py', 'rejection_bypasses.py', 'rejection_keyboard.py',
                   'rejection_interleaving.py', 'gate_adoption.py', 'stale_publication.py', 'custom_gates.py']:
        subprocess.run([sys.executable, str(ROOT/'test/ui_runtime'/script), sys.argv[1]],
                       check=True, timeout=180)

    activity = (ROOT/'test/ui_runtime/stubs/Activity.h').read_text()
    activity = activity.replace('#include <string>', '#include <string>\n#include <vector>\ninline std::vector<std::string> testEvents;')
    activity = replace_once(activity, 'const { return paintOk; }', 'const { testEvents.push_back("paint"); return paintOk; }')
    activity = replace_once(activity, '    refreshing = true;', '    testEvents.push_back("refresh");\n    refreshing = true;')
    (out/'Activity.h').write_text(activity)
    (out/'components').mkdir()
    helpers = (ROOT/'test/ui_runtime/stubs/components/UiAppHelpers.h').read_text()
    helpers = replace_once(helpers, 'inline void touchAction(bool) {}', 'inline void touchAction(bool) { testEvents.push_back("haptic"); }')
    (out/'components/UiAppHelpers.h').write_text(helpers)
    order = (ROOT/'test/ui_runtime/runtime_order.cpp.in').read_text().replace('@UPDATE_RENDER@', (ORACLE/'UpdateUiRender.txt').read_text())
    (out/'order.cpp').write_text(order)
    for route in ['native', 'wire']:
        h, cpp = ((ROOT/'src/activities/RustActivityAdapter.h').read_text(),
                  (ROOT/'src/activities/RustActivityAdapter.cpp').read_text()) if route == 'wire' else (header, source)
        (out/'RustActivityAdapter.h').write_text(h)
        (out/'RustActivityAdapter.cpp').write_text(cpp)
        build_run('order-'+route, [out/'order.cpp', out/'RustActivityAdapter.cpp'])

    native = (ORACLE/'ReaderChromeCommands.txt').read_text()
    commands_header = (ROOT/'src/activities/reader/ReaderChromeListActivity.h').read_text()
    start = commands_header.index('class ReaderChromeCommands {')
    end = commands_header.index('\n};', start) + 3
    commands_source = (ROOT/'src/activities/reader/ReaderChromeListActivity.cpp').read_text()
    methods = commands_source[commands_source.index('bool ReaderChromeCommands::reserve'):
                              commands_source.index('void sampleReaderChromeTheme(')]
    shared = commands_header[start:end] + '\n' + methods
    code = r'''
#include <xp_ui.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>
#define LOG_ERR(...) ((void)0)
static bool fail;
static unsigned live;
static std::vector<unsigned> requests;
struct DeleteCommands {
  void operator()(XpUiCommand* p) const { if (p) { --live; delete[] p; } }
};
// Track releases while preserving the production unique_ptr interface.
template<class T> using Storage = std::unique_ptr<T, DeleteCommands>;
template<class T> Storage<T> makeUniqueNoThrow(size_t n) {
  assert(live == 0); requests.push_back(n);
  if (fail) return nullptr;
  ++live; return Storage<T>(new XpUiCommand[n]{});
}
'''
    # Adapt only the physical allocator/deallocator so overlap is observable.
    for name, text in [('NativeCommands', native), ('SharedCommands', shared)]:
        code += text.replace('ReaderChromeCommands', name).replace('std::unique_ptr<XpUiCommand[]>', 'Storage<XpUiCommand[]>')
    code += r'''
template<class Commands> std::vector<unsigned> trace(unsigned initial, unsigned step) {
  requests.clear(); fail = false;
  {
    Commands commands;
    assert(commands.reserve(initial));
    for (;;) {
      const auto old = commands.buffer().command_capacity;
      if (old == 128) { assert(!commands.grow()); break; }
      fail = true;
      const auto next = std::min(128u, unsigned(old) + step);
      const bool grew = step == 16 ? commands.grow() : commands.reserve(
          std::is_same_v<Commands, SharedCommands> ? xp_ui_command_next_capacity(old, 0, step, 128) : next);
      assert(!grew && !commands.ready() && live == 0);
      assert(commands.buffer().command_capacity == 0 && requests.back() == next);
      fail = false;
      // FileBrowser's retry asks for its initial size; requested size must survive.
      assert(commands.reserve(initial));
      assert(commands.buffer().command_capacity == next);
    }
  }
  assert(live == 0);
  return requests;
}
int main() {
  static_assert(sizeof(XpUiSceneGate) == 4);
  static_assert(alignof(XpUiSceneGate) == 2);
  static_assert(offsetof(XpUiSceneGate, failed) == 2);
  static_assert(offsetof(XpUiSceneGate, suppressed_contact) == 3);
  for (auto initial : {8u, 16u, 24u, 32u, 40u, 48u}) {
    const auto native = trace<NativeCommands>(initial, 16);
    const auto shared = trace<SharedCommands>(initial, 16);
    assert(native == shared);
    std::cout << "start=" << initial << " step=16 requests=";
    for (auto n : shared) std::cout << n << ',';
    std::cout << '\n';
  }
  const auto files = trace<SharedCommands>(32, 4);
  assert(trace<NativeCommands>(32, 4) == files);
  std::cout << "start=32 step=4 requests=";
  for (auto n : files) std::cout << n << ',';
  std::cout << '\n';
  for (unsigned current = 32; current < 128; current += 4)
    assert(xp_ui_command_next_capacity(current, current+1, 4, 128) == current+4);
}
'''
    (out/'storage.cpp').write_text(code)
    build_run('storage', [out/'storage.cpp'])
print(f"Frozen native and bridge adapter compositions pass; {manifest['total_bytes']} source bytes consumed")
