"""Compare real Rust navigation methods with the frozen 1,882-case trace."""
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
    raise SystemExit('Navigation goldens are frozen; regeneration is not supported.')

fixture = (HERE / 'navigation_oracle.cpp').read_text().replace(
    '#include "navigation_rust.inc"', (HERE / 'navigation_rust.inc').read_text())
navigation_path = Path('crates/xp-ffi/src/app/epub/navigation.rs')
production = (ROOT / 'rust' / navigation_path).read_text()


def digest(source):
    return hashlib.sha256(source.encode()).hexdigest()


def mutate(source, replacements):
    for before, after in replacements:
        assert source.count(before) == 1, f'mutant site must occur once: {before}'
        source = source.replace(before, after, 1)
    return source


with tempfile.TemporaryDirectory(prefix='reader-navigation-') as directory:
    work = Path(directory)
    baseline_flags = rust_link_flags()

    def run(source, name, archive=None):
        source_path = work / f'{name}.cpp'
        source_path.write_text(source)
        flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                        '-I', str(HERE), str(source_path), *flags,
                        '-o', str(work / name)], check=True)
        return subprocess.check_output([str(work / name)])

    actual = run(fixture, 'navigation')
    assert actual == gzip.decompress((HERE / 'navigation.golden.gz').read_bytes())
    wrong_book = mutate(fixture, [
        ('return static_cast<Epub*>(book)->getBookSize();', 'return 0;'),
    ])
    assert run(wrong_book, 'book-size') != actual
    print(json.dumps({'mutant': 'book-size', 'source_sha256': digest(wrong_book),
                      'original_sha256': digest(fixture), 'replacement_counts': [1]}), flush=True)

    copied_repo = work / 'source'
    copied_rust = copied_repo / 'rust'
    shutil.copytree(ROOT / 'rust', copied_rust,
                    ignore=shutil.ignore_patterns('target', '__pycache__'))
    # Build scripts only read these inputs; generated output stays in the private target.
    for name in ('scripts', 'lib', 'src'):
        (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
    copied_navigation = copied_rust / navigation_path
    assert copied_navigation.read_text() == production
    target = work / 'target'
    mutants = [
        ('anchor-clear', [
            ('self.set_anchor(&access, anchor)',
             'self.clear_anchor(&access)'),
        ]),
        ('explicit-page', [
            ('self.model(|state| state.current.page = i32::from(page));',
             'self.model(|state| state.current.page = i32::from(page) + 1);'),
            ('state.current.page = target_page.max(0);',
             'state.current.page = target_page.max(0) + 1;'),
            ('self.model(|state| state.current.page = page);',
             'self.model(|state| state.current.page = page + 1);'),
        ]),
    ]
    for name, replacements in mutants:
        changed = mutate(production, replacements)
        copied_navigation.write_text(changed)
        print(json.dumps({'mutant': name, 'source_sha256': digest(changed),
                          'original_sha256': digest(production),
                          'replacement_counts': [1] * len(replacements)}), flush=True)
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        assert run(fixture, name, target / 'release/libxp_ffi.a') != actual
    assert (ROOT / 'rust' / navigation_path).read_text() == production
    print(f'navigation: {len(actual.splitlines())} frozen lines, 1882 cases; '
          'three mutants rejected, render/model lock depth returns to zero')
