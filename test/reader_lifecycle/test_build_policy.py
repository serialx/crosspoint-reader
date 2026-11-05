"""Compare real Rust chapter admission/build methods with frozen platform traces."""
from pathlib import Path
import gzip
import hashlib
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

if '--freeze-original' in sys.argv:
    raise SystemExit('Build-policy goldens are frozen; regeneration is not supported.')
release = '--release-log' in sys.argv
fixture = (HERE / 'build_policy_oracle.cpp').read_text().replace(
    '#include "build_session_rust.inc"', (HERE / 'build_session_rust.inc').read_text())
mutations = [
    ('free-heap', 'return free;', 'return 100000;'),
    ('raw-contact', 'return rawContact;', 'return false;'),
    ('framebuffer', 'return framebuffer;', 'return true;'),
]

with tempfile.TemporaryDirectory(prefix='reader-build-policy-') as directory:
    path = Path(directory)
    flags = rust_link_flags()
    for strip in (0, 1):
        def run(source, name):
            source_path = path / f'{name}.cpp'
            source_path.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                            f'-DXP_STRIP_RENDER={strip}', *(['-DBUILD_RELEASE_LOG'] if release else []),
                            '-I', str(HERE), '-I', str(ROOT / 'lib/I18n'),
                            str(source_path), *flags, '-o', str(path / name)], check=True)
            return subprocess.check_output([str(path / name)])

        actual = run(fixture, 'build')
        golden = HERE / f'build-policy-{strip}{"-release" if release else ""}.golden.gz'
        assert actual == gzip.decompress(golden.read_bytes()), (strip, release)
        for name, before, after in mutations:
            assert fixture.count(before) == 1, f'mutant site must occur once: {before}'
            wrong = fixture.replace(before, after, 1)
            assert run(wrong, name) != actual, (name, strip, release)
            print(json.dumps({'mutant': name, 'strip': strip, 'release': release,
                              'source_sha256': hashlib.sha256(wrong.encode()).hexdigest(),
                              'replacement_count': 1}), flush=True)
        print(f'build policy strip={strip} release={release}: {len(actual.splitlines())} frozen lines; '
              '4390 cases, three physical mutants rejected')
