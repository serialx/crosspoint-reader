"""Back after failed preparation uses the real input, App navigation and reader exit."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
source = (HERE / 'app_owned_epub.cpp').read_text().split('int main(int argc, char** argv)', 1)[0]
source += r"""
extern "C" uint8_t xp_epub_test_recovery_back(void*);
static const XpUiReaderInputServices backInput = {
  nullptr,
  [](void*, uint32_t mask, uint8_t kind, uint32_t) -> uint8_t {
    assert(mask == XP_UI_BACK);
    return kind == 1; // Back release, not a long press.
  },
  [](void*, uint8_t kind) -> uint32_t { assert(kind == 8 || kind == 2); return 0; },
  [](void*, int32_t*, int32_t*) -> uint8_t { assert(false); return 0; },
};
int main() {
  Fixture state; fixture = &state; bind();
  leaves.input_services = [] { return &backInput; };
  runtime.diagnostic = [](uint8_t phase, uint32_t, uint32_t) { assert(phase <= 1); };
  resources.settings_snapshot = [] { return XpAppHomeSettings{}; };
  resources.theme_snapshot = [](XpAppTheme* out) { *out = {}; out->columns = 1; };
  resources.recents_visit = [](XpAppBookSink, void*) -> uint8_t { return 1; };
  initialize();
  const auto originalLive = xp_test_live_allocations();
  state.startAttempt(); assert(install()); assertEntered();
  assert(xp_epub_test_recovery_back(xp_app_test_current_epub()));
  assert(xp_app_test_pending_kind() == 3); // Real Back replaces the reader with Home.
  assert(xp_app_test_drain());
  assert(!xp_app_test_current_epub() && xp_app_test_is_home());
  assertExited();
  drainToSleep();
  popFinalSleep();
  assert(state.handles.empty() && xp_test_live_allocations() == originalLive);
  puts("PASS: Back after failed preparation requests Home and tears down the reader");
}
"""
with tempfile.TemporaryDirectory(prefix='reader-recovery-back-') as directory:
    work = Path(directory); cpp = work / 'fixture.cpp'; cpp.write_text(source); binary = work / 'fixture'
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
                    '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
                    str(cpp), *rust_link_flags(), '-o', str(binary)], check=True)
    trace = subprocess.check_output([str(binary)])
    print(next(line.decode() for line in trace.splitlines() if line.startswith(b'PASS:')))
