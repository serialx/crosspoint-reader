"""Retain shared-reader checks, then compare the real EPUB base owner composition."""
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

TEST_NAME = 'reader_base_oracle'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/base.rs')
GZIP_SHA = '856bf8d1f234b3da90362a6e6833880da5675921b55bee61d8e3ff29a33e345c'
TRACE_SHA = '8ad8010f26e7697c31f27ad137db2cabdb4c92dfee5022491ac7b4ab118da1ee'
ALLOCATION = re.compile(rb'^(\d+) (allocate-bytes|release-bytes) (\d+) (\d+)$')
RUST_MUTANTS = (
    ('remember-before-first-paint', ((b'if state.base.remembered || !state.base.rendered {',
                                     b'if state.base.remembered {'),)),
    ('accept-invalid-document', ((b".all(|byte| matches!(byte, b'0'..=b'9' | b'a'..=b'f'))",
                                  b'.all(|_| true)'),)),
    ('continue-after-missing-file', ((b'if unsafe { (self.resources.path_exists)(self.path.wire()) } == 0 {',
                                     b'if unsafe { (self.resources.path_exists)(self.path.wire()) } == 2 {'),)),
    ('previous-wins-simultaneous-end-turn', ((b'if next {\n            self.go_home();',
                                            b'if next && !previous {\n            self.go_home();'),)),
    ('reset-after-document-release', (
        (b'// The native document remains borrowed until after reset.\n                self.reset_session();',
         b'// Deliberate mutant: reset after the native sink returns.'),
        (b'if consumed.is_none() {', b'if true {'),
    )),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('mutation must match exactly one site', before)
    return source.replace(before, after, 1)


def allocation_projection(trace):
    """Only generation-3 allocator-request sizes are placement diagnostics."""
    projected = []
    sizes = collections.Counter()
    for line in trace.splitlines(keepends=True):
        body = line.rstrip(b'\n')
        match = ALLOCATION.fullmatch(body)
        if match:
            event_id, operation, size, trailing = match.groups()
            assert trailing == b'0', ('unexpected allocator event schema', body)
            sizes[(operation.decode(), int(size))] += 1
            body = b' '.join((event_id, operation, b'<request-size>', trailing))
            projected.append(body + (b'\n' if line.endswith(b'\n') else b''))
        else:
            projected.append(line)
    return b''.join(projected), [{'operation': operation, 'bytes': size, 'count': count}
                                for (operation, size), count in sorted(sizes.items())]


def compare_trace(actual, expected):
    candidate, candidate_allocations = allocation_projection(actual)
    reference, reference_allocations = allocation_projection(expected)
    if candidate != reference:
        for line, (want, got) in enumerate(zip(reference.splitlines(), candidate.splitlines()), 1):
            if want != got:
                raise AssertionError(('owner trace differs', line, want, got))
        raise AssertionError(('owner trace length differs', len(reference.splitlines()), len(candidate.splitlines())))
    return reference, reference_allocations, candidate_allocations


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: test_base_owner.py <ReaderBaseOracle executable> <generated include directory>')
    shared_executable = Path(sys.argv[1]).resolve()
    generated = Path(sys.argv[2]).resolve()
    golden = HERE / 'base.golden.gz'
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(expected) == TRACE_SHA
    assert len(expected) == 931402 and len(expected.splitlines()) == 53786
    assert sum(line.startswith(b'CASE ') for line in expected.splitlines()) == 576
    assert (generated / 'base_action.inc').is_file()
    # Keep the original shared/XTC-facing facade comparison unchanged and first.
    subprocess.run([sys.executable, str(HERE / 'compare.py'), str(shared_executable), str(golden)], check=True)
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'base_oracle.cpp', HERE / 'base_owner_fixture.h', HERE / 'base_owner_rust.inc',
        Path(__file__), ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/base.rs',
        generated / 'base_action.inc')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-base-owner-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def compile_candidate(name, archive=None):
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                executable = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-DXP_BASE_OWNER', '-I', str(HERE), '-I', str(generated),
                                str(HERE / 'base_oracle.cpp'), *flags, '-o', str(executable)], check=True)
                return executable

            def run(executable, *args):
                return subprocess.run([str(executable), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            executable = compile_candidate('base-owner')
            baseline = run(executable)
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            normalized, original_allocations, candidate_allocations = compare_trace(baseline.stdout, expected)
            callbacks = run(executable, '--callbacks')
            assert callbacks.returncode == 0, callbacks.stderr.decode(errors='replace')
            assert callbacks.stdout.endswith(b'base owner callbacks: five 4096-byte paths stayed live through synchronous results\n')
            print(json.dumps({
                'test_name': TEST_NAME, 'original_shared_comparison': 'passed',
                'cases': 576, 'trace_lines': 53786, 'original_trace_bytes': 931402,
                'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': TRACE_SHA,
                'actual_trace_sha256': digest(baseline.stdout), 'normalized_trace_sha256': digest(normalized),
                'original_allocator_requests': original_allocations, 'candidate_allocator_requests': candidate_allocations,
                'allocation_projection': 'only size column of allocate-bytes/release-bytes; all other bytes exact',
                'candidate_source_sha256': digest(production), 'fixture_sha256': fixture_hashes,
                'long_path_cases': 5, 'long_path_bytes': 4096, 'long_path_trace_sha256': digest(callbacks.stdout),
                'session_getter_observation': 'snapshot copies scalar fixture values; five getters observed and checked at emit_session',
                'coverage_scope': 'base composition; historical inner stubs remain hooks; ready=true also after early entry failure',
                'exit_scope': 'historical depth-zero borrowed teardown; does not establish application exit locking',
            }), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            for name, replacements in RUST_MUTANTS:
                changed = production
                for before, after in replacements:
                    changed = replace_once(changed, before, after)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                mutant_executable = compile_candidate(name, target / 'release/libxp_ffi.a')
                result = run(mutant_executable)
                actual_projection, _ = allocation_projection(result.stdout)
                assert result.returncode != 0 or actual_projection != normalized, (name, 'actual Rust mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'kind': 'production-rust',
                    'replacement_counts': [1 for _ in replacements], 'source': str(PRODUCTION),
                    'original_source_sha256': digest(production), 'mutant_source_sha256': digest(changed),
                    'returncode': result.returncode, 'trace_sha256': digest(result.stdout),
                    'stderr_sha256': digest(result.stderr),
                }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert production_file.read_bytes() == production, 'real production source changed'
    print(f'{TEST_NAME}: shared comparison retained; 576 owner cases / 53786 lines; five actual Rust mutants rejected')


if __name__ == '__main__':
    main()
