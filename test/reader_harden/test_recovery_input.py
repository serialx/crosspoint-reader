"""Reader-loop dispatch after failed preparation: no idle retry, explicit Previous and menu work."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path[:0] = [str(HERE), str(ROOT / 'test')]
from rust_host import rust_link_flags
from test_loop_session import transformed_fixture, replace_once
source = transformed_fixture((HERE / 'loop_oracle.cpp').read_text()).split('int main(', 1)[0]
source = replace_once(source, 'static void event(const char* name, int a = 0, int b = 0) {',
    'static int updates, menus, builds;\nstatic void event(const char* name, int a = 0, int b = 0) {\n'
    'if (std::string(name) == "update") ++updates;\n'
    'if (std::string(name) == "reader-menu") ++menus;\n'
    'if (std::string(name) == "build-start") ++builds;')
source += r"""
int main() {
  EpubReaderActivity reader;
  reader.section.reset(); reader.currentSpineIndex = 0;
  reader.partialRebuildStartFailed = true;
  reader.automaticPageTurnActive = false;
  loop_session::Bridge session(reader);
  for (int cycle = 0; cycle < 100; ++cycle) reader.loop();
  assert(updates == 0 && menus == 0 && builds == 0 && session.buildAttempts == 0);
  ReaderUtils::prev = true;
  reader.loop();
  assert(updates == 1 && !reader.section && builds == 0);
  ReaderUtils::prev = false;
  reader.mappedInput.home = HomeButtonAction::ReaderMenu;
  reader.loop();
  assert(menus == 1 && builds == 0);
  puts("PASS: failed-preparation state has no idle build/redraw; Previous requests saved-page render; menu dispatch remains available");
}
"""
with tempfile.TemporaryDirectory(prefix='reader-harden-recovery-input-') as directory:
    work = Path(directory); cpp = work / 'fixture.cpp'; cpp.write_text(source); binary = work / 'fixture'
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I', str(HERE), '-I', str(ROOT / 'src'),
                    str(cpp), *rust_link_flags(), '-o', str(binary)], check=True)
    trace = subprocess.check_output([str(binary)])
    print(next(line.decode() for line in trace.splitlines() if line.startswith(b'PASS:')))
