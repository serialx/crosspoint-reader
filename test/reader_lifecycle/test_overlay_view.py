"""Compare real owner overlay models and rows with both immutable historical traces."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
from test_reader_settings import generated_i18n_directory

TEST_NAME = 'reader_overlay_view'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/overlay_view.rs')
GOLDENS = {
    'overlay-view.golden.gz': 'e92e88902e1002dc490b564a1b07f94e4f7b04c4380bc08502a6843802619f26',
    'overlay-view-bound.golden.gz': 'c3ffd407d94dafa4ecb1573cac59e74532197e02ed572a7b8913c0c50fce36f0',
}
RUST_MUTANTS = (
    ('selected-index', b'self.model(|state| state.panel_index)', b'0'),
    ('contents-indentation', b'(item.level - 1) as u32 * 2', b'(item.level - 1) as u32 * 1'),
    ('focus-checkbox-row', b'Overlay::Text if index == 4', b'Overlay::Text if index == 3'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one actual source site', before, source.count(before))
    return source.replace(before, after, 1)


def equal_trace(actual, expected, label):
    if actual == expected:
        return
    for index, (a, e) in enumerate(zip(actual.splitlines(), expected.splitlines()), 1):
        assert a == e, (label, index, e, a)
    raise AssertionError((label, 'trace length changed', len(expected), len(actual)))


def main():
    if len(sys.argv) != 1:
        raise SystemExit('Overlay-view goldens are immutable; regeneration is not supported.')
    originals = {name: (HERE / name).read_bytes() for name in GOLDENS}
    for name, value in originals.items():
        assert digest(value) == GOLDENS[name], ('original golden changed', name)
    expected = gzip.decompress(originals['overlay-view.golden.gz'])
    expected_bounds = gzip.decompress(originals['overlay-view-bound.golden.gz'])
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in expected.splitlines())
    assert counts['case'] == 960 and counts['missing'] == 3
    assert sum(line.startswith(b'model ') for line in expected_bounds.splitlines()) == 5
    fixture = replace_once((HERE / 'overlay_view_oracle.cpp').read_bytes(),
                           b'#include "overlay_view_session_rust.inc"',
                           (HERE / 'overlay_view_session_rust.inc').read_bytes())
    production_path = ROOT / 'rust' / PRODUCTION
    production = production_path.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'overlay_view_oracle.cpp', HERE / 'overlay_view_session_fixture.h',
        HERE / 'overlay_view_session_rust.inc', Path(__file__),
        HERE / 'test_reader_settings.py',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/overlay_view.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-overlay-view-owner-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def compile_candidate(name, archive=None):
                path = work / (name + '.cpp')
                path.write_bytes(fixture)
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                i18n = generated_i18n_directory(flags[-1])
                binary = work / name
                command = ['c++', '-std=c++17', '-UNDEBUG', '-O0', '-DOVERLAY_VIEW_OWNER=1',
                           '-fsanitize=address,undefined', '-I', str(HERE), '-I', str(i18n),
                           str(path), *flags, '-o', str(binary)]
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (name + '.compile.log')).write_bytes(result.stdout + result.stderr)
                assert result.returncode == 0, (name, 'compile failure is not mutant rejection',
                                                result.stderr.decode(errors='replace'))
                return binary

            def run(binary, name, argument=None):
                result = subprocess.run([str(binary)] + ([argument] if argument else []),
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (name + '.run.log')).write_bytes(result.stdout + result.stderr)
                return result

            binary = compile_candidate('baseline')
            baseline = run(binary, 'baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            equal_trace(baseline.stdout, expected, 'overlay')
            bounds = run(binary, 'bounds', '--bounds')
            assert bounds.returncode == 0, bounds.stderr.decode(errors='replace')
            equal_trace(bounds.stdout, expected_bounds, 'bounds')
            labels = [line.split(b'] [')[1] for line in bounds.stdout.splitlines()
                      if line.startswith(b'model ')]
            assert max(map(len, labels)) == 30
            assert all(len(label) + 1 <= 40 for label in labels)
            borrowed = run(binary, 'real-borrows', '--real-borrows')
            assert borrowed.returncode == 0, borrowed.stderr.decode(errors='replace')
            assert borrowed.stdout.endswith(
                b'real overlay borrows: 4096-byte chapter and TOC row retain native pointer identity through sinks\n')
            print(json.dumps({
                'test_name': TEST_NAME, 'cases': 960, 'missing_resources': 3, 'bounds_cases': 5,
                'trace_lines': len(expected.splitlines()), 'trace_bytes': len(expected),
                'bounds_lines': len(expected_bounds.splitlines()), 'bounds_bytes': len(expected_bounds),
                'event_counts': counts, 'original_gzip_sha256': GOLDENS,
                'original_trace_sha256': digest(expected), 'actual_trace_sha256': digest(baseline.stdout),
                'bounds_trace_sha256': digest(bounds.stdout),
                'candidate_source_sha256': digest(production), 'fixture_sha256': fixture_hashes,
                'policy_replacements': ['currentChapterTitle', 'textRowName', 'textRowValue',
                                        'moreRowName', 'moreRowValue'],
                'covering_inner_oracle': 'reader_view_values',
                'real_borrow_cases': ['4096-byte chapter through native TOC/toolbar consumer',
                                      '4096-byte TOC row with separate six-space prefix'],
                'real_borrow_output_sha256': digest(borrowed.stdout),
                'scope': 'caller-held resource token; five historical hooks; real ordinary owner model/rows',
                'limitations': 'pointer identity proves no replacement text copy at sinks, not allocation-stream parity; '
                               'paint-success fixture does not establish failed-scene or full entry integration',
            }), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            for name, before, after in RUST_MUTANTS:
                changed = replace_once(production, before, after)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                mutant = compile_candidate(name, target / 'release/libxp_ffi.a')
                result = run(mutant, name)
                assert result.returncode != 0 or result.stdout != expected, (name, 'actual Rust mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'kind': 'production-rust', 'replacement_count': 1,
                    'source': str(PRODUCTION), 'original_source_sha256': digest(production),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                }), flush=True)
    finally:
        for name, value in originals.items():
            assert (HERE / name).read_bytes() == value, ('immutable golden changed', name)
        assert production_path.read_bytes() == production, 'actual production source changed'
    print(f'{TEST_NAME}: 960 cases plus three missing resources and five bounds; '
          'real 4096-byte borrowed title/row; three actual Rust mutants rejected')


if __name__ == '__main__':
    main()
