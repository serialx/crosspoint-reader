"""Compare real owner projections against the immutable, explicitly projected trace."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

TEST_NAME = 'reader_projection'
PROJECTION = Path('crates/xp-ffi/src/app/epub/projection.rs')
POSITION = Path('crates/xp-ffi/src/app/epub/position.rs')
GZIP_SHA = '5c487f4b048d098c7a73076e33842133596d4d83d4e4e7469ca3284d4d49d82b'
RAW_SHA = '64aba34e646ddb061e380bdbc3e8e6d5780a00dda74c32157a6c18ae64946562'
MUTANTS = (
    ('paragraph-page-one', POSITION, b'paragraph(if page > 0 {', b'paragraph(if page > 1 {'),
    ('percent-upper-bound', PROJECTION, b'progress.clamp(0.0, 1.0)', b'progress.clamp(0.0, 0.5)'),
    ('touch-threshold', PROJECTION, b'if unsafe { input.assume_init() }.board_touch != 0 {',
     b'if false {'),
    ('cross-spine-lock-omitted', PROJECTION, b'let access = self.lock_resources();',
     b'let access = unsafe { self.borrowed_resources() };'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(data, before, after):
    assert data.count(before) == 1, ('mutation must match one site', before)
    return data.replace(before, after, 1)


def project(raw):
    output = []
    counts = dict(paragraph=0, offset=0, cached_drop=0)
    for line in raw.splitlines(keepends=True):
        if line.startswith((b'paragraph ', b'offset ')):
            match = re.fullmatch(rb'((paragraph|offset) [0-9]+) 0\n', line)
            assert match, line
            counts[match[2].decode()] += 1
            output.extend((b'lock 1 0\n', match[1] + b' 1\n', b'unlock 1 0\n'))
        elif line == b'cached-drop 0 0\n':
            counts['cached_drop'] += 1
            output.append(b'cached-drop 1 0\n')
        else:
            output.append(line)
    assert counts == dict(paragraph=1152, offset=1152, cached_drop=1152)
    result = b''.join(output)
    assert len(result.splitlines()) == 220551
    # No result or harness state bytes are normalized by the lock projection.
    result_prefixes = (b'position ', b'screenshot ', b'state ', b'result ', b'percent ', b'basis ')
    assert [line for line in result.splitlines() if line.startswith(result_prefixes)] == [
        line for line in raw.splitlines() if line.startswith(result_prefixes)]
    return result, counts


def generated_i18n_directory(archive):
    headers = sorted((Path(archive).resolve().parent / 'build').glob('xp-core-*/out/I18nKeys.h'))
    assert headers, ('missing generated I18nKeys.h for archive', str(archive))
    assert len({path.read_bytes() for path in headers}) == 1, ('conflicting generated I18n headers', headers)
    return headers[0].parent


def difference(expected, actual):
    for line, (left, right) in enumerate(zip(expected.splitlines(), actual.splitlines()), 1):
        if left != right:
            return line, left, right
    return 'length mismatch', len(expected), len(actual)


def main():
    if len(sys.argv) != 1:
        raise SystemExit('Projection goldens are immutable; regeneration is not supported.')
    golden = HERE / 'projection.golden.gz'
    compressed = golden.read_bytes()
    raw = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(raw) == RAW_SHA
    assert len(raw) == 3683754 and len(raw.splitlines()) == 215943
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in raw.splitlines())
    assert (counts['case'], counts['action'], counts['threshold-case'], counts['float']) == (4608, 27648, 40, 7)
    expected, projections = project(raw)
    fixture = (HERE / 'projection_oracle.cpp').read_bytes()
    marker = b'#include "projection_session_rust.inc"'
    assert fixture.count(marker) == 1
    fixture = fixture.replace(marker, (HERE / 'projection_session_rust.inc').read_bytes())
    production = {path: (ROOT / 'rust' / path).read_bytes() for path in (PROJECTION, POSITION)}
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'projection_oracle.cpp', HERE / 'projection_session_rust.inc', HERE / 'projection_session_fixture.h',
        HERE / 'projection_abi.cpp', Path(__file__), ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/projection.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-projection-session-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def compile_fixture(source, name, archive=None):
                path = work / (name + '.cpp')
                path.write_bytes(source)
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                i18n = generated_i18n_directory(baseline_flags[-1] if archive is None else archive)
                executable = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(HERE), '-I', str(i18n), '-I', str(ROOT / 'src'),
                                '-I', str(ROOT / 'src/activities/reader'), '-I', str(ROOT / 'lib/KOReaderSync'),
                                str(path), *flags, '-o', str(executable)], check=True)
                return executable

            def run(executable, edges=False):
                return subprocess.run([str(executable), *(['--owner-edges'] if edges else [])],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            executable = compile_fixture(fixture, 'projection')
            baseline = run(executable)
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == expected, difference(expected, baseline.stdout)
            edges = run(executable, True)
            assert edges.returncode == 0, edges.stderr.decode(errors='replace')
            assert edges.stdout.endswith(b'owner-edges 6 float 20 movement cases\n')
            # Retain the old facade's ABI/null-service checks as a separate executable.
            legacy = compile_fixture((HERE / 'projection_abi.cpp').read_bytes(), 'legacy-abi')
            legacy_result = run(legacy)
            assert legacy_result.returncode == 0, legacy_result.stderr.decode(errors='replace')
            print(json.dumps({
                'test_name': TEST_NAME, 'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': RAW_SHA,
                'projected_trace_sha256': digest(expected), 'actual_trace_sha256': digest(baseline.stdout),
                'original_lines': 215943, 'projected_lines': 220551, 'projection_event_counts': projections,
                'original_event_counts': counts, 'owner_edge_float_cases': 6, 'owner_edge_movement_cases': 20,
                'owner_edge_trace_sha256': digest(edges.stdout), 'legacy_abi_retained': True,
                'production_sha256': {str(path): digest(data) for path, data in production.items()},
                'fixture_sha256': fixture_hashes,
            }), flush=True)

            wrong = replace_once(fixture, b'constexpr bool holdTocLock = true;', b'constexpr bool holdTocLock = false;')
            result = run(compile_fixture(wrong, 'toc-caller-lock-omitted'))
            assert result.returncode != 0 and b'lockDepth == 1' in result.stderr, result.stderr
            print(json.dumps({
                'test_name': TEST_NAME, 'mutant': 'toc-caller-lock-omitted', 'kind': 'caller-contract',
                'replacement_count': 1, 'original_source_sha256': digest(fixture), 'mutant_source_sha256': digest(wrong),
                'returncode': result.returncode, 'stderr_sha256': digest(result.stderr),
            }), flush=True)

            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            for path, data in production.items():
                assert (copied_rust / path).read_bytes() == data
            target = work / 'target'
            for name, path, before, after in MUTANTS:
                for reset_path, data in production.items():
                    (copied_rust / reset_path).write_bytes(data)
                changed = replace_once(production[path], before, after)
                (copied_rust / path).write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                mutant = compile_fixture(fixture, name, target / 'release/libxp_ffi.a')
                result = run(mutant)
                edge_result = run(mutant, True)
                assert (result.returncode != 0 or result.stdout != expected or edge_result.returncode != 0), (name, 'mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'kind': 'production-rust', 'replacement_count': 1,
                    'source': str(path), 'original_source_sha256': digest(production[path]),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                    'owner_edge_returncode': edge_result.returncode, 'owner_edge_stderr_sha256': digest(edge_result.stderr),
                }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        for path, data in production.items():
            assert (ROOT / 'rust' / path).read_bytes() == data, ('real production source changed', path)
    print(f'{TEST_NAME}: 220551 projected lines; owner value/move edges and legacy ABI; four Rust mutants and caller-lock mutant rejected')


if __name__ == '__main__':
    main()
