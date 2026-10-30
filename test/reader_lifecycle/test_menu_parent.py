"""Freeze reader menu parents, child results and More-panel actions."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
freeze = '--freeze-original' in sys.argv
release = '--release-log' in sys.argv
source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
          if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
methods = '\n'.join(definition(source, signature) for signature in [
    'void EpubReaderActivity::openReaderMenu(', 'void EpubReaderActivity::onReaderMenuConfirm(',
    'void EpubReaderActivity::buildMoreActions(', 'void EpubReaderActivity::activateMoreRow(',
])
if 'void EpubReaderActivity::runMenuPolicy(' in source:
    methods += '\n' + definition(source, 'void EpubReaderActivity::runMenuPolicy(')
fixture = (HERE / 'menu_parent_oracle.cpp').read_text().replace('// MENU_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    for strip in [0, 1]:
        def run(text):
            (path / 'menu.cpp').write_text(text)
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-Wno-switch', '-fsanitize=address,undefined',
                            f'-DXP_STRIP_RENDER={strip}', *(['-DMENU_RELEASE_LOG'] if release else []),
                            str(path / 'menu.cpp'), *rust_link_flags(), '-o', str(path / 'menu')], check=True)
            return subprocess.check_output([str(path / 'menu')])
        actual = run(fixture)
        golden = HERE / f'menu-parent-{strip}{"-release" if release else ""}.golden.gz'
        if freeze:
            golden.write_bytes(gzip.compress(actual, mtime=0))
            for before, after in [('return saveOk;', 'return true;'),
                                  ('return toolbar;', 'return false;'),
                                  ('event("clear-deferred", lockDepth);', 'event("clear-deferred", 0);')]:
                wrong = fixture.replace(before, after)
                assert wrong != fixture and run(wrong) != actual
        else:
            assert actual == gzip.decompress(golden.read_bytes()), (strip, release)
        print(f'menu parent strip={strip} release={release}: {len(actual.splitlines())} original trace lines')
