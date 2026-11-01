"""Retain shared input coverage and add real owner button/LTR-touch wrapper coverage."""
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

TEST_NAME = 'reader_base_input_wrappers'
GZIP_SHA = '26a897febf037897c3e64bddc52df23ddc2a7fd5dbf0cf96c83935ff7b475199'
RAW_SHA = '0c333dfafcfa4a9aa1b2ad924d855e09843d74a392b3591d1704a07b11473175'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/base.rs')
MUTANTS = (
    ('wrong-long-button-setting', b'long_press: settings.long_button,',
     b'long_press: settings.short_power,'),
    ('wrong-ltr-direction',
     b'geometry.width,\n                geometry.height,\n                false,',
     b'geometry.width,\n                geometry.height,\n                true,'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(executable):
    return subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)


def require_trace(result, expected, label):
    assert result.returncode == 0, (label, result.returncode, result.stderr.decode(errors='replace'))
    if result.stdout != expected:
        actual_lines, expected_lines = result.stdout.splitlines(), expected.splitlines()
        mismatch = next((index for index, pair in enumerate(zip(actual_lines, expected_lines), 1)
                         if pair[0] != pair[1]), min(len(actual_lines), len(expected_lines)) + 1)
        raise AssertionError((label, 'trace mismatch', mismatch, digest(result.stdout), digest(expected)))


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_input_session.py SHARED_READER_INPUT_ORACLE')
    shared_executable = Path(sys.argv[1]).resolve()
    golden = HERE / 'input.golden.gz'
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(expected) == RAW_SHA
    source_file = ROOT / 'rust' / PRODUCTION
    source = source_file.read_bytes()
    fixture = HERE / 'input_oracle.cpp'
    evidence_files = (
        fixture, HERE / 'input_session_rust.inc', HERE / 'overlay_actions_session_fixture.h',
        HERE / 'extract_input.py', HERE / 'extract.py', Path(__file__),
        ROOT / 'src/activities/reader/ReaderUtils.h',
        ROOT / 'src/activities/reader/ChapterPosition.h',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/input.rs',
        ROOT / 'rust/crates/xp-ffi/include/xp_epub_session.h',
    )
    fixture_hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in evidence_files}
    try:
        # Preserve the existing shared executable comparison before adding owner coverage.
        shared = run(shared_executable)
        require_trace(shared, expected, 'shared original')
        with tempfile.TemporaryDirectory(prefix='reader-input-session-') as directory:
            work = Path(directory)
            extracted = work / 'input_methods.inc'
            subprocess.run([sys.executable, str(HERE / 'extract_input.py'), str(ROOT), str(extracted)], check=True)
            baseline_flags = rust_link_flags()

            def compile_fixture(name, archive=None):
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                executable = work / name
                subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-DXP_INPUT_SESSION=1', '-I', str(work), '-I', str(HERE),
                                '-I', str(ROOT / 'src/activities/reader'), str(fixture),
                                *flags, '-o', str(executable)], check=True)
                return executable

            actual = run(compile_fixture('input-owner'))
            require_trace(actual, expected, 'owner buttons and LTR touch')
            print(json.dumps({
                'test_name': TEST_NAME, 'scope': 'A1 base buttons and LTR touch only',
                'pending_scope': 'EPUB RTL, menu and remote routes',
                'button_cases': 63360, 'owner_button_calls': 126720, 'owner_ltr_touch_calls': 6750,
                'shared_rtl_touch_cases': 6750, 'original_gzip_sha256': GZIP_SHA,
                'original_raw_sha256': RAW_SHA, 'shared_trace_sha256': digest(shared.stdout),
                'owner_trace_sha256': digest(actual.stdout), 'trace_bytes': len(expected),
                'trace_lines': len(expected.splitlines()), 'candidate_source_sha256': digest(source),
                'extracted_methods_sha256': digest(extracted.read_bytes()), 'fixture_sha256': fixture_hashes,
            }), flush=True)

            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == source
            target = work / 'target'
            for name, before, after in MUTANTS:
                assert source.count(before) == 1, (name, 'mutation site must match once')
                changed = source.replace(before, after, 1)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result = run(compile_fixture(name, target / 'release/libxp_ffi.a'))
                # Both mutants must execute successfully and change real observable policy.
                assert result.returncode == 0, (name, result.returncode, result.stderr.decode(errors='replace'))
                assert result.stdout != expected, (name, 'mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'replacement_count': 1,
                    'source': str(PRODUCTION), 'original_source_sha256': digest(source),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'mutant_trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                    'expected_trace_sha256': RAW_SHA,
                }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert source_file.read_bytes() == source, 'real production source changed'
    print(f'{TEST_NAME}: shared trace retained; owner buttons/LTR touch and two Rust mutants passed; '
          'full EPUB input adaptation remains pending')


if __name__ == '__main__':
    main()
