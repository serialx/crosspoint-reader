"""Real render/section control flow consumes either failed popup-size bound.

Strict HAL tests in run.py establish the producer's None on read failure. This
session fixture injects that checked result, keeping physical page/build leaves
identical to the existing renderer oracle, and asserts render plus progress save.
"""
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
source = source[:source.index('int main() {')] + (ROOT / 'test/reader_harden/build_failure.cpp').read_text()
source = replace_once(source.encode(), b'#include "book_render_session_rust.inc"', (HERE / 'book_render_session_rust.inc').read_bytes())
source = replace_once(source, b'/* ACTUAL_SECTION_START_BUILD */', (HERE / 'legacy_section_start_build.inc').read_bytes())
with tempfile.TemporaryDirectory(prefix='reader-harden-build-') as tmp:
    work = Path(tmp); cpp = work / 'fixture.cpp'; cpp.write_bytes(source); binary = work / 'fixture'
    flags = rust_link_flags(); i18n = generated_i18n_directory(flags[-1])
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-Wno-format', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-DBOOK_RENDER_OWNER=1', '-I', str(HERE), '-I', str(i18n),
                    str(cpp), *flags, '-o', str(binary)], check=True)
    traces = []
    for fault in [0, 1, 2, 3]:
        trace = subprocess.check_output([str(binary), str(fault)])
        assert trace.count(b'build-start 1') == 2
        assert trace.count(b'save-spine 1') == 2
        assert trace.count(b'page-load ') == 2
        traces.append(trace)
    assert all(trace == traces[0] for trace in traces), 'failed estimates must match no-estimate build/render/save'
print('PASS: first, second and both size bounds fail; uncached and partial/offset sections build, render and save')
