"""Count allocations/callbacks in the actual toolbar resource window vs C++."""
from pathlib import Path
import subprocess
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

frozen = (HERE / 'toolbar.cpp').read_text()
frozen = frozen[:frozen.index('int main() {')]
source = (ROOT / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
methods = source[source.index('const char* ReaderToolbarUi::text('):re.search(r'(?:void|bool) ReaderToolbarUi::render\(\)', source).start()]
fixture = (HERE / 'toolbar_resources.cpp').read_text().replace('// RESOURCE_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    # ResourceTarget maps font slots like the native text target, with fixed oracle metrics.
    (p / 'widgets.cpp').write_text((HERE / 'widgets.cpp').read_text().replace('class Recorder final', 'class Recorder'))
    (p / 'resources.cpp').write_text(frozen + fixture)
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-DFREEINK_UI_COMPONENT_STYLE=0',
                    '-I' + str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
                    str(p / 'resources.cpp'), str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'),
                    *rust_link_flags(), '-o', str(p / 'resources')], check=True)
    result = subprocess.check_output([str(p / 'resources')], text=True)
    print('\n'.join(line for line in result.splitlines() if line.startswith('resources ')))
