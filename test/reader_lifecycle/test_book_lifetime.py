"""Compare real session loading/teardown with the unchanged lifetime references.

Eight typed app-test seams preserve the historical compositional stubs; their
inner implementations have separate helper, read-folder and bookmark oracles.
The fixture's error string is an observation of the production failure tag for
the two historical cases (None/Expired), never a copy of the input error.

Native BookHandle promotion remains inside book_load under the coordinator's
ruling: it moves before loan_drop, while the old shared_ptr assignment followed
loan_drop. There is no promotion trace event, and loan teardown has no heap call.
The separate campaign allocation-stream gate still owns that ordering evidence.
"""
from pathlib import Path
import gzip
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

if '--freeze-original' in sys.argv:
    raise SystemExit('Book lifetime goldens are frozen; regeneration is not supported.')
release = '--release-log' in sys.argv
fixture = (HERE / 'book_lifetime_oracle.cpp').read_text().replace(
    '#include "book_lifetime_session_rust.inc"', (HERE / 'book_lifetime_session_rust.inc').read_text())
production_path = Path('crates/xp-ffi/src/app/epub/lifetime.rs')
production_file = ROOT / 'rust' / production_path
production = production_file.read_text()
golden = HERE / ('book-lifetime-release.golden.gz' if release else 'book-lifetime.golden.gz')
frozen_bytes = golden.read_bytes()
expected = gzip.decompress(frozen_bytes)


def digest(source):
    return hashlib.sha256(source.encode()).hexdigest()


def mutate(source, before, after):
    assert source.count(before) == 1, f'mutant site must occur once: {before}'
    return source.replace(before, after, 1)


with tempfile.TemporaryDirectory(prefix='reader-book-lifetime-') as directory:
    work = Path(directory)
    source_path = work / 'book.cpp'
    source_path.write_text(fixture)
    baseline_flags = rust_link_flags()

    def run(name, archive=None):
        executable = work / name
        flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                        '-I', str(HERE), '-I', str(ROOT / 'lib/I18N'),
                        *(['-DLIFETIME_RELEASE_LOG'] if release else []), str(source_path),
                        *flags, '-o', str(executable)], check=True)
        return subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    result = run('book')
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    assert result.stdout == expected, 'production lifetime trace differs from frozen reference'
    copied_repo = work / 'source'
    copied_rust = copied_repo / 'rust'
    shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
    for name in ('scripts', 'lib', 'src'):
        (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
    copied_production = copied_rust / production_path
    assert copied_production.read_text() == production
    target = work / 'target'
    mutants = [
        ('framebuffer-loan', 'let _loan = if uncached {', 'let _loan = if false {'),
        ('text-reference', '(self.leaves.book_text_reference)(book.as_ptr())', '0i32'),
        ('save-links', 'self.save_links(access);', '/* deliberate mutant: omit link-stack save */'),
        ('progress-file-lifetime', 'let _progress_file = self.load_progress(access);',
         'drop(self.load_progress(access));'),
    ]
    try:
        for name, before, after in mutants:
            changed = mutate(production, before, after)
            copied_production.write_text(changed)
            print(json.dumps({'mutant': name, 'release': release, 'replacement_count': 1,
                              'original_sha256': digest(production), 'source_sha256': digest(changed)}), flush=True)
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            wrong = run(name, target / 'release/libxp_ffi.a')
            assert wrong.returncode != 0 or wrong.stdout != expected, f'mutant survived: {name}'
    finally:
        assert production_file.read_text() == production
        assert golden.read_bytes() == frozen_bytes
    print(f'book lifetime ({"release" if release else "debug"}): {len(expected.splitlines())} frozen lines '
          'across 576 load and 32 teardown cases; four production mutants rejected; '
          'failed-open destruction silent, successful progress handle spans all restoration calls')
