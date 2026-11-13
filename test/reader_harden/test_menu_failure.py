"""A checked missing size must not disable unrelated classic-menu actions."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path[:0] = [str(HERE), str(ROOT / 'test')]
from rust_host import rust_link_flags
from test_menu_parent import transformed_fixture, generated_i18n_directory, replace_once
source = transformed_fixture((HERE / 'menu_parent_oracle.cpp').read_text())
source = source[:source.index('int main() {')] + (ROOT / 'test/reader_harden/menu_failure.cpp').read_text()
source = replace_once(source, 'event("percent");\n    return 42;', 'event("percent");\n    return -1;')
with tempfile.TemporaryDirectory(prefix='reader-harden-menu-') as directory:
    work = Path(directory); cpp = work / 'fixture.cpp'; cpp.write_text(source); binary = work / 'fixture'
    flags = rust_link_flags(); i18n = generated_i18n_directory(flags[-1])
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-Wno-switch', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-DXP_STRIP_RENDER=0', '-I', str(HERE), '-I', str(i18n),
                    str(cpp), *flags, '-o', str(binary)], check=True)
    trace = subprocess.check_output([str(binary)])
    assert trace.count(b'start [menu]') == 3
    assert b'home ' in trace
    print(trace.splitlines()[-1].decode())
