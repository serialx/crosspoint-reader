"""Freeze chapter admission, idle build scheduling, heap gates and popup timing."""
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
def source(path):
    return (subprocess.check_output(['git', 'show', '8d534a99:' + path], cwd=ROOT, text=True)
            if freeze else (ROOT / path).read_text())

reader = source('src/activities/reader/EpubReaderActivity.cpp')
methods = '\n'.join(definition(reader, signature) for signature in [
    'bool EpubReaderActivity::makeRoomForChapterBuild(',
    'bool EpubReaderActivity::buildTickHeapGate(',
    'void EpubReaderActivity::showBuildPopup(',
    'bool EpubReaderActivity::anyButtonPressed(',
    'void EpubReaderActivity::backgroundBuildTick(',
    'bool EpubReaderActivity::backgroundBuildWanted(',
    'bool EpubReaderActivity::skipLoopDelay(',
])
if freeze:
    methods += '\nvoid EpubReaderActivity::showBuildPopup() { showBuildPopup(renderer, refresh); }'
if 'bool EpubReaderActivity::runBuildPolicy(' in reader:
    methods += '\n' + definition(reader, 'bool EpubReaderActivity::runBuildPolicy(')
gate = source('src/activities/reader/ChapterBuildGate.h')
gate = '\n'.join(line for line in gate.splitlines() if not line.startswith(('#include', '#pragma')))
fixture = (HERE / 'build_policy_oracle.cpp').read_text().replace('// BUILD_GATE', gate).replace('// BUILD_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    for strip in [0, 1]:
        def run(text):
            (path / 'build.cpp').write_text(text)
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                            f'-DXP_STRIP_RENDER={strip}', *(['-DBUILD_RELEASE_LOG'] if release else []),
                            str(path / 'build.cpp'), *rust_link_flags(), '-o', str(path / 'build')], check=True)
            return subprocess.check_output([str(path / 'build')])
        actual = run(fixture)
        golden = HERE / f'build-policy-{strip}{"-release" if release else ""}.golden.gz'
        if freeze:
            golden.write_bytes(gzip.compress(actual, mtime=0))
            # Mutate platform leaves so both the original and the port must observe them.
            for before, after in [('return free;', 'return 100000;'),
                                  ('return rawContact;', 'return false;'),
                                  ('return framebuffer;', 'return true;')]:
                wrong = fixture.replace(before, after)
                assert wrong != fixture and run(wrong) != actual
        else:
            assert actual == gzip.decompress(golden.read_bytes()), (strip, release)
        print(f'build policy strip={strip} release={release}: {len(actual.splitlines())} frozen lines')
