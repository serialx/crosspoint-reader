"""Exercise production toolbar navigation during a busy paint with ASan/UBSan."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

source = (ROOT / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
methods = source[source.index('void ReaderToolbarUi::begin()'):source.index('ReaderToolbarUi::Routed ReaderToolbarUi::route(')]
fixture = (HERE / 'toolbar_publication.cpp').read_text().replace('// TOOLBAR_NAVIGATION_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'publication.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    str(p / 'publication.cpp'), *rust_link_flags(), '-o', str(p / 'publication')], check=True)
    subprocess.run([str(p / 'publication')], check=True)
    (p / 'abi.c').write_text('''#include <xp_ui_reader.h>
_Static_assert(sizeof(((XpUiListState*)0)->navigation_revision) == 4, "revision width");
void (*publish)(XpUiListState*, const XpUiListState*) = xp_ui_reader_toolbar_nav_publish;
''')
    subprocess.run(['cc', '-std=c11', '-Werror', '-fsyntax-only', str(p / 'abi.c'),
                    '-I' + str(ROOT / 'rust/crates/xp-ffi/include')], check=True)
    # A header/export spelling drift must fail compilation, not silently omit coverage.
    (p / 'drift.cpp').write_text(fixture.replace('xp_ui_reader_toolbar_nav_publish', 'xp_ui_reader_toolbar_nav_publish_drift'))
    drift = subprocess.run(['c++', '-std=c++17', '-fsyntax-only', str(p / 'drift.cpp'),
                            '-I' + str(ROOT / 'rust/crates/xp-ffi/include')], capture_output=True, text=True)
    assert drift.returncode != 0 and 'xp_ui_reader_toolbar_nav_publish_drift' in drift.stderr
    print('toolbar publication: reset/scroll survive busy paint; original publication loses both; ASan/UBSan passed')
