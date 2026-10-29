"""Exercise the actual reader scratch owner with allocation failure injection."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'src/activities/reader/ReaderChromeListActivity.h').read_text()
source = (ROOT / 'src/activities/reader/ReaderChromeListActivity.cpp').read_text()
start = header.index('class ReaderChromeCommands {')
owner = header[start:header.index('\n};', start) + 3]
start = source.index('bool ReaderChromeCommands::reserve(')
methods = source[start:source.index('ReaderChromeListActivity::ReaderChromeListActivity', start)]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <xp_ui_reader_chrome.h>
static bool fail = false;
static int allocations = 0, errors = 0;
static void* live = nullptr;
void* operator new[](size_t count) {
  assert(!live); // Growth must release the superseded scratch before allocation.
  live = std::malloc(count);
  assert(live);
  ++allocations;
  return live;
}
void operator delete[](void* p) noexcept { assert(p == live); std::free(p); live = nullptr; }
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (fail) return {};
  return std::unique_ptr<T>(new std::remove_extent_t<T>[count]);
}
#define LOG_ERR(...) (++errors)
'''
checks = r'''
int main() {
  {
    ReaderChromeCommands commands;
    assert(!commands.ready());
    fail = true;
    assert(!commands.reserve(32) && !commands.ready() && !live && errors == 1);
    fail = false;
    assert(commands.reserve(32));
    assert(commands.buffer().command_capacity == 32 && allocations == 1);
    assert(commands.reserve(24) && allocations == 1); // Repaints reuse storage.
    assert(commands.grow() && commands.buffer().command_capacity == 48);
    fail = true;
    assert(!commands.grow() && !commands.ready() && !live && errors == 2);
    assert(commands.buffer().command_capacity == 0);
    fail = false;
    assert(commands.recover());
    assert(commands.ready() && commands.buffer().command_capacity == 64);
    assert(commands.reserve(128));
    const int before = allocations;
    assert(!commands.grow() && !commands.reserve(129) && allocations == before);
    assert(commands.ready() && commands.buffer().command_capacity == 128);
    commands.reset();
    assert(!commands.ready() && !live);
    assert(commands.reserve(24));
  }
  assert(!live); // Parent/visit destruction frees the final allocation.
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'commands.cpp').write_text(fixture + owner + methods + checks)
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I' + str(ROOT / 'rust/crates/xp-ffi/include'),
                    str(path / 'commands.cpp'), '-o', str(path / 'commands')], check=True)
    subprocess.run([str(path / 'commands')], check=True)
