"""Failed chapter preparation restores a saved complete or partial page without eager retry."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path.insert(0, str(ROOT / 'test'))
sys.path.insert(0, str(HERE))
from rust_host import rust_link_flags
from test_reader_settings import generated_i18n_directory
from test_book_render import replace_once
source = (HERE / 'book_render_oracle.cpp').read_text()
source = source[:source.index('int main() {')] + (ROOT / 'test/reader_harden/chapter_recovery.cpp').read_text()
source = replace_once(source.encode(), b'#include "book_render_session_rust.inc"', (HERE / 'book_render_session_rust.inc').read_bytes())
source = replace_once(source, b'/* ACTUAL_SECTION_START_BUILD */', (HERE / 'legacy_section_start_build.inc').read_bytes())
with tempfile.TemporaryDirectory(prefix='reader-harden-build-') as tmp:
    work = Path(tmp); cpp = work / 'fixture.cpp'; cpp.write_bytes(source); binary = work / 'fixture'
    flags = rust_link_flags(); i18n = generated_i18n_directory(flags[-1])
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-Wno-format', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-DBOOK_RENDER_OWNER=1', '-I', str(HERE), '-I', str(i18n),
                    str(cpp), *flags, '-o', str(binary)], check=True)
    trace = subprocess.check_output([str(binary)])
    assert trace.count(b'build-start 0') == 2
    assert trace.count(b'page-load 2') == 2
    assert b'save-spine 1' not in trace
    assert trace.count(b'save-spine 0') <= 1  # partial estimate may be saved again, with the true same position
    print(trace.splitlines()[-1].decode())
