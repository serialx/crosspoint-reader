"""Compare real position extraction in loop and caller-held resource contexts."""
import gzip
import hashlib
import itertools
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

TEST_NAME = 'reader_position_helper'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/position.rs')
GZIP_SHA = '5c487f4b048d098c7a73076e33842133596d4d83d4e4e7469ca3284d4d49d82b'
RAW_SHA = '64aba34e646ddb061e380bdbc3e8e6d5780a00dda74c32157a6c18ae64946562'
SUBSET_SHA = 'eecec5b8567ca79f9104d6a47145f31cb87e7bd42e36f8f8d8948b3be64a47a0'
MUTANTS = (
    ('paragraph-page-one', b'paragraph(if page > 0 {', b'paragraph(if page > 1 {'),
    ('visible-offset-flag', b'position.has_visible_offset = 1;', b'position.has_visible_offset = 0;'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def position_subset(raw):
    """Keep each original case header and its complete post-basis POSITION segment."""
    blocks = []
    current = None
    collecting = False
    for line in raw.splitlines(keepends=True):
        if line.startswith(b'case '):
            assert current is None, 'previous case did not produce POSITION'
            current = [line]
            collecting = False
        elif current is not None:
            if line.startswith(b'basis '):
                assert not collecting, 'duplicate basis before POSITION'
                collecting = True
            elif collecting:
                assert line.startswith((b'estimate ', b'paragraph ', b'offset ', b'position ')), line
                current.append(line)
                if line.startswith(b'position '):
                    blocks.append(current)
                    current = None
                    collecting = False
    assert current is None and len(blocks) == 4608
    headers = [f'case {flags} {page} {total} {spine}\n'.encode() for flags, page, total, spine in
               itertools.product(range(64), (-1, 0, 1, 4, 5, 65536), (-1, 0, 1, 5), (0, 3, 4))]
    assert [block[0] for block in blocks] == headers
    result = b''.join(line for block in blocks for line in block)
    assert digest(result) == SUBSET_SHA
    assert len(result) == 320688 and len(result.splitlines()) == 13824
    return result


def project(subset, mode):
    """Only change query depth; loop queries additionally acquire/release one lock."""
    output = []
    counts = dict(case=0, estimate=0, paragraph=0, offset=0, position=0, lock=0, unlock=0)
    for line in subset.splitlines(keepends=True):
        name = line.split(b' ', 1)[0].decode()
        counts[name] += 1
        if name in ('paragraph', 'offset'):
            match = re.fullmatch(rb'((?:paragraph|offset) [0-9]+) 0\n', line)
            assert match, line
            if mode == 'loop':
                output.append(b'lock 1 0\n')
                counts['lock'] += 1
            output.append(match[1] + b' 1\n')
            if mode == 'loop':
                output.append(b'unlock 1 0\n')
                counts['unlock'] += 1
        else:
            output.append(line)
    assert counts == dict(case=4608, estimate=2304, paragraph=1152, offset=1152, position=4608,
                         lock=2304 if mode == 'loop' else 0, unlock=2304 if mode == 'loop' else 0)
    result = b''.join(output)
    # Every POSITION byte, including all numbers, survives without normalization.
    assert [line for line in result.splitlines() if line.startswith(b'position ')] == [
        line for line in subset.splitlines() if line.startswith(b'position ')]
    return result, counts


def main():
    if len(sys.argv) != 1:
        raise SystemExit('No capture or golden regeneration mode is supported.')
    golden = HERE / 'projection.golden.gz'
    compressed = golden.read_bytes()
    raw = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(raw) == RAW_SHA
    subset = position_subset(raw)
    expected = {mode: project(subset, mode) for mode in ('loop', 'held')}
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    fixture = HERE / 'position_session_oracle.cpp'
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        fixture, HERE / 'position_session_fixture.h', Path(__file__),
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/position.rs')}

    try:
        with tempfile.TemporaryDirectory(prefix='reader-position-session-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def compile_fixture(name, archive=None):
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                executable = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(HERE), str(fixture), *flags, '-o', str(executable)], check=True)
                return executable

            def run(executable, mode):
                return subprocess.run([str(executable), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            executable = compile_fixture('position')
            for mode, (trace, counts) in expected.items():
                actual = run(executable, mode)
                assert actual.returncode == 0, actual.stderr.decode(errors='replace')
                assert actual.stdout == trace, (mode, digest(actual.stdout), digest(trace))
                print(json.dumps({
                    'test_name': TEST_NAME, 'mode': mode, 'cases': 4608,
                    'original_gzip_sha256': GZIP_SHA, 'original_raw_sha256': RAW_SHA,
                    'subset_sha256': SUBSET_SHA, 'subset_bytes': len(subset), 'subset_lines': 13824,
                    'projected_trace_sha256': digest(trace), 'actual_trace_sha256': digest(actual.stdout),
                    'projected_bytes': len(trace), 'projected_lines': len(trace.splitlines()),
                    'event_counts': counts, 'candidate_source_sha256': digest(production),
                    'fixture_sha256': fixture_hashes,
                }), flush=True)

            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            for name, before, after in MUTANTS:
                assert production.count(before) == 1, (name, 'mutation site must match once')
                changed = production.replace(before, after, 1)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                mutant = compile_fixture(name, target / 'release/libxp_ffi.a')
                for mode, (trace, _) in expected.items():
                    result = run(mutant, mode)
                    assert result.returncode != 0 or result.stdout != trace, (name, mode, 'mutant survived')
                    print(json.dumps({
                        'test_name': TEST_NAME, 'mode': mode, 'mutant': name, 'replacement_count': 1,
                        'source': str(PRODUCTION), 'original_source_sha256': digest(production),
                        'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                        'mutant_trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                        'expected_trace_sha256': digest(trace),
                    }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert production_file.read_bytes() == production, 'real production source changed'
    print(f'{TEST_NAME}: 4608 POSITION cases in each context; two actual Rust mutants rejected in each context')


if __name__ == '__main__':
    main()
