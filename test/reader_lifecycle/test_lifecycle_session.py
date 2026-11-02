"""Compare the complete lifecycle oracle through ordinary Rust EPUB owners.

Shared reader/storage helpers and original case order remain unchanged. No
candidate filtering or expected-output projections are applied.
"""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ADAPTER = Path(__file__).resolve().parent
DEFAULT_ROOT = ADAPTER.parents[1] if ADAPTER.name == 'reader_lifecycle' else Path.cwd()
GZIP_SHA = '00a1cfdfaf3713c676f8ae99e7f29df0be50085cf6cf2645c0e91e17cc6ffc08'
RAW_SHA = '9721b653909ad8dbaacfaa7c11741e10e5cada4b02e917dd26ad3f108b6d1ab3'
EPUB = Path('crates/xp-ffi/src/app/epub')
CASE_COUNTS = {
    'session': 864, 'reset': 216,
    'turn-input': 288, 'turn-output': 288, 'no-section': 1,
    'gates': 8, 'wait': 12, 'build-input': 1024, 'build-output': 1024,
    'input': 96, 'input-state': 96,
    'save': 36, 'load': 36, 'save-failure': 4, 'load-size': 15,
    'links': 4, 'position': 462, 'links-invalid': 150, 'links-back': 150,
    'atomic-length': 4, 'atomic-result': 4,
    'refresh': 240, 'rtl': 10, 'gesture': 6, 'note': 50,
    'turn-lock': 12, 'progress-lifetime': 4, 'progress-done': 4,
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected exactly one source site', before)
    return source.replace(before, after, 1)


def case_counts(trace):
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in trace.splitlines())
    observed = {name: counts[name] for name in CASE_COUNTS}
    assert observed == CASE_COUNTS, observed
    return observed


def difference(actual, expected):
    left, right = actual.splitlines(), expected.splitlines()
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return {'line': index + 1,
                    'actual': [line.decode(errors='replace') for line in left[max(0, index - 3):index + 4]],
                    'expected': [line.decode(errors='replace') for line in right[max(0, index - 3):index + 4]]}
    return {'actual_lines': len(left), 'expected_lines': len(right)}


def shared_methods(root, definition):
    source = (root / 'src/activities/reader/ReaderUtils.h').read_text()
    signatures = ('inline bool gestureAllowsSwipe(', 'inline bool gestureAllowsTap(',
'inline void displayWithRefreshCycle(',
                  'inline void displayBaseWithRefreshCycle(')
    return 'namespace ReaderUtils {\n' + '\n'.join(definition(source, item) for item in signatures) + '\n}\n'


def transformed_fixture(template, adapter, root, definition):
    # The body, cases, printf expressions, and shared classes remain the original source.
    source = replace_once(template, '#include "build_wait_fixture.h"', '')
    source = replace_once(source, '  EpubReaderActivity() { section->renderer = &renderer; }',
                          '  EpubReaderActivity() { section->renderer = &renderer; }\n'
                          '  mutable void* lifecycleSession = nullptr;\n'
                          '  ~EpubReaderActivity();')
    source = replace_once(source, '#include "reader_methods.inc"',
                          (adapter / 'lifecycle_session_rust.inc').read_text() + '\n' + shared_methods(root, definition))
    return replace_once(source, '  progressFileLifetime();\n}',
                        '  progressFileLifetime();\n  lifecycle_session::assertComplete();\n}')


def mutate_method(source, signature, before, after, definition):
    method = definition(source, signature)
    return replace_once(source, method, replace_once(method, before, after))


def rust_mutants(sources, definition):
    # Mutations change ordinary Rust methods, never fixture behavior or shared C++ wrappers.
    path = EPUB / 'loop_owner.rs'
    yield ('manual-gap-201ms', path,
           mutate_method(sources[path], 'pub(super) fn manual_turn_input(', '< 200;', '< 201;', definition))
    path = EPUB / 'projection.rs'
    yield ('turn-clock-not-recorded', path,
           mutate_method(sources[path], 'pub(super) fn page_turn(',
                         'self.apply_page_move(movement, true)', 'self.apply_page_move(movement, false)', definition))
    path = EPUB / 'build.rs'
    yield ('lower-build-free-threshold', path,
           replace_once(sources[path], 'const MIN_FREE: u32 = 32 * 1024;', 'const MIN_FREE: u32 = 31 * 1024;'))
    path = EPUB / 'lifetime.rs'
    yield ('discard-loaded-offset', path,
           mutate_method(sources[path], "pub(super) fn load_progress<'a>(",
                         'state.cached.offset = Some(progress.offset);', 'state.cached.offset = None;', definition))
    path = EPUB / 'navigation.rs'
    yield ('restore-consumes-all-history', path,
           mutate_method(sources[path], 'pub(super) fn restore_position(',
                         'state.history.depth -= 1;', 'state.history.depth = 0;', definition))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=DEFAULT_ROOT)
    parser.add_argument('--adapter-dir', type=Path, default=ADAPTER)
    parser.add_argument('--interface-dir', type=Path,
                        help='Directory containing the main-owned lifecycle_session_fixture.h; defaults to adapter directory')
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_LIFECYCLE_EVIDENCE_DIR'))
    args = parser.parse_args()
    root = args.repo.resolve()
    here = root / 'test/reader_lifecycle'
    adapter = args.adapter_dir.resolve()
    interface = (args.interface_dir or adapter).resolve()
    header = interface / 'lifecycle_session_fixture.h'
    assert header.is_file(), ('pass --interface-dir containing the frozen lifecycle header', str(header))
    sys.path.insert(0, str(here))
    from extract import definition
    sys.path.insert(0, str(root / 'test'))
    from rust_host import rust_link_flags

    golden = here / 'lifecycle.golden.gz'
    compressed = golden.read_bytes()
    raw = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(raw) == RAW_SHA
    assert len(raw) == 156224 and len(raw.splitlines()) == 6824
    counts = case_counts(raw)
    template = (here / 'oracle.cpp').read_text()
    fixture = transformed_fixture(template, adapter, root, definition)
    sources = {EPUB / name: (root / 'rust' / EPUB / name).read_text()
               for name in ('loop_owner.rs', 'projection.rs', 'build.rs', 'lifetime.rs', 'navigation.rs')}
    # Freeze mutation sites before spending time compiling. A stale/missing method is an explicit error.
    mutants = list(rust_mutants(sources, definition))
    tracked = [here / 'oracle.cpp', here / 'extract.py', here / 'build_wait_fixture.h', golden, header,
               adapter / 'lifecycle_session_rust.inc', Path(__file__).resolve(),
               root / 'src/activities/reader/ReaderSession.cpp', root / 'src/activities/reader/ReaderSession.h',
               root / 'src/activities/reader/ReaderUtils.h', root / 'src/activities/reader/EpubReaderUtils.h',
               root / 'src/activities/reader/ProgressFile.h', root / 'src/activities/reader/RemoteTurnGate.h',
               here / 'reader_link_note_owner.h', here / 'reader_value_owner.h',
               root / 'test/ble_link_note_title/owner_tables.h', root / 'rust/crates/xp-ffi/include/xp_epub_session.h',
               root / 'rust/crates/xp-ffi/include/xp_ui_reader.h', root / 'rust/crates/xp-ffi/include/xp_app.h']
    tracked.extend(sorted((here / 'stubs').rglob('*.h')))
    tracked.extend(sorted((root / 'rust' / EPUB).rglob('*.rs')))
    hashes = {str(path.resolve()): digest(path.read_bytes()) for path in tracked}
    with tempfile.TemporaryDirectory(prefix='reader-lifecycle-session-') as directory:
        work = Path(directory)
        evidence = args.evidence_dir / 'reader_lifecycle_oracle' if args.evidence_dir else work / 'evidence'
        evidence.mkdir(parents=True, exist_ok=True)
        (evidence / 'original.trace').write_bytes(raw)
        (evidence / 'candidate.cpp').write_text(fixture)
        flags = rust_link_flags()
        try:
            def logged(command, name, cwd=None):
                result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                (evidence / (name + '.command.json')).write_text(json.dumps(command, indent=2) + '\n')
                assert result.returncode == 0, (name, result.returncode, result.stderr.decode(errors='replace'))
                return result

            def compile_fixture(name, source, archive=None):
                path = work / (name + '.cpp')
                path.write_text(source)
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                binary = work / name
                command = ['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                           '-fno-sanitize-recover=all', '-I', str(here / 'stubs'), '-I', str(work), '-I', str(interface), '-I', str(here),
                           '-I', str(root / 'src/activities/reader'), '-I', str(root / 'lib/Epub'),
                           '-I', str(root / 'lib/BlePageTurner/include'),
                           '-I', str(root / 'freeink-sdk/libs/network/BleKeyboardHost/include'),
                           str(path), str(root / 'src/activities/reader/ReaderSession.cpp'),
                           *link, '-o', str(binary)]
                logged(command, name + '-compile')
                return binary

            def capture(name, binary):
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result

            candidate = capture('candidate', compile_fixture('candidate', fixture))
            assert candidate.returncode == 0, candidate.stderr.decode(errors='replace')
            assert candidate.stdout == raw, difference(candidate.stdout, raw)
            assert case_counts(candidate.stdout) == counts
            summary = {
                'test_name': 'reader_lifecycle_oracle',
                'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': RAW_SHA,
                'trace_lines': 6824, 'trace_bytes': 156224, 'case_counts': counts,
                'candidate_trace_sha256': digest(candidate.stdout),
                'owners': 1566, 'input_owners': 12, 'input_frames': 96, 'room_calls': 20,
                'section_publications': 1549, 'source_sha256': hashes,
                'baseline': 'immutable lifecycle.golden.gz', 'projections': {},
                'policy': 'complete immutable golden; one retained owner per fixture; shared cases unchanged; no output filtering',
                'physical_boundaries': 'original HalFile/Section/RenderLock; original clear stub with explicit pending-page setter',
                'mutants': [],
            }
            (evidence / 'baseline.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(root / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(root / name, target_is_directory=True)
            target = work / 'target'
            for name, path, changed in mutants:
                for original_path, source in sources.items():
                    (copied_rust / original_path).write_text(source)
                (copied_rust / path).write_text(changed)
                (evidence / (name + '.rs')).write_text(changed)
                logged(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], name + '-rust-build', cwd=copied_rust)
                result = capture(name, compile_fixture(name, fixture, target / 'release/libxp_ffi.a'))
                if name == 'lower-build-free-threshold':
                    assert result.returncode == 42 and result.stderr == (
                        b'EXPECTED_ASSERTION: LIFECYCLE_PUBLICATION_COUNT expected=1549 actual=1553\n'
                    ), ('wrong threshold-mutant failure', result.returncode, result.stderr.decode(errors='replace'))
                else:
                    assert result.returncode == 0, ('mutant did not complete', name, result.returncode,
                                                    result.stderr.decode(errors='replace'))
                assert case_counts(result.stdout) == counts
                killed = result.stdout != raw
                record = {'name': name, 'rust_source': str(path), 'source_sha256': digest(changed.encode()),
                          'compiled': True, 'killed': killed, 'returncode': result.returncode,
                          'trace_sha256': digest(result.stdout), 'difference': difference(result.stdout, raw)}
                summary['mutants'].append(record)
                (evidence / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
                print(json.dumps(record), flush=True)
                assert killed, ('ordinary Rust mutant survived unchanged complete capture', name)
            assert len(summary['mutants']) == 5
            (evidence / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        finally:
            for path, expected_hash in hashes.items():
                assert digest(Path(path).read_bytes()) == expected_hash, ('source changed during run', path)
            assert digest(golden.read_bytes()) == GZIP_SHA


if __name__ == '__main__':
    main()
